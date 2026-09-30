#include "ValorCombatComponent.h"

#include "AbilitySystem/Abilities/UGA_WeaponADS.h"
#include "AbilitySystem/Abilities/UGA_WeaponFire.h"
#include "AbilitySystem/Abilities/UGA_WeaponReload.h"
#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystem/ValorAbilitySystemComponent.h"
#include "Components/ValorCameraComponent.h"
#include "Components/ValorLagCompensationComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "ValorCharacter.h"
#include "Weapons/ValorWeaponBase.h"
#include "World/ValorWeaponPickup.h"

DEFINE_LOG_CATEGORY_STATIC(LogValorCombat, Log, All);

namespace
{
	// 연사 타이머가 프레임 경계 때문에 늦게 불린 경우, 이 시간(초) 이내면 발사 시각을 "이상적인 연사 시각"으로 맞춘다.
	constexpr double MaxScheduleSlackSeconds = 0.05;
}

UValorCombatComponent::UValorCombatComponent()
{
	// 발사/재장전은 입력·RPC·타이머 이벤트로만 동작하므로 매 프레임 Tick이 필요 없다(서버 비용 절감).
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	FireAbilityClass = UUGA_WeaponFire::StaticClass();
	ReloadAbilityClass = UUGA_WeaponReload::StaticClass();
	ADSAbilityClass = UUGA_WeaponADS::StaticClass();
}

void UValorCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<AValorCharacter>(GetOwner());
	ShotRateTokens = ShotRateBurstCapacity;
}

void UValorCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UValorCombatComponent, EquippedWeapon);
	DOREPLIFETIME(UValorCombatComponent, bIsADS);
	DOREPLIFETIME(UValorCombatComponent, bIsReloading);
}

void UValorCombatComponent::InitializeAbilityBindings(UValorAbilitySystemComponent* InAbilitySystemComponent)
{
	AbilitySystemComponent = InAbilitySystemComponent;
	if (!AbilitySystemComponent || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	AbilitySystemComponent->GrantAbilityIfAbsent(FireAbilityClass, 1, FireAbilityHandle);
	AbilitySystemComponent->GrantAbilityIfAbsent(ReloadAbilityClass, 1, ReloadAbilityHandle);
	AbilitySystemComponent->GrantAbilityIfAbsent(ADSAbilityClass, 1, ADSAbilityHandle);
}

void UValorCombatComponent::HandleFireInputPressed()
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	bLocalFireHeld = true;
	TryFireLocalShot();
}

void UValorCombatComponent::HandleFireInputReleased()
{
	bLocalFireHeld = false;

	// 연사 속도보다 빨리 눌러 예약된 탭(버퍼된 한 발)은 버튼을 떼도 쏜다. 그 외의 연사 예약만 취소한다.
	if (bFireShotBuffered)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LocalFireTimerHandle);
	}
}

void UValorCombatComponent::HandleReloadInputPressed()
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	ServerRequestReload();
}

void UValorCombatComponent::HandleADSInputPressed()
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	// 소유 클라는 입력 의도를 바로 반영해 ADS 카메라와 반동 예측을 즉시 전환한다(서버 승인 대기 없음).
	// 리슨 호스트는 아래 RPC가 즉시 실행되어 bIsADS가 먼저 바뀌므로, RPC 호출 뒤에 카메라를 갱신한다.
	bLocalADSIntent = true;
	ServerSetADSInput(true);
	RefreshADSOnLocalClient();
}

void UValorCombatComponent::HandleADSInputReleased()
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	bLocalADSIntent = false;
	ServerSetADSInput(false);
	RefreshADSOnLocalClient();
}

void UValorCombatComponent::HandleInteractInputPressed()
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	ServerInteractWithPickup();
}

void UValorCombatComponent::TryFireLocalShot()
{
	UWorld* World = GetWorld();
	if (!World || !OwnerCharacter || !GetOwner())
	{
		return;
	}

	World->GetTimerManager().ClearTimer(LocalFireTimerHandle);

	// 버퍼된 탭은 이미 떼어진 버튼이라도 한 발은 쏘고, 그 다음 연사는 버튼을 누르고 있을 때만 이어간다.
	const bool bFiringBufferedShot = bFireShotBuffered;
	bFireShotBuffered = false;
	if ((!bLocalFireHeld && !bFiringBufferedShot) || !EquippedWeapon || bIsReloading)
	{
		return;
	}

	const double FireInterval = EquippedWeapon->GetFireInterval(IsADSForGameplay());
	const double Now = GetSynchronizedTime();
	const double EarliestShotTime = LastLocalShotTime + FireInterval;

	// 연사 속도보다 빠르게 눌렀다면 입력을 버리지 않고 "쏠 수 있는 가장 이른 시각"으로 예약한다(탭 버퍼).
	if (Now + KINDA_SMALL_NUMBER < EarliestShotTime)
	{
		bFireShotBuffered = true;
		World->GetTimerManager().SetTimer(LocalFireTimerHandle, this, &UValorCombatComponent::TryFireLocalShot, static_cast<float>(EarliestShotTime - Now), false);
		return;
	}

	// 탄약은 서버 권위지만, RTT만큼 늦게 오는 복제값 대신 "비행 중인 예측 발"을 뺀 예측 탄약으로 판단해 빈 탄창 헛발을 막는다.
	if (EquippedWeapon->GetPredictedMagazineAmmo() <= 0)
	{
		return;
	}

	// 연사 중 타이머가 프레임 경계 때문에 조금 늦게 불려도 발사 시각은 이상적인 간격으로 맞춘다.
	// → 서버/클라 모두 "정확히 연사 속도대로 쐈다"로 계산하므로 풀오토 패턴이 프레임레이트와 무관해진다(결정성).
	const double ShotTime = (Now - EarliestShotTime) <= MaxScheduleSlackSeconds ? EarliestShotTime : Now;

	FValorShotRequest Request;
	Request.ClientShotTime = static_cast<float>(ShotTime);
	// 조준은 카메라(뷰 펀치 포함)가 아니라 컨트롤 회전이다. ADS 반동 카메라가 조준 입력에 섞여 들어가지 않게 한다.
	Request.AimRotation = OwnerCharacter->GetViewRotation();
	// 서버가 받게 될 값(압축·복원 결과)과 비트 단위로 같게 만든 뒤, 그 값으로 예측한다.
	Request.Quantize();
	LastLocalShotTime = Request.ClientShotTime;

	if (GetOwner()->HasAuthority())
	{
		// 리슨 서버 호스트: 로컬이 곧 서버이므로 예측 없이 권위 경로를 바로 실행한다(연출도 그 안에서 재생).
		SubmitShotToFireAbility(Request);
	}
	else
	{
		// 원격 소유 클라: 즉시 예측 연출 → 서버에 입력(시각·조준)만 전송. 판정은 서버가 독자적으로 다시 계산한다.
		PredictLocalShot(Request);
		ServerFireShot(Request);
	}

	if (bLocalFireHeld && EquippedWeapon && EquippedWeapon->IsAutomatic())
	{
		// 다음 발 예약은 "지금"이 아니라 "이상적인 다음 발사 시각" 기준으로 잡아 지연이 누적되지 않게 한다.
		const double NextShotTime = Request.ClientShotTime + FireInterval;
		const float Delay = FMath::Max(0.001f, static_cast<float>(NextShotTime - GetSynchronizedTime()));
		World->GetTimerManager().SetTimer(LocalFireTimerHandle, this, &UValorCombatComponent::TryFireLocalShot, Delay, false);
	}
}

void UValorCombatComponent::SubmitShotToFireAbility(const FValorShotRequest& Request)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (!AbilitySystemComponent || !FireAbilityHandle.IsValid() || bIsReloading)
	{
		return;
	}

	// GAS 흐름 유지: 서버가 발사 어빌리티를 활성화하고, 어빌리티가 ExecuteServerFireAbility로 이 요청을 처리한다.
	// 발사 자체를 어빌리티로 두면 기절/무장해제 같은 상태이상 태그로 발사를 막는 확장을 GAS 규칙만으로 할 수 있다.
	PendingShotRequest = Request;
	bHasPendingShotRequest = true;
	AbilitySystemComponent->TryActivateAbility(FireAbilityHandle);
	bHasPendingShotRequest = false;
}

void UValorCombatComponent::PredictLocalShot(const FValorShotRequest& Request)
{
	UWorld* World = GetWorld();
	if (!World || !OwnerCharacter || !EquippedWeapon)
	{
		return;
	}

	// 서버와 똑같은 함수·입력(시각, 조준, 시드 번호, 자세)으로 이번 발을 계산한다.
	// 로컬 스프레이 상태가 한 발 전진하므로 ADS 카메라 반동과 크로스헤어 탄퍼짐도 다음 프레임에 바로 반영된다.
	const FValorShooterStance Stance = BuildShooterStance();
	const FValorComputedShotData ShotData = EquippedWeapon->SimulateShot(Request.ClientShotTime, Stance);

	FVector TraceStart = FVector::ZeroVector;
	FRotator IgnoredViewRotation = FRotator::ZeroRotator;
	OwnerCharacter->GetWeaponViewPoint(TraceStart, IgnoredViewRotation);

	const FVector ShotDirection = EquippedWeapon->ComputeShotDirection(Request.AimRotation, ShotData);
	const FVector TraceEnd = TraceStart + (ShotDirection * EquippedWeapon->GetTraceDistance());

	// 연출용 로컬 트레이스(피해 판정 아님). 서버와 같은 방향이라 벽 탄흔 위치는 서버 결과와 거의 항상 일치한다.
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ValorPredictedShotTrace), false, OwnerCharacter);
	QueryParams.AddIgnoredActor(EquippedWeapon);
	const bool bBlockingHit = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	const bool bHitCharacter = bBlockingHit && Cast<AValorCharacter>(Hit.GetActor()) != nullptr;

	PlayLocalShotPresentation(
		bBlockingHit ? FVector(Hit.ImpactPoint) : TraceEnd,
		bBlockingHit ? FVector(Hit.ImpactNormal) : -ShotDirection,
		bBlockingHit,
		bHitCharacter);
}

void UValorCombatComponent::ExecuteServerFireAbility()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !OwnerCharacter || !EquippedWeapon || bIsReloading || !bHasPendingShotRequest)
	{
		return;
	}

	FValorShotRequest Request = PendingShotRequest;
	bHasPendingShotRequest = false;

	if (!ValidateShotRequest(Request))
	{
		return;
	}

	// 서버 권위 계산: 서버가 가진 스프레이 상태·자세로 반동/탄퍼짐을 직접 계산한다. 클라의 예측 결과는 받지도 쓰지도 않는다.
	const FValorShooterStance Stance = BuildShooterStance();
	const FValorComputedShotData ShotData = EquippedWeapon->SimulateShot(Request.ClientShotTime, Stance);
	EquippedWeapon->ConsumeAmmo();
	LastAcceptedShotTime = Request.ClientShotTime;

	// 탄 시작점은 서버가 아는 캐릭터 카메라 위치(클라가 보낸 위치를 믿지 않음), 방향은 검증된 조준 입력 + 서버 계산 반동/탄퍼짐.
	FVector TraceStart = FVector::ZeroVector;
	FRotator IgnoredViewRotation = FRotator::ZeroRotator;
	OwnerCharacter->GetWeaponViewPoint(TraceStart, IgnoredViewRotation);
	const FVector ShotDirection = EquippedWeapon->ComputeShotDirection(Request.AimRotation, ShotData);

	// 발사 시각으로 되감아 판정한다(랙 보상): 클라가 방아쇠를 당긴 순간 화면에 보이던 적 위치 기준.
	FValorHitScanResult HitResult;
	PerformServerHitScan(TraceStart, ShotDirection, EquippedWeapon->GetTraceDistance(), Request.ClientShotTime, HitResult);

	if (HitResult.HitCharacter)
	{
		if (UValorAbilitySystemComponent* TargetASC = Cast<UValorAbilitySystemComponent>(HitResult.HitCharacter->GetAbilitySystemComponent()))
		{
			float Damage = EquippedWeapon->ComputeDamage(HitResult.TravelDistance, HitResult.HitZone);
			if (HitResult.bPenetratedSurface)
			{
				Damage *= EquippedWeapon->GetPenetrationDamageMultiplier();
			}

			TargetASC->ApplyModToAttribute(UValorCombatAttributeSet::GetIncomingDamageAttribute(), EGameplayModOp::Additive, Damage);
		}
	}

	// 연출: 로컬 사수가 곧 서버인 리슨 호스트는 여기서 바로 재생하고, 나머지 클라는 멀티캐스트로 받는다.
	const bool bHitCharacter = HitResult.HitCharacter != nullptr;
	if (OwnerCharacter->IsLocallyControlled())
	{
		PlayLocalShotPresentation(HitResult.ImpactPoint, HitResult.ImpactNormal, HitResult.bBlockingHit, bHitCharacter);
	}

	MulticastSimulateFire(HitResult.ImpactPoint, HitResult.ImpactNormal, HitResult.bBlockingHit, bHitCharacter);
}

bool UValorCombatComponent::ValidateShotRequest(FValorShotRequest& InOutRequest)
{
	UWorld* World = GetWorld();
	if (!World || !OwnerCharacter || !EquippedWeapon)
	{
		return false;
	}

	const double ServerNow = World->GetTimeSeconds();
	const double FireInterval = EquippedWeapon->GetFireInterval(IsADSForGameplay());

	// (1) 발사 시각 범위: 너무 먼 과거(되감기 한도 밖)나 미래 시각은 허용 범위로 끌어온다.
	//     클라 시계(GameState 동기화 시간)는 서버보다 약간 뒤처지는 것이 정상이므로 과거 쪽은 되감기 한도까지 허용한다.
	const double MinShotTime = ServerNow - MaxShotRewindSeconds;
	const double MaxShotTime = ServerNow + MaxShotTimeLeadSeconds;
	if (InOutRequest.ClientShotTime < MinShotTime || InOutRequest.ClientShotTime > MaxShotTime)
	{
		UE_LOG(LogValorCombat, Verbose, TEXT("[발사 검증] %s 발사 시각 보정: 주장 %.3f / 서버 %.3f"), *GetNameSafe(OwnerCharacter), InOutRequest.ClientShotTime, ServerNow);

		// 되감기 한도를 넘는 고지연 클라: 여러 발이 한 패킷에 몰려 와도 같은 시각으로 뭉개져 오거부되지 않도록,
		// 직전 승인 발 + 발사 간격으로 연사 리듬을 재구성한다(실제 초당 발사 수는 아래 토큰 버킷이 제한한다).
		const double ClampedTime = FMath::Clamp<double>(InOutRequest.ClientShotTime, MinShotTime, ServerNow);
		InOutRequest.ClientShotTime = static_cast<float>(FMath::Max(ClampedTime, LastAcceptedShotTime + FireInterval));
	}
	else if (InOutRequest.ClientShotTime + KINDA_SMALL_NUMBER < LastAcceptedShotTime + (FireInterval * FireIntervalTolerance))
	{
		// (2) 연사 속도: 주장된 시각 기준으로 직전 승인 발과의 간격이 발사 간격보다 짧으면 거부한다.
		RejectShot(TEXT("연사 속도 위반(주장 시각 간격)"), true);
		return false;
	}

	// (3) 실제 수신 속도 제한(토큰 버킷): 시각을 위조해 간격을 벌려도 "초당 도착한 발 수"는 속일 수 없다.
	const double RefillPerSecond = (1.0 / FireInterval) * 1.1;
	ShotRateTokens = FMath::Min<double>(ShotRateBurstCapacity, ShotRateTokens + FMath::Max(0.0, ServerNow - LastShotRateRefillTime) * RefillPerSecond);
	LastShotRateRefillTime = ServerNow;
	if (ShotRateTokens < 1.0)
	{
		RejectShot(TEXT("연사 속도 위반(실수신 속도)"), true);
		return false;
	}
	ShotRateTokens -= 1.0;

	// (4) 탄약: 탄창 끝에서 예측이 한 발 앞서는 것은 정상이므로 경고가 아닌 상세 로그로 남긴다.
	if (!EquippedWeapon->HasAmmo())
	{
		RejectShot(TEXT("탄약 없음"), false);
		return false;
	}

	// (5) 조준 정합성: 조준은 입력이므로 받아들이되, 서버가 아는 시점과 비정상적으로 다르면 서버 시점으로 대체한다.
	FRotator AimRotation = InOutRequest.AimRotation.GetNormalized();
	AimRotation.Pitch = FMath::Clamp(AimRotation.Pitch, -89.9, 89.9);
	AimRotation.Roll = 0.0;

	const FRotator ServerViewRotation = OwnerCharacter->GetViewRotation();
	const double AimDot = FMath::Clamp(FVector::DotProduct(AimRotation.Vector(), ServerViewRotation.Vector()), -1.0, 1.0);
	const double AimDivergenceDegrees = FMath::RadiansToDegrees(FMath::Acos(AimDot));
	if (AimDivergenceDegrees > MaxAimDivergenceDegrees)
	{
		UE_LOG(LogValorCombat, Warning, TEXT("[발사 검증] %s 조준 불일치 %.1f도 → 서버 시점으로 대체"), *GetNameSafe(OwnerCharacter), AimDivergenceDegrees);
		AimRotation = ServerViewRotation;
	}

	InOutRequest.AimRotation = AimRotation;
	return true;
}

void UValorCombatComponent::RejectShot(const TCHAR* Reason, bool bSuspicious)
{
	++RejectedShotCount;

	if (bSuspicious)
	{
		UE_LOG(LogValorCombat, Warning, TEXT("[발사 검증] %s 발사 거부: %s (누적 거부 %d회)"), *GetNameSafe(OwnerCharacter), Reason, RejectedShotCount);
		return;
	}

	UE_LOG(LogValorCombat, Verbose, TEXT("[발사 검증] %s 발사 거부: %s"), *GetNameSafe(OwnerCharacter), Reason);
}

void UValorCombatComponent::PlayLocalShotPresentation(const FVector& ImpactPoint, const FVector& ImpactNormal, bool bBlockingHit, bool bHitCharacter)
{
	// 애니메이션 인스턴스가 발사 몽타주를 한 번 재생할 수 있도록 로컬 발사 시각을 남긴다.
	if (const UWorld* World = GetWorld())
	{
		LastFireSimulationWorldTime = static_cast<float>(World->GetTimeSeconds());
	}

	if (!EquippedWeapon)
	{
		return;
	}

	EquippedWeapon->PlayFireEffects(ImpactPoint, ImpactNormal, bBlockingHit, bHitCharacter);

	// 사수 본인 화면만 매 발 튀게 한다(발로란트의 사격 시 화면 흔들림). 예측 클라는 발사 입력 프레임에 바로 실행되므로
	// 서버 왕복을 기다리지 않고 킥이 즉시 나온다. 조준(컨트롤 회전)은 건드리지 않는 순수 연출이다.
	if (OwnerCharacter && OwnerCharacter->IsLocallyControlled())
	{
		if (UValorCameraComponent* CameraLogicComponent = OwnerCharacter->GetCameraLogicComponent())
		{
			CameraLogicComponent->AddFireKick(EquippedWeapon->GetCameraKickConfig(IsADSForGameplay()));
		}
	}
}

void UValorCombatComponent::ExecuteServerReloadAbility()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !EquippedWeapon || bIsReloading || !EquippedWeapon->CanReload())
	{
		return;
	}

	bIsReloading = true;
	MulticastPlayReloadCue();
	GetWorld()->GetTimerManager().SetTimer(ReloadTimerHandle, this, &UValorCombatComponent::FinishReload, EquippedWeapon->GetReloadDuration(), false);
}

void UValorCombatComponent::SetADSStateFromAbility(bool bNewADS)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	bIsADS = bNewADS && EquippedWeapon != nullptr && !bIsReloading;
	RefreshADSOnLocalClient();
}

bool UValorCombatComponent::IsADSForGameplay() const
{
	const bool bIsPredictingClient = OwnerCharacter && OwnerCharacter->IsLocallyControlled() && GetOwner() && !GetOwner()->HasAuthority();
	if (bIsPredictingClient)
	{
		// 서버의 승인 조건(SetADSStateFromAbility)과 같은 조건으로 예측해 대부분의 경우 서버 결과와 일치시킨다.
		return bLocalADSIntent && EquippedWeapon != nullptr && !bIsReloading;
	}

	return bIsADS;
}

FValorShooterStance UValorCombatComponent::BuildShooterStance() const
{
	FValorShooterStance Stance;
	Stance.bIsADS = IsADSForGameplay();

	if (!OwnerCharacter)
	{
		return Stance;
	}

	Stance.HorizontalSpeed = OwnerCharacter->GetVelocity().Size2D();
	Stance.WalkSpeed = OwnerCharacter->GetWalkSpeed();
	Stance.RunSpeed = OwnerCharacter->GetRunSpeed();
	Stance.CrouchSpeed = OwnerCharacter->GetCrouchSpeed();

	if (const UCharacterMovementComponent* MovementComponent = OwnerCharacter->GetCharacterMovement())
	{
		Stance.bIsCrouched = MovementComponent->IsCrouching();
		Stance.bIsAirborne = MovementComponent->IsFalling();
	}

	if (const UWorld* World = GetWorld())
	{
		Stance.TimeSinceLanded = static_cast<float>(World->GetTimeSeconds() - OwnerCharacter->GetLastLandedWorldTime());
	}

	return Stance;
}

double UValorCombatComponent::GetSynchronizedTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}

	// 서버에서는 월드 시간과 같고, 클라에서는 서버 시간 추정치다. 발사 시각·스프레이 회복·랙 보상이 모두 이 시간축을 공유한다.
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}

	return World->GetTimeSeconds();
}

bool UValorCombatComponent::GetCurrentSprayEvaluation(FValorSprayEvaluation& OutEvaluation, FValorShooterStance& OutStance) const
{
	if (!EquippedWeapon)
	{
		return false;
	}

	OutStance = BuildShooterStance();
	OutEvaluation = EquippedWeapon->EvaluateSpray(GetSynchronizedTime(), OutStance);
	return true;
}

FRotator UValorCombatComponent::GetCameraRecoilOffset() const
{
	FValorSprayEvaluation Evaluation;
	FValorShooterStance Stance;
	if (!GetCurrentSprayEvaluation(Evaluation, Stance))
	{
		return FRotator::ZeroRotator;
	}

	// 힙파이어(0.5): 화면이 반동의 절반만 따라 올라가고 탄은 크로스헤어보다 더 위에 맞는다. ADS(1): 조준점이 반동을 끝까지 따라간다.
	// 탄퍼짐(무작위 오차)은 카메라에 넣지 않는다 — 발로란트도 조준점은 결정적 반동만 따라간다.
	const float FollowRatio = EquippedWeapon->GetCameraRecoilFollowRatio(Stance.bIsADS);
	return FRotator(Evaluation.RecoilPitchDegrees * FollowRatio, Evaluation.RecoilYawDegrees * FollowRatio, 0.0f);
}

void UValorCombatComponent::ServerFireShot_Implementation(const FValorShotRequest& Request)
{
	SubmitShotToFireAbility(Request);
}

void UValorCombatComponent::ServerRequestReload_Implementation()
{
	if (!AbilitySystemComponent || !ReloadAbilityHandle.IsValid())
	{
		return;
	}

	AbilitySystemComponent->TryActivateAbility(ReloadAbilityHandle);
}

void UValorCombatComponent::ServerSetADSInput_Implementation(bool bNewADS)
{
	if (!AbilitySystemComponent || !ADSAbilityHandle.IsValid())
	{
		return;
	}

	if (bNewADS)
	{
		AbilitySystemComponent->TryActivateAbility(ADSAbilityHandle);
		return;
	}

	AbilitySystemComponent->CancelAbilityHandle(ADSAbilityHandle);
}

void UValorCombatComponent::ServerInteractWithPickup_Implementation()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !OwnerCharacter)
	{
		return;
	}

	if (AValorWeaponPickup* Pickup = FindPickupInView())
	{
		if (AValorWeaponBase* SpawnedWeapon = Pickup->SpawnWeaponForPickup(OwnerCharacter))
		{
			EquipWeapon(SpawnedWeapon);
		}
	}
}

void UValorCombatComponent::MulticastSimulateFire_Implementation(FVector_NetQuantize ImpactPoint, FVector_NetQuantizeNormal ImpactNormal, bool bBlockingHit, bool bHitCharacter)
{
	if (!OwnerCharacter || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 사수 본인(예측 클라 / 리슨 호스트)은 이미 로컬에서 재생했으므로 중복 재생하지 않는다.
	if (OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		LastFireSimulationWorldTime = static_cast<float>(World->GetTimeSeconds());
	}

	if (EquippedWeapon)
	{
		EquippedWeapon->PlayFireEffects(ImpactPoint, ImpactNormal, bBlockingHit, bHitCharacter);
	}
}

void UValorCombatComponent::MulticastPlayReloadCue_Implementation()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 이후 애니메이션 몽타주와 사운드를 붙일 수 있도록 리로드 Cue만 열어둔다.
}

void UValorCombatComponent::OnRep_EquippedWeapon(AValorWeaponBase* PreviousWeapon)
{
	if (PreviousWeapon && PreviousWeapon != EquippedWeapon)
	{
		PreviousWeapon->OnUnequipped();
	}

	ApplyEquippedWeaponAttachment();
	RefreshADSOnLocalClient();
}

void UValorCombatComponent::OnRep_IsADS()
{
	RefreshADSOnLocalClient();
}

void UValorCombatComponent::OnRep_IsReloading()
{
	// 재장전 시작 시에는 ADS가 풀리고(IsADSForGameplay가 false), 끝나면 우클릭 유지 여부에 따라 다시 조준한다.
	RefreshADSOnLocalClient();
}

void UValorCombatComponent::EquipWeapon(AValorWeaponBase* NewWeapon)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !NewWeapon)
	{
		return;
	}

	if (EquippedWeapon)
	{
		EquippedWeapon->Destroy();
	}

	EquippedWeapon = NewWeapon;
	EquippedWeapon->OnEquippedBy(OwnerCharacter);
	bIsReloading = false;
	bIsADS = false;
	LastAcceptedShotTime = -1000.0;
	ApplyEquippedWeaponAttachment();
}

void UValorCombatComponent::ApplyEquippedWeaponAttachment() const
{
	if (EquippedWeapon && OwnerCharacter)
	{
		EquippedWeapon->OnEquippedBy(OwnerCharacter);
	}
}

void UValorCombatComponent::RefreshADSOnLocalClient() const
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	UValorCameraComponent* CameraLogicComponent = OwnerCharacter->GetCameraLogicComponent();
	if (!CameraLogicComponent)
	{
		return;
	}

	if (IsADSForGameplay() && EquippedWeapon)
	{
		CameraLogicComponent->SetADSState(true, EquippedWeapon->GetADSFieldOfView(CameraLogicComponent->GetHipFireFOV()), EquippedWeapon->GetADSInterpSpeed());
		return;
	}

	CameraLogicComponent->SetADSState(false, 0.0f, 0.0f);
}

void UValorCombatComponent::FinishReload()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !EquippedWeapon)
	{
		return;
	}

	EquippedWeapon->ReloadFromReserve();
	bIsReloading = false;
	RefreshADSOnLocalClient();
}

AValorWeaponPickup* UValorCombatComponent::FindPickupInView() const
{
	if (!OwnerCharacter)
	{
		return nullptr;
	}

	FVector TraceStart = FVector::ZeroVector;
	FRotator TraceRotation = FRotator::ZeroRotator;
	OwnerCharacter->GetWeaponViewPoint(TraceStart, TraceRotation);

	const FVector TraceEnd = TraceStart + (TraceRotation.Vector() * 350.0f);
	FHitResult HitResult;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ValorPickupTrace), false, OwnerCharacter);
	if (!GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
	{
		return nullptr;
	}

	return Cast<AValorWeaponPickup>(HitResult.GetActor());
}

void UValorCombatComponent::PerformServerHitScan(const FVector& TraceStart, const FVector& ShotDirection, float MaxDistance, float ClientShotTimestampSeconds, FValorHitScanResult& OutResult) const
{
	const FVector TraceEnd = TraceStart + (ShotDirection * MaxDistance);

	OutResult = FValorHitScanResult();
	OutResult.ImpactPoint = TraceEnd;
	OutResult.ImpactNormal = -ShotDirection;
	OutResult.TravelDistance = MaxDistance;

	UWorld* World = GetWorld();
	if (!World || !OwnerCharacter)
	{
		return;
	}

	float BlockingDistance = MaxDistance;
	bool bBlockedByWorld = false;

	// 캐릭터는 아래에서 랙 보상 히트박스로 따로 판정하므로, 월드 트레이스에서는 모든 캐릭터를 무시하고 벽만 찾는다.
	FCollisionQueryParams WorldTraceParams(SCENE_QUERY_STAT(ValorWeaponWorldTrace), false, OwnerCharacter);
	for (TActorIterator<AValorCharacter> It(World); It; ++It)
	{
		WorldTraceParams.AddIgnoredActor(*It);
	}

	FHitResult WorldBlockingHit;
	if (World->LineTraceSingleByChannel(WorldBlockingHit, TraceStart, TraceEnd, ECC_Visibility, WorldTraceParams))
	{
		BlockingDistance = WorldBlockingHit.Distance;
		bBlockedByWorld = true;
		OutResult.bBlockingHit = true;
		OutResult.ImpactPoint = WorldBlockingHit.ImpactPoint;
		OutResult.ImpactNormal = WorldBlockingHit.ImpactNormal;
		OutResult.TravelDistance = BlockingDistance;
	}

	float BestDistance = TNumericLimits<float>::Max();
	AValorCharacter* BestCharacter = nullptr;
	EValorHitZone BestZone = EValorHitZone::None;
	FVector BestImpactPoint = TraceEnd;
	bool bBestPenetrated = false;

	for (TActorIterator<AValorCharacter> It(World); It; ++It)
	{
		AValorCharacter* CandidateCharacter = *It;
		if (!CandidateCharacter || CandidateCharacter == OwnerCharacter)
		{
			continue;
		}

		UValorLagCompensationComponent* LagCompComponent = CandidateCharacter->GetLagCompensationComponent();
		if (!LagCompComponent)
		{
			continue;
		}

		// 발사 시각의 히트박스 스냅샷으로 판정한다(서버 되감기).
		FValorLagCompHitResult CandidateHit;
		if (!LagCompComponent->ConfirmHitAtTime(TraceStart, ShotDirection, MaxDistance, ClientShotTimestampSeconds, CandidateHit))
		{
			continue;
		}

		const bool bPassesWithoutPenetration = CandidateHit.HitDistance <= BlockingDistance + KINDA_SMALL_NUMBER;
		const bool bCanPenetrate = bBlockedByWorld
			&& EquippedWeapon
			&& EquippedWeapon->GetPenetrationTier() != EValorWallPenetrationTier::Low
			&& CandidateHit.HitDistance <= (BlockingDistance + EquippedWeapon->GetPenetrationDepth());

		if (!bPassesWithoutPenetration && !bCanPenetrate)
		{
			continue;
		}

		if (CandidateHit.HitDistance < BestDistance)
		{
			BestDistance = CandidateHit.HitDistance;
			BestCharacter = CandidateCharacter;
			BestZone = CandidateHit.HitZone;
			BestImpactPoint = CandidateHit.ImpactPoint;
			bBestPenetrated = bCanPenetrate && !bPassesWithoutPenetration;
		}
	}

	if (!BestCharacter)
	{
		return;
	}

	OutResult.HitCharacter = BestCharacter;
	OutResult.HitZone = BestZone;
	OutResult.ImpactPoint = BestImpactPoint;
	OutResult.ImpactNormal = -ShotDirection;
	OutResult.TravelDistance = BestDistance;
	OutResult.bBlockingHit = true;
	OutResult.bPenetratedSurface = bBestPenetrated;
}

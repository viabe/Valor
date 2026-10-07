#include "ValorCombatComponent.h"

#include "AbilitySystem/Abilities/UGA_WeaponADS.h"
#include "AbilitySystem/Abilities/UGA_WeaponFire.h"
#include "AbilitySystem/Abilities/UGA_WeaponReload.h"
#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystem/ValorAbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
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
	DroppedWeaponPickupClass = AValorWeaponPickup::StaticClass();
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
	bLocalFiringAltAttack = false;
	TryFireLocalShot();
}

void UValorCombatComponent::HandleFireInputReleased()
{
	bLocalFireHeld = false;

	// 연사 속도보다 빨리 눌러 예약된 탭(버퍼된 한 발)과 이미 시작한 점사는 버튼을 떼도 쏜다. 그 외의 연사 예약만 취소한다.
	if (bFireShotBuffered || (EquippedWeapon && EquippedWeapon->IsBurstInProgress()))
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

	bLocalADSButtonHeld = true;
	if (!EquippedWeapon)
	{
		return;
	}

	// 우클릭이 공격인 총(클래식 3펠릿 산탄, 버키 캐니스터): 조준이 아니라 보조 발사다(한 번 누르면 한 발).
	if (EquippedWeapon->IsAltFireAttack())
	{
		bLocalAltFireHeld = true;
		bLocalFiringAltAttack = true;
		TryFireLocalShot();
		return;
	}

	// 조준: 저격총은 발로란트 기본처럼 토글(1단 → 2단 → 해제), 그 외 총은 누르고 있는 동안 조준(설정으로 바꿀 수 있다).
	GetWorld()->GetTimerManager().ClearTimer(ReScopeTimerHandle);
	const bool bToggle = EquippedWeapon->IsSniper() ? bToggleSniperZoom : bToggleADS;
	if (bToggle)
	{
		SetLocalZoomLevel(LocalZoomLevel >= EquippedWeapon->GetMaxZoomLevel() ? 0 : LocalZoomLevel + 1);
		return;
	}

	SetLocalZoomLevel(1);
}

void UValorCombatComponent::HandleADSInputReleased()
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	bLocalADSButtonHeld = false;

	// 우클릭 공격 버튼을 뗐다(반자동이라 이미 쏜 발은 그대로).
	if (bLocalAltFireHeld)
	{
		bLocalAltFireHeld = false;
		return;
	}

	const bool bToggle = EquippedWeapon && (EquippedWeapon->IsSniper() ? bToggleSniperZoom : bToggleADS);
	if (!bToggle)
	{
		GetWorld()->GetTimerManager().ClearTimer(ReScopeTimerHandle);
		SetLocalZoomLevel(0);
	}
}

void UValorCombatComponent::SetLocalZoomLevel(int32 NewZoomLevel)
{
	const int32 MaxZoomLevel = EquippedWeapon ? EquippedWeapon->GetMaxZoomLevel() : 0;
	NewZoomLevel = FMath::Clamp(NewZoomLevel, 0, MaxZoomLevel);

	const bool bWasScoped = LocalZoomLevel > 0;
	LocalZoomLevel = NewZoomLevel;
	const bool bScoped = LocalZoomLevel > 0;

	// 소유 클라는 입력 의도를 바로 반영해 조준 카메라와 반동 예측을 즉시 전환한다(서버 승인 대기 없음).
	// 리슨 호스트는 아래 RPC가 즉시 실행되어 bIsADS가 먼저 바뀌므로, RPC 호출 뒤에 카메라를 갱신한다.
	bLocalADSIntent = bScoped;
	if (bWasScoped != bScoped)
	{
		ServerSetADSInput(bScoped);
	}

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
	// 점사는 한 번 시작하면 버튼을 떼도 끝까지 쏜다(발로란트 불독·스팅어 ADS).
	const bool bFiringBufferedShot = bFireShotBuffered;
	bFireShotBuffered = false;
	const bool bBurstInProgress = EquippedWeapon && EquippedWeapon->IsBurstInProgress();
	const bool bTriggerHeld = bLocalFiringAltAttack ? bLocalAltFireHeld : bLocalFireHeld;
	if ((!bTriggerHeld && !bFiringBufferedShot && !bBurstInProgress) || !EquippedWeapon || bIsReloading)
	{
		return;
	}

	// 다음 발까지 최소 간격은 직전 발이 정한다(점사 간격/점사 사이 대기/오딘 가속). 서버도 같은 값으로 검증한다.
	const double Now = GetSynchronizedTime();
	const double EarliestShotTime = LastLocalShotTime + EquippedWeapon->GetNextShotCooldown();

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
	Request.bAltFire = bLocalFiringAltAttack;
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

	// 다음 발 예약: 점사가 남았으면 버튼과 무관하게, 아니면 자동 무기를 누르고 있을 때만(우클릭 공격은 항상 반자동).
	const bool bContinueFiring = EquippedWeapon
		&& (EquippedWeapon->IsBurstInProgress() || (!bLocalFiringAltAttack && bLocalFireHeld && EquippedWeapon->IsAutomatic()));
	if (bContinueFiring)
	{
		// 다음 발 예약은 "지금"이 아니라 "이상적인 다음 발사 시각" 기준으로 잡아 지연이 누적되지 않게 한다.
		const double NextShotTime = Request.ClientShotTime + EquippedWeapon->GetNextShotCooldown();
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

	// 서버와 똑같은 함수·입력(시각, 조준, 시드 번호, 자세, 발사 모드)으로 이번 발을 계산한다.
	// 로컬 스프레이 상태가 한 발 전진하므로 ADS 카메라 반동과 크로스헤어 탄퍼짐도 다음 프레임에 바로 반영된다.
	const bool bAltMode = ResolveShotAltMode(Request);
	const bool bWasScoped = bAltMode && !EquippedWeapon->IsAltFireAttack();
	FValorShooterStance Stance = BuildShooterStance();
	Stance.bIsADS = bAltMode;

	// 쓸 탄 수는 예측 탄약 기준으로 이 발을 쏘기 "전에" 정한다(SimulateShot이 발 번호를 올리기 전).
	const int32 RoundsUsed = EquippedWeapon->GetRoundsForShot(bAltMode, EquippedWeapon->GetPredictedMagazineAmmo());
	const FValorComputedShotData ShotData = EquippedWeapon->SimulateShot(Request.ClientShotTime, Stance);
	EquippedWeapon->NotePredictedShotRounds(RoundsUsed);
	const int32 PelletCount = EquippedWeapon->GetPelletCountForShot(bAltMode, RoundsUsed);

	FVector TraceStart = FVector::ZeroVector;
	FRotator IgnoredViewRotation = FRotator::ZeroRotator;
	OwnerCharacter->GetWeaponViewPoint(TraceStart, IgnoredViewRotation);

	// 연출용 로컬 트레이스(피해 판정 아님). 서버와 같은 방향(같은 시드의 펠릿)이라 벽 탄흔 위치는 서버 결과와 거의 항상 일치한다.
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ValorPredictedShotTrace), false, OwnerCharacter);
	QueryParams.AddIgnoredActor(EquippedWeapon);

	FValorShotEffects ShotEffects;
	TraceShot(Request, ShotData, bAltMode, PelletCount, TraceStart, ShotEffects,
		[World, &QueryParams](const FVector& RayStart, const FVector& RayDirection, float MaxDistance, float /*DamageDistanceOffset*/, FValorShotImpact& OutImpact)
		{
			const FVector RayEnd = RayStart + (RayDirection * MaxDistance);
			FHitResult Hit;
			OutImpact.bBlockingHit = World->LineTraceSingleByChannel(Hit, RayStart, RayEnd, ECC_Visibility, QueryParams);
			OutImpact.bHitCharacter = OutImpact.bBlockingHit && Cast<AValorCharacter>(Hit.GetActor()) != nullptr;
			OutImpact.ImpactPoint = OutImpact.bBlockingHit ? FVector(Hit.ImpactPoint) : RayEnd;
			OutImpact.ImpactNormal = OutImpact.bBlockingHit ? FVector(Hit.ImpactNormal) : -RayDirection;
			return OutImpact.bBlockingHit;
		});

	PlayLocalShotPresentation(ShotEffects);

	if (bWasScoped)
	{
		HandleUnscopeAfterShot();
	}
}

bool UValorCombatComponent::ResolveShotAltMode(const FValorShotRequest& Request) const
{
	if (!EquippedWeapon)
	{
		return false;
	}

	// 우클릭이 공격인 총(클래식 산탄·버키 캐니스터)은 어느 버튼으로 쐈는지(입력)를 그대로 쓴다. 두 모드 모두 정당한 선택이라 믿어도 된다.
	// 우클릭이 조준인 총은 서버가 아는 조준 상태로 정한다(클라가 "조준 중이었다"고 주장해도 무시).
	return EquippedWeapon->IsAltFireAttack() ? Request.bAltFire : IsADSForGameplay();
}

void UValorCombatComponent::TraceShot(const FValorShotRequest& Request, const FValorComputedShotData& ShotData, bool bAltMode, int32 PelletCount,
	const FVector& TraceStart, FValorShotEffects& OutEffects,
	TFunctionRef<bool(const FVector& RayStart, const FVector& RayDirection, float MaxDistance, float DamageDistanceOffset, FValorShotImpact& OutImpact)> TraceRay) const
{
	OutEffects = FValorShotEffects();
	OutEffects.bAltMode = bAltMode;
	if (!EquippedWeapon)
	{
		return;
	}

	const float TraceDistance = EquippedWeapon->GetTraceDistance();
	const float AirBurstDistance = EquippedWeapon->GetAirBurstDistance();
	PelletCount = FMath::Max(PelletCount, 1);

	// 버키 우클릭: 캐니스터가 반동 방향(퍼짐 없음)으로 날아가 AirBurstDistance에서 터지고, 그 지점에서 펠릿이 퍼진다.
	// 터지기 전에 무언가에 맞으면 터지지 않고 그 자리에 펠릿 1알 피해만 준다(위키).
	if (bAltMode && EquippedWeapon->GetAltFireType() == EValorAltFireType::AirBurst && AirBurstDistance > 0.0f)
	{
		const FVector CanisterDirection = EquippedWeapon->ComputeRecoilDirection(Request.AimRotation, ShotData);
		FValorShotImpact CanisterImpact;
		if (TraceRay(TraceStart, CanisterDirection, AirBurstDistance, 0.0f, CanisterImpact))
		{
			OutEffects.Impacts.Add(CanisterImpact);
			return;
		}

		const FVector BurstOrigin = TraceStart + (CanisterDirection * AirBurstDistance);
		OutEffects.TracerOrigin = BurstOrigin;
		OutEffects.bHasTracerOrigin = true;
		for (int32 PelletIndex = 0; PelletIndex < PelletCount; ++PelletIndex)
		{
			FValorShotImpact& PelletImpact = OutEffects.Impacts.AddDefaulted_GetRef();
			TraceRay(BurstOrigin, EquippedWeapon->ComputePelletDirection(Request.AimRotation, ShotData, PelletIndex), FMath::Max(TraceDistance - AirBurstDistance, 0.0f), AirBurstDistance, PelletImpact);
		}
		return;
	}

	// 일반 탄(1알) 또는 산탄(N알): 반동은 같고 펠릿마다 탄퍼짐 위치만 다르다.
	for (int32 PelletIndex = 0; PelletIndex < PelletCount; ++PelletIndex)
	{
		FValorShotImpact& PelletImpact = OutEffects.Impacts.AddDefaulted_GetRef();
		TraceRay(TraceStart, EquippedWeapon->ComputePelletDirection(Request.AimRotation, ShotData, PelletIndex), TraceDistance, 0.0f, PelletImpact);
	}
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
	const bool bAltMode = ResolveShotAltMode(Request);
	const bool bWasScoped = bAltMode && !EquippedWeapon->IsAltFireAttack();
	FValorShooterStance Stance = BuildShooterStance();
	Stance.bIsADS = bAltMode;

	const FValorComputedShotData ShotData = EquippedWeapon->SimulateShot(Request.ClientShotTime, Stance);
	const int32 RoundsUsed = EquippedWeapon->ConsumeAmmoForShot(bAltMode);
	const int32 PelletCount = EquippedWeapon->GetPelletCountForShot(bAltMode, RoundsUsed);
	LastAcceptedShotTime = Request.ClientShotTime;

	// 탄 시작점은 서버가 아는 캐릭터 카메라 위치(클라가 보낸 위치를 믿지 않음), 방향은 검증된 조준 입력 + 서버 계산 반동/탄퍼짐.
	FVector TraceStart = FVector::ZeroVector;
	FRotator IgnoredViewRotation = FRotator::ZeroRotator;
	OwnerCharacter->GetWeaponViewPoint(TraceStart, IgnoredViewRotation);

	// 펠릿마다 발사 시각으로 되감아 판정하고(랙 보상), 한 대상에 맞은 펠릿 피해는 합산해 한 번에 적용한다.
	TMap<AValorCharacter*, float> DamageByTarget;
	const float ShotTime = Request.ClientShotTime;
	FValorShotEffects ShotEffects;
	TraceShot(Request, ShotData, bAltMode, PelletCount, TraceStart, ShotEffects,
		[this, ShotTime, &DamageByTarget](const FVector& RayStart, const FVector& RayDirection, float MaxDistance, float DamageDistanceOffset, FValorShotImpact& OutImpact)
		{
			FValorHitScanResult HitResult;
			PerformServerHitScan(RayStart, RayDirection, MaxDistance, ShotTime, HitResult);

			if (HitResult.HitCharacter)
			{
				// 피해 거리 = 사수로부터의 거리(버키 캐니스터 펠릿은 폭발 지점까지의 거리를 더한다).
				float Damage = EquippedWeapon->ComputeDamage(HitResult.TravelDistance + DamageDistanceOffset, HitResult.HitZone);
				if (HitResult.bPenetratedSurface)
				{
					Damage *= EquippedWeapon->GetPenetrationDamageMultiplier();
				}

				DamageByTarget.FindOrAdd(HitResult.HitCharacter) += Damage;
			}

			OutImpact.ImpactPoint = HitResult.ImpactPoint;
			OutImpact.ImpactNormal = HitResult.ImpactNormal;
			OutImpact.bBlockingHit = HitResult.bBlockingHit || HitResult.HitCharacter != nullptr;
			OutImpact.bHitCharacter = HitResult.HitCharacter != nullptr;
			return OutImpact.bBlockingHit;
		});

	for (const TPair<AValorCharacter*, float>& TargetDamage : DamageByTarget)
	{
		if (UValorAbilitySystemComponent* TargetASC = Cast<UValorAbilitySystemComponent>(TargetDamage.Key->GetAbilitySystemComponent()))
		{
			TargetASC->ApplyModToAttribute(UValorCombatAttributeSet::GetIncomingDamageAttribute(), EGameplayModOp::Additive, TargetDamage.Value);
		}
	}

	// 연출: 로컬 사수가 곧 서버인 리슨 호스트는 여기서 바로 재생하고, 나머지 클라는 멀티캐스트로 받는다.
	if (OwnerCharacter->IsLocallyControlled())
	{
		PlayLocalShotPresentation(ShotEffects);
	}

	MulticastSimulateFire(ShotEffects);

	// 오퍼레이터·마샬: 쏘면 조준이 풀린다(서버가 조준 상태를 확정한다. 다음 발은 다시 조준하기 전까지 비조준 정확도).
	if (bWasScoped)
	{
		HandleUnscopeAfterShot();
	}
}

bool UValorCombatComponent::ValidateShotRequest(FValorShotRequest& InOutRequest)
{
	UWorld* World = GetWorld();
	if (!World || !OwnerCharacter || !EquippedWeapon)
	{
		return false;
	}

	const double ServerNow = World->GetTimeSeconds();

	// 직전 승인 발이 정한 "다음 발까지 최소 간격"(점사 간격/점사 사이 대기/오딘 가속 반영). 클라도 같은 값으로 발사를 예약한다.
	const double FireInterval = FMath::Max(static_cast<double>(EquippedWeapon->GetNextShotCooldown()), 0.0);

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
	//     점사 안 속도(스팅어 18발/초)·가속 최고 속도(오딘 15.6)까지 허용해야 정상 점사가 거부되지 않는다.
	const double RefillPerSecond = static_cast<double>(EquippedWeapon->GetMaxFireRate()) * 1.1;
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

void UValorCombatComponent::PlayLocalShotPresentation(const FValorShotEffects& ShotEffects)
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

	EquippedWeapon->PlayFireEffects(ShotEffects);

	// 사수 본인 화면만 매 발 튀게 한다(발로란트의 사격 시 화면 흔들림). 예측 클라는 발사 입력 프레임에 바로 실행되므로
	// 서버 왕복을 기다리지 않고 킥이 즉시 나온다. 조준(컨트롤 회전)은 건드리지 않는 순수 연출이다.
	if (OwnerCharacter && OwnerCharacter->IsLocallyControlled())
	{
		if (UValorCameraComponent* CameraLogicComponent = OwnerCharacter->GetCameraLogicComponent())
		{
			CameraLogicComponent->AddFireKick(EquippedWeapon->GetCameraKickConfig(ShotEffects.bAltMode));
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

	// 조준할 수 없는 총(우클릭이 공격이거나 없는 총)은 조준 요청을 받아도 조준 상태가 되지 않는다(서버 검증).
	bIsADS = bNewADS && EquippedWeapon != nullptr && EquippedWeapon->GetMaxZoomLevel() > 0 && !bIsReloading;
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

	// 우클릭이 공격인 총(클래식·버키)은 마지막으로 쏜 모드의 반동 규칙으로 평가해야 카메라 복귀와 크로스헤어가 그 발과 맞는다.
	if (EquippedWeapon->IsAltFireAttack())
	{
		OutStance.bIsADS = EquippedWeapon->WasLastShotAltMode();
	}

	OutEvaluation = EquippedWeapon->EvaluateSpray(GetSynchronizedTime(), OutStance);
	return true;
}

int32 UValorCombatComponent::GetDisplayedMagazineAmmo() const
{
	return EquippedWeapon ? EquippedWeapon->GetPredictedMagazineAmmo() : 0;
}

int32 UValorCombatComponent::GetDisplayedReserveAmmo() const
{
	return EquippedWeapon ? EquippedWeapon->GetCurrentReserveAmmo() : 0;
}

FText UValorCombatComponent::GetEquippedWeaponDisplayName() const
{
	return EquippedWeapon ? EquippedWeapon->GetWeaponConfig().DisplayName : FText::GetEmpty();
}

int32 UValorCombatComponent::GetZoomLevel() const
{
	return IsADSForGameplay() ? FMath::Max(LocalZoomLevel, 1) : 0;
}

bool UValorCombatComponent::IsScopeOverlayActive() const
{
	return EquippedWeapon && EquippedWeapon->UsesScopeOverlay() && IsADSForGameplay();
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

	// 새 총을 먼저 만든 뒤에 교체한다: 스폰이 실패하면 들고 있던 총을 잃지 않는다.
	// 교체 시 들고 있던 총은 EquipWeapon 안에서 바닥에 떨어진다.
	if (AValorWeaponPickup* Pickup = FindPickupInView())
	{
		if (AValorWeaponBase* SpawnedWeapon = Pickup->SpawnWeaponForPickup(OwnerCharacter))
		{
			EquipWeapon(SpawnedWeapon);
		}
	}
}

void UValorCombatComponent::MulticastSimulateFire_Implementation(const FValorShotEffects& ShotEffects)
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
		EquippedWeapon->PlayFireEffects(ShotEffects);
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
	// 교체된 이전 총은 서버에서 파괴되므로, 파괴 복제가 이 OnRep보다 먼저 도착했을 수 있다(IsValid로 확인).
	if (IsValid(PreviousWeapon) && PreviousWeapon != EquippedWeapon)
	{
		PreviousWeapon->OnUnequipped();
	}

	// 서버는 교체할 때 조준을 해제한다. 소유 클라도 조준 의도·줌 단계를 지워 서버와 같은 상태(힙)로 예측한다.
	if (PreviousWeapon != EquippedWeapon && OwnerCharacter && OwnerCharacter->IsLocallyControlled())
	{
		bLocalADSIntent = false;
		LocalZoomLevel = 0;
		bLocalAltFireHeld = false;
		GetWorld()->GetTimerManager().ClearTimer(ReScopeTimerHandle);
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

	// 발로란트 규칙: 총을 든 채 다른 총을 주우면, 들고 있던 총은 탄약을 가진 채 바닥에 떨어지고 새 총으로 바뀐다.
	if (EquippedWeapon && EquippedWeapon != NewWeapon)
	{
		DropEquippedWeapon();
	}

	// 이전 총의 재장전 타이머가 새 총에 적용되지 않게 끊는다(재장전 중 교체 → 새 총이 즉시 채워지는 문제 방지).
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadTimerHandle);
	}

	// 교체하면 조준이 풀린다. 우클릭을 누르고 있어도 다시 눌러야 조준한다(어빌리티 종료 → SetADSStateFromAbility(false)).
	if (AbilitySystemComponent && ADSAbilityHandle.IsValid())
	{
		AbilitySystemComponent->CancelAbilityHandle(ADSAbilityHandle);
	}

	EquippedWeapon = NewWeapon;
	bIsReloading = false;
	bIsADS = false;
	LastAcceptedShotTime = -1000.0;
	ApplyEquippedWeaponAttachment();

	// 리슨 서버 호스트는 EquippedWeapon OnRep이 오지 않으므로 로컬 조준 상태를 여기서 정리한다.
	if (OwnerCharacter && OwnerCharacter->IsLocallyControlled())
	{
		bLocalADSIntent = false;
		LocalZoomLevel = 0;
		bLocalAltFireHeld = false;
		GetWorld()->GetTimerManager().ClearTimer(ReScopeTimerHandle);
		RefreshADSOnLocalClient();
	}
}

void UValorCombatComponent::HandleUnscopeAfterShot()
{
	if (!EquippedWeapon || !EquippedWeapon->ShouldUnscopeAfterShot() || !GetOwner())
	{
		return;
	}

	// 서버: 조준 어빌리티를 끝내 bIsADS를 false로 확정한다(복제). 다음 발은 다시 조준하기 전까지 비조준 정확도다.
	// 원격 클라가 곧바로 우클릭으로 재조준해도, 신뢰성 RPC는 같은 액터에서 순서가 보장되므로 "발사 → 해제 → 재조준" 순서가 유지된다.
	if (GetOwner()->HasAuthority() && AbilitySystemComponent && ADSAbilityHandle.IsValid())
	{
		AbilitySystemComponent->CancelAbilityHandle(ADSAbilityHandle);
	}

	// 로컬 사수(예측 클라/리슨 호스트): 서버 결과를 기다리지 않고 줌을 푼다. "자동 재조준" 설정이면 장전(발사 간격)이 끝나는 대로 되돌린다.
	if (OwnerCharacter && OwnerCharacter->IsLocallyControlled())
	{
		const int32 ZoomLevelBeforeShot = FMath::Max(LocalZoomLevel, 1);
		LocalZoomLevel = 0;
		bLocalADSIntent = false;
		RefreshADSOnLocalClient();

		if (bAutoReScopeAfterShot)
		{
			PendingReScopeZoomLevel = ZoomLevelBeforeShot;
			const float ReScopeDelay = FMath::Max(EquippedWeapon->GetNextShotCooldown(), 0.05f);
			GetWorld()->GetTimerManager().SetTimer(ReScopeTimerHandle, this, &UValorCombatComponent::ReScopeAfterShot, ReScopeDelay, false);
		}
	}
}

void UValorCombatComponent::ReScopeAfterShot()
{
	if (!EquippedWeapon || bIsReloading || LocalZoomLevel > 0)
	{
		return;
	}

	// 누르고 있는 동안 조준 방식이면 아직 우클릭을 누르고 있을 때만 다시 조준한다.
	const bool bToggle = EquippedWeapon->IsSniper() ? bToggleSniperZoom : bToggleADS;
	if (bToggle || bLocalADSButtonHeld)
	{
		SetLocalZoomLevel(PendingReScopeZoomLevel);
	}
}

void UValorCombatComponent::DropEquippedWeapon()
{
	UWorld* World = GetWorld();
	if (!World || !GetOwner() || !GetOwner()->HasAuthority() || !EquippedWeapon)
	{
		return;
	}

	AValorWeaponBase* WeaponToDrop = EquippedWeapon;
	EquippedWeapon = nullptr;

	// 바닥의 총은 별도 픽업 액터로 만들고(총 종류 + 탄약을 옮김), 손에 들고 있던 무기 액터는 파괴한다.
	// 픽업은 서버에서 스폰되어 복제되므로 모든 클라에 같은 위치로 보인다.
	UClass* PickupClass = DroppedWeaponPickupClass ? DroppedWeaponPickupClass.Get() : AValorWeaponPickup::StaticClass();
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AValorWeaponPickup* DroppedPickup = World->SpawnActor<AValorWeaponPickup>(PickupClass, ComputeDropTransform(), SpawnParameters))
	{
		DroppedPickup->InitializeFromDroppedWeapon(*WeaponToDrop);
	}
	else
	{
		UE_LOG(LogValorCombat, Warning, TEXT("총을 떨어뜨릴 픽업을 스폰하지 못했다: %s"), *GetNameSafe(WeaponToDrop));
	}

	WeaponToDrop->OnUnequipped();
	WeaponToDrop->Destroy();
}

FTransform UValorCombatComponent::ComputeDropTransform() const
{
	const UWorld* World = GetWorld();
	if (!World || !OwnerCharacter)
	{
		return GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	OwnerCharacter->GetWeaponViewPoint(ViewLocation, ViewRotation);

	// 바라보는 방향(수평)으로 내려놓는다. 위·아래를 보고 있어도 총은 바닥에 눕는다.
	const FRotator DropRotation(0.0f, ViewRotation.Yaw, 0.0f);
	const FVector Forward = DropRotation.Vector();

	const FVector CharacterCenter = OwnerCharacter->GetActorLocation();
	const float HalfHeight = OwnerCharacter->GetCapsuleComponent() ? OwnerCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.0f;
	const FVector Feet = CharacterCenter - FVector(0.0f, 0.0f, HalfHeight);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ValorWeaponDrop), false, OwnerCharacter);

	// 1) 캐릭터 중심 높이에서 앞으로. 벽이 가까우면 벽 앞에서 멈춘다(벽 너머로 총이 넘어가지 않게).
	float ForwardDistance = DropForwardDistance;
	FHitResult WallHit;
	if (World->LineTraceSingleByChannel(WallHit, CharacterCenter, CharacterCenter + Forward * DropForwardDistance, ECC_Visibility, QueryParams))
	{
		ForwardDistance = FMath::Max(WallHit.Distance - DropWallClearance, 0.0f);
	}

	// 2) 그 지점에서 아래로 바닥을 찾는다. 못 찾으면(허공 등) 캐릭터 발밑에 둔다.
	const FVector AboveDropPoint = CharacterCenter + Forward * ForwardDistance;
	FVector DropLocation = Feet;
	FHitResult GroundHit;
	if (World->LineTraceSingleByChannel(GroundHit, AboveDropPoint, AboveDropPoint - FVector(0.0f, 0.0f, DropGroundSearchDepth), ECC_Visibility, QueryParams))
	{
		DropLocation = GroundHit.ImpactPoint;
	}

	return FTransform(DropRotation, DropLocation);
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

	const bool bScoped = IsADSForGameplay() && EquippedWeapon;
	if (bScoped)
	{
		// 줌 단계별 FOV(오퍼레이터 1단 2.5배 / 2단 5배). 리슨 호스트도 로컬 줌 단계를 쓴다(서버 판정은 조준 여부만 본다).
		const float ZoomFieldOfView = EquippedWeapon->GetZoomFieldOfView(CameraLogicComponent->GetHipFireFOV(), FMath::Max(LocalZoomLevel, 1));
		CameraLogicComponent->SetADSState(true, ZoomFieldOfView, EquippedWeapon->GetADSInterpSpeed());
	}
	else
	{
		CameraLogicComponent->SetADSState(false, 0.0f, 0.0f);
	}

	// 조준경 화면(저격총)일 때는 1인칭 총을 숨긴다. 조준경 자체는 HUD가 그린다.
	if (EquippedWeapon)
	{
		EquippedWeapon->SetScopedViewHidden(bScoped && EquippedWeapon->UsesScopeOverlay());
	}
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
	UWorld* World = GetWorld();
	if (!World || !OwnerCharacter)
	{
		return nullptr;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	OwnerCharacter->GetWeaponViewPoint(ViewLocation, ViewRotation);
	const FVector ViewDirection = ViewRotation.Vector();

	// 픽업은 충돌이 없으므로(사격을 막지 않게) 물리 트레이스 대신 조준선과의 거리로 고른다.
	// 줍기 입력 때만 서버에서 한 번 도는 순회라 비용이 작다(맵에 놓인 픽업은 많아야 수십 개).
	struct FPickupCandidate
	{
		AValorWeaponPickup* Pickup;
		float DistanceFromAimLine;
	};

	TArray<FPickupCandidate, TInlineAllocator<8>> Candidates;
	for (TActorIterator<AValorWeaponPickup> It(World); It; ++It)
	{
		AValorWeaponPickup* Pickup = *It;
		float DistanceFromAimLine = 0.0f;
		if (IsValid(Pickup) && Pickup->IsPickupAvailable()
			&& AValorWeaponPickup::ComputeAimOffset(ViewLocation, ViewDirection, Pickup->GetActorLocation(), Pickup->GetInteractionRadius(), PickupInteractDistance, DistanceFromAimLine))
		{
			Candidates.Add({Pickup, DistanceFromAimLine});
		}
	}

	// 크로스헤어에 가장 가까운 총부터, 벽 너머가 아닌(시야가 닿는) 첫 픽업을 고른다.
	Candidates.Sort([](const FPickupCandidate& A, const FPickupCandidate& B) { return A.DistanceFromAimLine < B.DistanceFromAimLine; });
	for (const FPickupCandidate& Candidate : Candidates)
	{
		const FVector PickupLocation = Candidate.Pickup->GetActorLocation();
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ValorPickupLineOfSight), false, OwnerCharacter);
		QueryParams.AddIgnoredActor(Candidate.Pickup);

		// 바닥에 놓인 총은 중심이 바닥면에 붙어 있어 시선이 총 바로 앞 바닥에 닿을 수 있다.
		// 그래서 막힌 지점이 총의 줍기 반경 안이면 보이는 것으로 친다.
		FHitResult BlockingHit;
		const bool bBlocked = World->LineTraceSingleByChannel(BlockingHit, ViewLocation, PickupLocation, ECC_Visibility, QueryParams)
			&& FVector::Dist(BlockingHit.ImpactPoint, PickupLocation) > Candidate.Pickup->GetInteractionRadius();
		if (!bBlocked)
		{
			return Candidate.Pickup;
		}
	}

	return nullptr;
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

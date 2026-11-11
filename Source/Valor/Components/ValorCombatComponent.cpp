#include "ValorCombatComponent.h"

#include "AbilitySystem/Abilities/UGA_WeaponADS.h"
#include "AbilitySystem/Abilities/UGA_WeaponFire.h"
#include "AbilitySystem/Abilities/UGA_WeaponReload.h"
#include "AbilitySystem/Effects/ValorDamageGameplayEffect.h"
#include "AbilitySystem/ValorAbilitySystemComponent.h"
#include "AbilitySystem/ValorDamageTypes.h"
#include "AbilitySystem/ValorGameplayTags.h"
#include "Components/CapsuleComponent.h"
#include "Components/ValorCameraComponent.h"
#include "Components/ValorViewmodelComponent.h"
#include "Components/ValorCharacterMovementComponent.h"
#include "Components/ValorLagCompensationComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "ValorCharacter.h"
#include "Weapons/ValorMeleeWeapon.h"
#include "Weapons/ValorWeaponBase.h"
#include "World/ValorWeaponPickup.h"

DEFINE_LOG_CATEGORY_STATIC(LogValorCombat, Log, All);

namespace
{
	// 연사 타이머가 프레임 경계 때문에 늦게 불린 경우, 이 시간(초) 이내면 발사 시각을 "이상적인 연사 시각"으로 맞춘다.
	constexpr double MaxScheduleSlackSeconds = 0.05;

	// 발사 시각과 장착 완료 시각 비교의 허용 오차(초). 둘 다 float로 오가며 생기는 반올림 차이만 흡수한다(조작 여지 10ms).
	constexpr double EquipReadyToleranceSeconds = 0.01;

	// "장착 시간 없이 바로 쓸 수 있음"을 나타내는 장착 시작 시각(스폰 지급).
	constexpr float InstantEquipStartTime = -1000.0f;
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
	MeleeWeaponClass = AValorMeleeWeapon::StaticClass();
}

void UValorCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<AValorCharacter>(GetOwner());
	ShotRateTokens = ShotRateBurstCapacity;
	LocalEquipState = EquipState;

	// 칼은 모든 캐릭터가 항상 갖는다. 서버가 스폰하면 소유자 관련성으로 클라에 복제된다.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		EnsureMeleeWeapon();
	}

	RefreshActiveEquipment();
}

void UValorCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 시체가 사라지거나 플레이어가 나가 캐릭터가 파괴되면, 남아 있던 총·칼 액터도 같이 없앤다(소유자가 사라진 무기가 월드에 남지 않게).
	if (EndPlayReason == EEndPlayReason::Destroyed && GetOwner() && GetOwner()->HasAuthority())
	{
		DestroyInventory();
	}

	Super::EndPlay(EndPlayReason);
}

void UValorCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UValorCombatComponent, PrimaryWeapon);
	DOREPLIFETIME(UValorCombatComponent, SecondaryWeapon);
	DOREPLIFETIME(UValorCombatComponent, MeleeWeapon);
	DOREPLIFETIME(UValorCombatComponent, EquipState);
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
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled() || !OwnerCharacter->IsAlive())
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
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled() || !OwnerCharacter->IsAlive())
	{
		return;
	}

	// 칼을 들었거나 아직 꺼내는 중이면 재장전하지 않는다(서버도 같은 조건으로 거부한다).
	if (!EquippedWeapon || !IsEquipReady())
	{
		return;
	}

	ServerRequestReload();
}

void UValorCombatComponent::HandleADSInputPressed()
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled() || !OwnerCharacter->IsAlive())
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

	// 총을 다 꺼내기 전에는 조준하지 않는다(서버도 장착 완료 전 조준 요청은 받아들이지 않는다).
	if (!IsEquipReady())
	{
		return;
	}

	// 조준: 발로란트 기본처럼 토글(누를 때마다 조준 ↔ 해제, 오퍼레이터는 1단 → 2단 → 해제).
	// 설정에서 "누르고 있는 동안" 방식으로 바꾸면 누르는 동안만 1단으로 조준한다.
	GetWorld()->GetTimerManager().ClearTimer(ReScopeTimerHandle);
	if (UsesToggleZoom())
	{
		SetLocalZoomLevel(LocalZoomLevel >= EquippedWeapon->GetMaxZoomLevel() ? 0 : LocalZoomLevel + 1);
		return;
	}

	SetLocalZoomLevel(1);
}

bool UValorCombatComponent::UsesToggleZoom() const
{
	return EquippedWeapon && (EquippedWeapon->IsSniper() ? bToggleSniperZoom : bToggleADS);
}

void UValorCombatComponent::ExitToggledZoomForReload()
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled() || !bIsReloading || LocalZoomLevel <= 0 || !UsesToggleZoom())
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(ReScopeTimerHandle);
	SetLocalZoomLevel(0);
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

	if (!UsesToggleZoom())
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

	// 조준을 새로 시작한 시각(총을 올리기 시작한 시각). 불독·스팅어는 여기서 ADSFireDelay가 지나야 쏠 수 있다.
	if (bScoped && !bWasScoped)
	{
		LocalADSStartTime = GetSynchronizedTime();
	}

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
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled() || !OwnerCharacter->IsAlive())
	{
		return;
	}

	ServerInteractWithPickup();
}

void UValorCombatComponent::HandleEquipSlotInput(EValorWeaponSlot Slot)
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled() || !OwnerCharacter->IsAlive() || !GetOwner())
	{
		return;
	}

	// 빈 슬롯, 이미 든 슬롯은 무시한다(발로란트: 같은 키를 다시 눌러도 다시 꺼내지 않는다 = 장착 시간이 다시 걸리지 않는다).
	if (!CanEquipSlot(Slot) || GetEffectiveEquipState().ActiveSlot == Slot)
	{
		return;
	}

	const float RequestTime = static_cast<float>(GetSynchronizedTime());

	// 리슨 서버 호스트: 로컬이 곧 서버이므로 예측 없이 바로 처리한다.
	if (GetOwner()->HasAuthority())
	{
		ProcessEquipRequestAuthority(Slot, RequestTime);
		return;
	}

	// 원격 소유 클라: 서버 왕복을 기다리지 않고 바로 바꿔 든다(총 보이기/숨기기, 이동 속도, 장착 타이머가 즉시 반영).
	// 서버는 같은 요청 시각으로 장착 시간을 계산하므로, 클라가 "다 꺼냈다"고 보는 순간과 서버 판정이 일치한다.
	++LocalEquipRequestId;
	LocalEquipState.ActiveSlot = Slot;
	LocalEquipState.EquipStartTime = RequestTime;
	LocalEquipState.AckedRequestId = LocalEquipRequestId;
	RefreshActiveEquipment();

	ServerRequestEquipSlot(Slot, RequestTime, LocalEquipRequestId);
}

void UValorCombatComponent::HandleCycleWeaponInput(int32 Direction)
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	const EValorWeaponSlot CurrentSlot = GetEffectiveEquipState().ActiveSlot;
	const EValorWeaponSlot NextSlot = ValorInventory::CycleSlot(CurrentSlot, Direction, CanEquipSlot(EValorWeaponSlot::Primary), CanEquipSlot(EValorWeaponSlot::Secondary));
	if (NextSlot != CurrentSlot)
	{
		HandleEquipSlotInput(NextSlot);
	}
}

void UValorCombatComponent::HandleDropWeaponInput()
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled() || !OwnerCharacter->IsAlive() || !GetOwner())
	{
		return;
	}

	// 칼은 버릴 수 없다. 총을 들고 있을 때만 서버에 요청한다(서버가 자기 상태로 다시 확인한다).
	if (GetEffectiveEquipState().ActiveSlot == EValorWeaponSlot::Melee || !EquippedWeapon)
	{
		return;
	}

	ServerDropActiveWeapon();
}

void UValorCombatComponent::TryFireLocalShot()
{
	UWorld* World = GetWorld();
	if (!World || !OwnerCharacter || !GetOwner())
	{
		return;
	}

	World->GetTimerManager().ClearTimer(LocalFireTimerHandle);

	// 예약된 다음 발(연사·점사·탭 버퍼)이 사망 뒤에 불려도 쏘지 않는다. 서버도 State.Dead 태그로 따로 거부한다.
	if (!OwnerCharacter->IsAlive())
	{
		bFireShotBuffered = false;
		return;
	}

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

	const double Now = GetSynchronizedTime();

	// 총을 꺼내는 중: 방아쇠를 누르고 있으면 장착이 끝나는 순간 쏜다(떼면 HandleFireInputReleased가 예약을 지운다).
	// 짧게 누른 탭은 버퍼하지 않는다 — 장착 중 입력이 장착 직후 저절로 나가면 의도치 않은 발사가 된다.
	const double EquipReadyTime = GetEquipReadyTime();
	if (Now + KINDA_SMALL_NUMBER < EquipReadyTime)
	{
		if (bTriggerHeld)
		{
			World->GetTimerManager().SetTimer(LocalFireTimerHandle, this, &UValorCombatComponent::TryFireLocalShot, static_cast<float>(EquipReadyTime - Now), false);
		}

		return;
	}

	// 다음 발까지 최소 간격은 직전 발이 정한다(점사 간격/점사 사이 대기/오딘 가속). 서버도 같은 값으로 검증한다.
	double EarliestShotTime = LastLocalShotTime + EquippedWeapon->GetNextShotCooldown();

	// 4.07: 불독·스팅어는 조준(점사 모드)으로 총을 올리는 동안 발사 입력을 미룬다. 입력은 버리지 않고 다 올린 순간 쏜다.
	// 조준을 풀고 연사로 돌아갈 때는 지연이 없다(조준 시작 시각만 기준으로 삼는다).
	if (!bLocalFiringAltAttack && IsADSForGameplay() && EquippedWeapon->GetADSFireDelay() > 0.0f)
	{
		EarliestShotTime = FMath::Max(EarliestShotTime, LocalADSStartTime + EquippedWeapon->GetADSFireDelay());
	}

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

	// 연출용 예측 판정(피해 없음). 서버와 같은 방향(같은 시드의 펠릿)·같은 판정 함수를 쓰고, 되감기만 하지 않는다.
	// 예전에는 Visibility 채널 트레이스를 썼는데, 캐릭터 캡슐(Pawn)·메시(CharacterMesh)가 Visibility를 무시해서
	// 사수 화면에서는 탄이 적을 뚫고 뒤 바닥·벽에 탄흔을 남겼다(서버 피해는 정상).
	FValorShotEffects ShotEffects;
	TraceShot(Request, ShotData, bAltMode, PelletCount, TraceStart, ShotEffects,
		[this](const FVector& RayStart, const FVector& RayDirection, float MaxDistance, float /*DamageDistanceOffset*/, FValorShotImpact& OutImpact)
		{
			FValorHitScanResult HitResult;
			PerformHitScan(RayStart, RayDirection, MaxDistance, 0.0f, false, HitResult);
			OutImpact.ImpactPoint = HitResult.ImpactPoint;
			OutImpact.ImpactNormal = HitResult.ImpactNormal;
			OutImpact.bBlockingHit = HitResult.bBlockingHit || HitResult.HitCharacter != nullptr;
			OutImpact.bHitCharacter = HitResult.HitCharacter != nullptr;
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
	// 대표 부위는 가장 치명적인 펠릿(머리 > 몸 > 다리), 관통은 한 알이라도 벽을 뚫었으면 관통으로 본다(킬피드 표시용).
	TMap<AValorCharacter*, FValorPendingTargetDamage> DamageByTarget;
	const float ShotTime = Request.ClientShotTime;
	FValorShotEffects ShotEffects;
	TraceShot(Request, ShotData, bAltMode, PelletCount, TraceStart, ShotEffects,
		[this, ShotTime, &DamageByTarget](const FVector& RayStart, const FVector& RayDirection, float MaxDistance, float DamageDistanceOffset, FValorShotImpact& OutImpact)
		{
			FValorHitScanResult HitResult;
			PerformHitScan(RayStart, RayDirection, MaxDistance, ShotTime, true, HitResult);

			if (HitResult.HitCharacter)
			{
				// 피해 거리 = 사수로부터의 거리(버키 캐니스터 펠릿은 폭발 지점까지의 거리를 더한다).
				float Damage = EquippedWeapon->ComputeDamage(HitResult.TravelDistance + DamageDistanceOffset, HitResult.HitZone);
				if (HitResult.bPenetratedSurface)
				{
					Damage *= EquippedWeapon->GetPenetrationDamageMultiplier();
				}

				FValorPendingTargetDamage& Pending = DamageByTarget.FindOrAdd(HitResult.HitCharacter);
				Pending.Damage += Damage;
				Pending.HitZone = ValorDamage::MoreSevereZone(Pending.HitZone, HitResult.HitZone);
				Pending.bPenetrated |= HitResult.bPenetratedSurface;
			}

			OutImpact.ImpactPoint = HitResult.ImpactPoint;
			OutImpact.ImpactNormal = HitResult.ImpactNormal;
			OutImpact.bBlockingHit = HitResult.bBlockingHit || HitResult.HitCharacter != nullptr;
			OutImpact.bHitCharacter = HitResult.HitCharacter != nullptr;
			return OutImpact.bBlockingHit;
		});

	for (const TPair<AValorCharacter*, FValorPendingTargetDamage>& TargetDamage : DamageByTarget)
	{
		ApplyDamageToTarget(TargetDamage.Key, TargetDamage.Value, TraceStart);
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

void UValorCombatComponent::ApplyDamageToTarget(AValorCharacter* Target, const FValorPendingTargetDamage& PendingDamage, const FVector& ShotOrigin)
{
	if (!Target || !Target->IsAlive() || !AbilitySystemComponent || !EquippedWeapon || PendingDamage.Damage <= 0.0f)
	{
		return;
	}

	UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent();
	if (!TargetASC)
	{
		return;
	}

	// 피해는 사수의 ASC가 만든 GE 스펙으로 보낸다 → 대상 AttributeSet이 "누가(사수), 무엇으로(무기), 어디서(발사 위치)"를 안다.
	// Instigator = 사수 캐릭터, EffectCauser = 무기 액터(킬피드 무기 이름), Origin = 사수 눈 위치(피격 방향 표시).
	FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
	Context.AddInstigator(OwnerCharacter, EquippedWeapon);
	Context.AddSourceObject(EquippedWeapon);
	Context.AddOrigin(ShotOrigin);

	const FGameplayEffectSpecHandle SpecHandle = AbilitySystemComponent->MakeOutgoingSpec(UValorDamageGameplayEffect::StaticClass(), 1.0f, Context);
	if (!SpecHandle.IsValid())
	{
		return;
	}

	SpecHandle.Data->SetSetByCallerMagnitude(ValorGameplayTags::SetByCaller_Damage, PendingDamage.Damage);

	const FGameplayTag ZoneTag = ValorDamage::HitZoneToTag(PendingDamage.HitZone);
	if (ZoneTag.IsValid())
	{
		SpecHandle.Data->AddDynamicAssetTag(ZoneTag);
	}

	if (PendingDamage.bPenetrated)
	{
		SpecHandle.Data->AddDynamicAssetTag(ValorGameplayTags::Damage_Penetrated);
	}

	AbilitySystemComponent->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
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

	// (2-1) 장착 시간: 발사 시각이 장착 완료 전이면 거부한다. 장착 시작 시각은 클라가 주장한 교체 시각을 되감기 한도로 자른 값이라
	//       정상 클라는 자기 예측과 같은 완료 시각을 쓰고, 탄창 끝처럼 예측이 서버보다 살짝 앞선 경우만 여기에 걸린다.
	if (static_cast<double>(InOutRequest.ClientShotTime) + EquipReadyToleranceSeconds < GetEquipReadyTime())
	{
		RejectShot(TEXT("장착 완료 전 발사"), false);
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

	LastLocalShotEffects = ShotEffects;
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

		// 1인칭 총 자체의 킥(뒤로 밀림 + 총구 들림). 우클릭이 공격인 총(클래식·버키)의 우클릭은 조준이 아니므로 힙 킥을 쓴다.
		if (UValorViewmodelComponent* ViewmodelComponent = OwnerCharacter->GetViewmodelComponent())
		{
			const bool bWasAimedShot = ShotEffects.bAltMode && EquippedWeapon->GetMaxZoomLevel() > 0;
			ViewmodelComponent->AddFireKick(EquippedWeapon->GetViewmodelConfig(), bWasAimedShot);
		}
	}
}

void UValorCombatComponent::ExecuteServerReloadAbility()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !EquippedWeapon || bIsReloading || !EquippedWeapon->CanReload() || !IsEquipReady())
	{
		return;
	}

	bIsReloading = true;
	MulticastPlayReloadCue();
	GetWorld()->GetTimerManager().SetTimer(ReloadTimerHandle, this, &UValorCombatComponent::FinishReload, EquippedWeapon->GetReloadDuration(), false);

	// 리슨 서버 호스트는 OnRep_IsReloading이 오지 않으므로 여기서 토글 조준을 푼다(원격 클라는 OnRep에서 푼다).
	ExitToggledZoomForReload();
}

void UValorCombatComponent::SetADSStateFromAbility(bool bNewADS)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// 조준할 수 없는 총(우클릭이 공격이거나 없는 총)은 조준 요청을 받아도 조준 상태가 되지 않는다(서버 검증).
	// 장착이 끝나기 전 조준 요청도 받지 않는다(클라는 장착 중 우클릭을 보내지 않으므로 정상 플레이에는 영향이 없다).
	bIsADS = bNewADS && EquippedWeapon != nullptr && EquippedWeapon->GetMaxZoomLevel() > 0 && !bIsReloading && IsEquipReady();
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

	// 정확도의 "걷기/달리기/멈춤" 판정 기준은 지금 든 무기·조준 상태의 최고 속도다(Riot 측정: 팬텀 기준 걷기 61%, 데드존 25%).
	// 칼 기준 속도를 쓰면 라이플은 최고 속도로 달려도 "달리기"에 못 미쳐 이동 오차가 덜 붙는다.
	if (const UValorCharacterMovementComponent* MovementComponent = OwnerCharacter->GetValorMovement())
	{
		Stance.RunSpeed = MovementComponent->GetBaseRunSpeed() * GetEquipMoveSpeedMultiplier();
		Stance.WalkSpeed = Stance.RunSpeed * MovementComponent->GetWalkSpeedRatio();
		Stance.CrouchSpeed = Stance.RunSpeed * MovementComponent->GetCrouchSpeedRatio();
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
	if (!GetOwner() || !GetOwner()->HasAuthority() || !OwnerCharacter || !OwnerCharacter->IsAlive())
	{
		return;
	}

	// 새 총을 먼저 만든 뒤에 교체한다: 스폰이 실패하면 들고 있던 총을 잃지 않는다.
	// 같은 슬롯에 있던 총은 AddWeaponToInventory 안에서 바닥에 떨어지고, 주운 총을 바로 꺼내 든다(장착 시간 적용).
	if (AValorWeaponPickup* Pickup = FindPickupInView())
	{
		if (AValorWeaponBase* SpawnedWeapon = Pickup->SpawnWeaponForPickup(OwnerCharacter))
		{
			AddWeaponToInventory(SpawnedWeapon, true, false);
		}
	}
}

void UValorCombatComponent::ServerRequestEquipSlot_Implementation(EValorWeaponSlot Slot, float ClientRequestTime, uint8 RequestId)
{
	// 처리 결과와 상관없이 "이 요청까지 처리했다"를 먼저 기록한다. 거부해도 번호가 올라가야 클라가 예측을 버리고 서버 상태로 돌아온다.
	EquipState.AckedRequestId = RequestId;
	ProcessEquipRequestAuthority(Slot, ClientRequestTime);
}

void UValorCombatComponent::ServerDropActiveWeapon_Implementation()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !OwnerCharacter || !OwnerCharacter->IsAlive())
	{
		return;
	}

	if (EquipState.ActiveSlot != EValorWeaponSlot::Melee && GetWeaponInSlot(EquipState.ActiveSlot))
	{
		DropWeaponInSlot(EquipState.ActiveSlot, true);
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

void UValorCombatComponent::RefreshEquipmentViewAttachment()
{
	CacheOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	for (AValorWeaponBase* SlotWeapon : {PrimaryWeapon.Get(), SecondaryWeapon.Get()})
	{
		if (IsValid(SlotWeapon))
		{
			SlotWeapon->RefreshViewAttachment(OwnerCharacter);
		}
	}

	if (IsValid(MeleeWeapon))
	{
		MeleeWeapon->RefreshViewAttachment(OwnerCharacter);
	}
}

void UValorCombatComponent::OnRep_PrimaryWeapon(AValorWeaponBase* PreviousWeapon)
{
	CacheOwnerCharacter();

	// 교체된 이전 총은 서버에서 파괴되므로, 파괴 복제가 이 OnRep보다 먼저 도착했을 수 있다(IsValid로 확인).
	if (IsValid(PreviousWeapon) && PreviousWeapon != PrimaryWeapon && PreviousWeapon != SecondaryWeapon)
	{
		PreviousWeapon->OnUnequipped();
	}

	if (IsValid(PrimaryWeapon) && OwnerCharacter)
	{
		PrimaryWeapon->OnEquippedBy(OwnerCharacter);
	}

	RefreshActiveEquipment();
}

void UValorCombatComponent::OnRep_SecondaryWeapon(AValorWeaponBase* PreviousWeapon)
{
	CacheOwnerCharacter();

	if (IsValid(PreviousWeapon) && PreviousWeapon != PrimaryWeapon && PreviousWeapon != SecondaryWeapon)
	{
		PreviousWeapon->OnUnequipped();
	}

	if (IsValid(SecondaryWeapon) && OwnerCharacter)
	{
		SecondaryWeapon->OnEquippedBy(OwnerCharacter);
	}

	RefreshActiveEquipment();
}

void UValorCombatComponent::OnRep_MeleeWeapon()
{
	CacheOwnerCharacter();

	if (IsValid(MeleeWeapon) && OwnerCharacter)
	{
		MeleeWeapon->OnEquippedBy(OwnerCharacter);
	}

	RefreshActiveEquipment();
}

void UValorCombatComponent::OnRep_EquipState()
{
	CacheOwnerCharacter();

	// 교체를 예측하는 소유 클라: 서버가 내 마지막 요청까지 처리한 상태만 받아들인다. 그 전의 복제값은 아직 처리 안 된
	// 요청 이전의 낡은 상태라 받아들이면 방금 바꾼 무기가 잠깐 되돌아간다(서버가 처리하면 곧 새 상태가 온다).
	// 예측 중이 아니면(다른 플레이어, 아직 빙의 전) 사본을 그대로 맞춰 둔다 — 나중에 예측을 시작할 때 출발점이 서버 값이 되게.
	if (!IsPredictingOwner() || ValorInventory::IsRequestAcknowledged(EquipState.AckedRequestId, LocalEquipRequestId))
	{
		LocalEquipState = EquipState;
		LocalEquipState.AckedRequestId = LocalEquipRequestId;
	}

	RefreshActiveEquipment();
}

AValorWeaponBase* UValorCombatComponent::GetWeaponInSlot(EValorWeaponSlot Slot) const
{
	switch (Slot)
	{
	case EValorWeaponSlot::Primary:
		return IsValid(PrimaryWeapon) ? PrimaryWeapon.Get() : nullptr;
	case EValorWeaponSlot::Secondary:
		return IsValid(SecondaryWeapon) ? SecondaryWeapon.Get() : nullptr;
	case EValorWeaponSlot::Melee:
	default:
		return nullptr;
	}
}

bool UValorCombatComponent::CanEquipSlot(EValorWeaponSlot Slot) const
{
	// 칼은 항상 꺼낼 수 있다(칼 액터가 아직 복제되지 않았어도 슬롯은 존재한다).
	return Slot == EValorWeaponSlot::Melee || GetWeaponInSlot(Slot) != nullptr;
}

void UValorCombatComponent::SetSlotWeapon(EValorWeaponSlot Slot, AValorWeaponBase* Weapon)
{
	if (Slot == EValorWeaponSlot::Primary)
	{
		PrimaryWeapon = Weapon;
	}
	else if (Slot == EValorWeaponSlot::Secondary)
	{
		SecondaryWeapon = Weapon;
	}
}

void UValorCombatComponent::CacheOwnerCharacter()
{
	if (!OwnerCharacter)
	{
		OwnerCharacter = Cast<AValorCharacter>(GetOwner());
	}
}

bool UValorCombatComponent::IsPredictingOwner() const
{
	return OwnerCharacter && OwnerCharacter->IsLocallyControlled() && GetOwner() && !GetOwner()->HasAuthority();
}

const FValorEquipState& UValorCombatComponent::GetEffectiveEquipState() const
{
	return IsPredictingOwner() ? LocalEquipState : EquipState;
}

float UValorCombatComponent::GetEquipDurationForSlot(EValorWeaponSlot Slot) const
{
	if (Slot == EValorWeaponSlot::Melee)
	{
		if (IsValid(MeleeWeapon))
		{
			return MeleeWeapon->GetEquipTime();
		}

		const AValorMeleeWeapon* MeleeDefaults = MeleeWeaponClass ? MeleeWeaponClass->GetDefaultObject<AValorMeleeWeapon>() : nullptr;
		return MeleeDefaults ? MeleeDefaults->GetEquipTime() : 0.0f;
	}

	const AValorWeaponBase* SlotWeapon = GetWeaponInSlot(Slot);
	return SlotWeapon ? SlotWeapon->GetEquipTime() : 0.0f;
}

double UValorCombatComponent::GetEquipReadyTime() const
{
	const FValorEquipState& State = GetEffectiveEquipState();
	return static_cast<double>(State.EquipStartTime) + static_cast<double>(GetEquipDurationForSlot(State.ActiveSlot));
}

bool UValorCombatComponent::IsEquipReady() const
{
	return GetSynchronizedTime() + KINDA_SMALL_NUMBER >= GetEquipReadyTime();
}

float UValorCombatComponent::GetEquipProgress() const
{
	const FValorEquipState& State = GetEffectiveEquipState();
	const float Duration = GetEquipDurationForSlot(State.ActiveSlot);
	if (Duration <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}

	return FMath::Clamp(static_cast<float>((GetSynchronizedTime() - State.EquipStartTime) / Duration), 0.0f, 1.0f);
}

FText UValorCombatComponent::GetSlotDisplayName(EValorWeaponSlot Slot) const
{
	if (Slot == EValorWeaponSlot::Melee)
	{
		if (IsValid(MeleeWeapon))
		{
			return MeleeWeapon->GetDisplayName();
		}

		const AValorMeleeWeapon* MeleeDefaults = MeleeWeaponClass ? MeleeWeaponClass->GetDefaultObject<AValorMeleeWeapon>() : nullptr;
		return MeleeDefaults ? MeleeDefaults->GetDisplayName() : FText::GetEmpty();
	}

	const AValorWeaponBase* SlotWeapon = GetWeaponInSlot(Slot);
	return SlotWeapon ? SlotWeapon->GetWeaponConfig().DisplayName : FText::GetEmpty();
}

float UValorCombatComponent::GetEquipMoveSpeedMultiplier() const
{
	if (EquippedWeapon)
	{
		return EquippedWeapon->GetMoveSpeedMultiplier(IsADSForGameplay());
	}

	if (IsValid(MeleeWeapon))
	{
		return MeleeWeapon->GetMoveSpeedMultiplier();
	}

	// 칼 액터가 아직 복제되지 않았거나 빈 슬롯을 예측 중인 짧은 순간: 칼 기본 속도(배율 1).
	return 1.0f;
}

void UValorCombatComponent::RefreshActiveEquipment()
{
	const EValorWeaponSlot ActiveSlot = GetEffectiveEquipState().ActiveSlot;
	AValorWeaponBase* NewActiveWeapon = GetWeaponInSlot(ActiveSlot);

	// 손에 든 것만 보이고 나머지는 숨긴다. 발로란트 3인칭 모델도 집어넣은 무기를 몸에 그리지 않는다.
	// 숨김은 각 머신이 같은 장착 상태로 직접 계산한다(복제되는 액터 숨김 플래그를 쓰면 소유 클라의 교체 예측을 서버 값이 덮는다).
	for (AValorWeaponBase* SlotWeapon : {PrimaryWeapon.Get(), SecondaryWeapon.Get()})
	{
		if (IsValid(SlotWeapon))
		{
			SlotWeapon->SetHolstered(SlotWeapon != NewActiveWeapon);
		}
	}

	if (IsValid(MeleeWeapon))
	{
		MeleeWeapon->SetHolstered(ActiveSlot != EValorWeaponSlot::Melee);
	}

	AValorWeaponBase* PreviousWeapon = EquippedWeapon;
	const bool bActiveChanged = PreviousWeapon != NewActiveWeapon || ActiveSlot != LastAppliedActiveSlot;
	EquippedWeapon = NewActiveWeapon;
	LastAppliedActiveSlot = ActiveSlot;

	if (bActiveChanged)
	{
		HandleActiveEquipmentChanged(PreviousWeapon);
	}

	RefreshADSOnLocalClient();
}

void UValorCombatComponent::HandleActiveEquipmentChanged(AValorWeaponBase* PreviousWeapon)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 모든 머신: 집어넣은 총의 점사를 끊는다(서버와 예측 클라가 같은 순서 위치에서 끊으므로 다음 발 간격 계산이 일치한다).
	if (IsValid(PreviousWeapon) && PreviousWeapon != EquippedWeapon)
	{
		PreviousWeapon->CancelBurst();
	}

	// 서버: 재장전은 교체하면 취소된다(진행도도 사라진다). 조준 어빌리티를 끝내 bIsADS를 내린다.
	// 로컬 발사 예약보다 먼저 해야 리슨 호스트가 재장전 중 교체했을 때도 새 총 발사 예약이 막히지 않는다.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		World->GetTimerManager().ClearTimer(ReloadTimerHandle);
		bIsReloading = false;

		if (AbilitySystemComponent && ADSAbilityHandle.IsValid())
		{
			AbilitySystemComponent->CancelAbilityHandle(ADSAbilityHandle);
		}

		bIsADS = false;
		LastAcceptedShotTime = -1000.0;
	}

	// 로컬 사수(예측 클라/리슨 호스트): 옛 총의 발사 예약·조준·재조준 예약을 지운다. 서버도 교체 시 조준을 끝내므로 같은 상태(힙)다.
	if (OwnerCharacter && OwnerCharacter->IsLocallyControlled())
	{
		World->GetTimerManager().ClearTimer(LocalFireTimerHandle);
		World->GetTimerManager().ClearTimer(ReScopeTimerHandle);
		bFireShotBuffered = false;
		bLocalAltFireHeld = false;
		bLocalFiringAltAttack = false;
		bLocalADSIntent = false;
		LocalZoomLevel = 0;

		// 방아쇠를 누른 채 바꿨다면 새 총은 장착이 끝나는 순간부터 쏜다(TryFireLocalShot이 장착 완료 시각으로 예약한다).
		if (bLocalFireHeld && EquippedWeapon && OwnerCharacter->IsAlive())
		{
			TryFireLocalShot();
		}
	}
}

void UValorCombatComponent::ProcessEquipRequestAuthority(EValorWeaponSlot Slot, float ClientRequestTime)
{
	UWorld* World = GetWorld();
	if (!World || !GetOwner() || !GetOwner()->HasAuthority() || !OwnerCharacter || !OwnerCharacter->IsAlive())
	{
		return;
	}

	// 빈 슬롯 요청(클라 예측이 서버보다 앞선 경우 등)과 이미 든 슬롯은 바꾸지 않는다. 요청 번호는 이미 기록됐으므로
	// 클라는 다음 복제에서 서버 상태로 돌아온다.
	if (!CanEquipSlot(Slot) || EquipState.ActiveSlot == Slot)
	{
		return;
	}

	// 장착 시작 시각 = 클라가 누른 시각(발사 시각과 같은 동기화 시간축). 너무 먼 과거 주장은 되감기 한도로 잘라
	// "교체를 오래전에 눌렀다"고 속여 장착 시간을 줄이는 조작을 막는다.
	const float StartTime = ValorInventory::ClampEquipRequestTime(ClientRequestTime, World->GetTimeSeconds(), MaxShotRewindSeconds);
	SetActiveSlotAuthority(Slot, StartTime);
}

void UValorCombatComponent::SetActiveSlotAuthority(EValorWeaponSlot Slot, float StartTime)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	EquipState.ActiveSlot = Slot;
	EquipState.EquipStartTime = StartTime;
	RefreshActiveEquipment();
}

void UValorCombatComponent::AddWeaponToInventory(AValorWeaponBase* NewWeapon, bool bMakeActive, bool bInstantEquip)
{
	UWorld* World = GetWorld();
	if (!World || !GetOwner() || !GetOwner()->HasAuthority() || !NewWeapon || !OwnerCharacter)
	{
		return;
	}

	EnsureMeleeWeapon();

	// 발로란트 규칙: 같은 종류(주무기/권총)의 총을 주우면, 그 칸의 총은 탄약을 가진 채 바닥에 떨어지고 새 총으로 바뀐다.
	const EValorWeaponSlot Slot = NewWeapon->GetInventorySlot();
	AValorWeaponBase* ExistingWeapon = GetWeaponInSlot(Slot);
	if (ExistingWeapon && ExistingWeapon != NewWeapon)
	{
		DropWeaponInSlot(Slot, false);
	}

	SetSlotWeapon(Slot, NewWeapon);
	NewWeapon->OnEquippedBy(OwnerCharacter);

	if (bMakeActive)
	{
		SetActiveSlotAuthority(Slot, bInstantEquip ? InstantEquipStartTime : static_cast<float>(World->GetTimeSeconds()));
		return;
	}

	RefreshActiveEquipment();
}

void UValorCombatComponent::EnsureMeleeWeapon()
{
	UWorld* World = GetWorld();
	if (!World || !GetOwner() || !GetOwner()->HasAuthority() || IsValid(MeleeWeapon) || !MeleeWeaponClass)
	{
		return;
	}

	if (!OwnerCharacter)
	{
		OwnerCharacter = Cast<AValorCharacter>(GetOwner());
	}

	if (!OwnerCharacter)
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = OwnerCharacter;
	SpawnParameters.Instigator = OwnerCharacter;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	MeleeWeapon = World->SpawnActor<AValorMeleeWeapon>(MeleeWeaponClass, OwnerCharacter->GetActorTransform(), SpawnParameters);
	if (MeleeWeapon)
	{
		MeleeWeapon->OnEquippedBy(OwnerCharacter);
	}

	RefreshActiveEquipment();
}

void UValorCombatComponent::DestroyInventory()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	for (AValorWeaponBase* SlotWeapon : {PrimaryWeapon.Get(), SecondaryWeapon.Get()})
	{
		if (IsValid(SlotWeapon))
		{
			SlotWeapon->OnUnequipped();
			SlotWeapon->Destroy();
		}
	}

	if (IsValid(MeleeWeapon))
	{
		MeleeWeapon->Destroy();
	}

	PrimaryWeapon = nullptr;
	SecondaryWeapon = nullptr;
	MeleeWeapon = nullptr;
	EquippedWeapon = nullptr;
}

void UValorCombatComponent::OnRep_IsADS()
{
	RefreshADSOnLocalClient();
}

void UValorCombatComponent::OnRep_IsReloading()
{
	// 재장전 시작: 토글 조준은 풀린다(다시 우클릭해야 조준). 누르고 있는 방식이면 재장전 동안만 꺼졌다가 버튼을 유지하면 다시 조준된다.
	ExitToggledZoomForReload();
	RefreshADSOnLocalClient();
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
	if (UsesToggleZoom() || bLocalADSButtonHeld)
	{
		SetLocalZoomLevel(PendingReScopeZoomLevel);
	}
}

void UValorCombatComponent::DropWeaponInSlot(EValorWeaponSlot Slot, bool bSelectFallback)
{
	UWorld* World = GetWorld();
	AValorWeaponBase* WeaponToDrop = GetWeaponInSlot(Slot);
	if (!World || !GetOwner() || !GetOwner()->HasAuthority() || !WeaponToDrop)
	{
		return;
	}

	SetSlotWeapon(Slot, nullptr);

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

	// 든 총을 버렸다면 남은 가장 좋은 무기를 꺼낸다(장착 시간 적용). 서버가 먼저 바꾸는 상태라 클라는 복제로 따라온다.
	if (bSelectFallback && EquipState.ActiveSlot == Slot)
	{
		const EValorWeaponSlot FallbackSlot = ValorInventory::SelectFallbackSlot(CanEquipSlot(EValorWeaponSlot::Primary), CanEquipSlot(EValorWeaponSlot::Secondary));
		SetActiveSlotAuthority(FallbackSlot, static_cast<float>(World->GetTimeSeconds()));
		return;
	}

	RefreshActiveEquipment();
}

void UValorCombatComponent::HandleOwnerDeath()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 모든 머신 공통: 발사 루프·재조준 예약을 끊고 로컬 조준 의도를 지운다(소유 클라는 OnRep_IsDead에서 이 함수를 부른다).
	World->GetTimerManager().ClearTimer(LocalFireTimerHandle);
	World->GetTimerManager().ClearTimer(ReScopeTimerHandle);
	bLocalFireHeld = false;
	bLocalAltFireHeld = false;
	bLocalFiringAltAttack = false;
	bFireShotBuffered = false;
	bLocalADSButtonHeld = false;
	bLocalADSIntent = false;
	LocalZoomLevel = 0;

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		// 서버: 재장전을 취소하고(타이머가 죽은 뒤 탄을 채우지 않게), 가장 좋은 총 하나를 시체 옆에 떨어뜨린다.
		// 발로란트: 주무기가 있으면 주무기, 없으면 권총이 떨어지고, 나머지(권총·칼)는 시체와 함께 사라진다.
		// 조준 어빌리티는 캐릭터가 CancelAllAbilities로 이미 끝냈다 → SetADSStateFromAbility(false)로 bIsADS가 내려간다.
		World->GetTimerManager().ClearTimer(ReloadTimerHandle);
		bIsReloading = false;
		bIsADS = false;

		DropWeaponInSlot(PrimaryWeapon ? EValorWeaponSlot::Primary : EValorWeaponSlot::Secondary, false);
		DestroyInventory();
	}

	RefreshADSOnLocalClient();
}

void UValorCombatComponent::GiveWeapon(TSubclassOf<AValorWeaponBase> WeaponClass, bool bMakeActive, bool bInstantEquip)
{
	// 리슨/스탠드얼론의 첫 스폰은 월드 BeginPlay 전에 GameMode가 부를 수 있어, BeginPlay에서 채우는 OwnerCharacter를 여기서도 확보한다.
	if (!OwnerCharacter)
	{
		OwnerCharacter = Cast<AValorCharacter>(GetOwner());
	}

	UWorld* World = GetWorld();
	if (!World || !GetOwner() || !GetOwner()->HasAuthority() || !OwnerCharacter || !WeaponClass)
	{
		return;
	}

	// 줍기와 같은 규칙으로 스폰한다(Owner/Instigator = 캐릭터 → 소유 클라에만 의미 있는 예측 탄약 등이 같은 경로로 동작).
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = OwnerCharacter;
	SpawnParameters.Instigator = OwnerCharacter;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (AValorWeaponBase* NewWeapon = World->SpawnActor<AValorWeaponBase>(WeaponClass, OwnerCharacter->GetActorTransform(), SpawnParameters))
	{
		AddWeaponToInventory(NewWeapon, bMakeActive, bInstantEquip);
	}
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

void UValorCombatComponent::PerformHitScan(const FVector& TraceStart, const FVector& ShotDirection, float MaxDistance, float ClientShotTimestampSeconds, bool bRewindToShotTime, FValorHitScanResult& OutResult) const
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
		// 시체는 판정 대상이 아니다(탄을 막지도, 피해를 받지도 않는다). 사망은 서버가 확정하므로 서버 상태만 본다.
		if (!CandidateCharacter || CandidateCharacter == OwnerCharacter || !CandidateCharacter->IsAlive())
		{
			continue;
		}

		UValorLagCompensationComponent* LagCompComponent = CandidateCharacter->GetLagCompensationComponent();
		if (!LagCompComponent)
		{
			continue;
		}

		// 서버: 발사 시각의 히트박스 스냅샷으로 판정한다(되감기). 예측: 사수 화면에 지금 보이는 자세로 판정한다.
		FValorLagCompHitResult CandidateHit;
		const bool bCandidateHit = bRewindToShotTime
			? LagCompComponent->ConfirmHitAtTime(TraceStart, ShotDirection, MaxDistance, ClientShotTimestampSeconds, CandidateHit)
			: LagCompComponent->ConfirmHitAtCurrentPose(TraceStart, ShotDirection, MaxDistance, CandidateHit);
		if (!bCandidateHit)
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

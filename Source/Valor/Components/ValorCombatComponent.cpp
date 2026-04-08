#include "ValorCombatComponent.h"

#include "AbilitySystem/Abilities/UGA_WeaponADS.h"
#include "AbilitySystem/Abilities/UGA_WeaponFire.h"
#include "AbilitySystem/Abilities/UGA_WeaponReload.h"
#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystem/ValorAbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/ValorCameraComponent.h"
#include "Components/ValorLagCompensationComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "ValorCharacter.h"
#include "Weapons/ValorWeaponBase.h"
#include "World/ValorWeaponPickup.h"

UValorCombatComponent::UValorCombatComponent()
{
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
	RequestSingleFireShot();

	if (EquippedWeapon && EquippedWeapon->IsAutomatic())
	{
		GetWorld()->GetTimerManager().SetTimer(LocalAutomaticFireTimerHandle, this, &UValorCombatComponent::HandleLocalAutomaticFire, EquippedWeapon->GetShotInterval(), true, EquippedWeapon->GetShotInterval());
	}
}

void UValorCombatComponent::HandleFireInputReleased()
{
	bLocalFireHeld = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LocalAutomaticFireTimerHandle);
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

	if (EquippedWeapon)
	{
		if (UValorCameraComponent* CameraLogicComponent = OwnerCharacter->GetCameraLogicComponent())
		{
			CameraLogicComponent->SetADSState(true, EquippedWeapon->GetADSFieldOfView(), EquippedWeapon->GetADSInterpSpeed());
		}
	}

	ServerSetADSInput(true);
}

void UValorCombatComponent::HandleADSInputReleased()
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	if (UValorCameraComponent* CameraLogicComponent = OwnerCharacter->GetCameraLogicComponent())
	{
		CameraLogicComponent->SetADSState(false, 0.0f, 0.0f);
	}

	ServerSetADSInput(false);
}

void UValorCombatComponent::HandleInteractInputPressed()
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	ServerInteractWithPickup();
}

void UValorCombatComponent::ExecuteServerFireAbility()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !OwnerCharacter || !EquippedWeapon || bIsReloading)
	{
		return;
	}

	FVector TraceStart = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	OwnerCharacter->GetWeaponViewPoint(TraceStart, ViewRotation);

	const float MovementMaxSpeed = OwnerCharacter->GetCharacterMovement() ? FMath::Max(OwnerCharacter->GetCharacterMovement()->MaxWalkSpeed, 1.0f) : 1.0f;
	const float MovementAlpha = FMath::Clamp(OwnerCharacter->GetVelocity().Size2D() / MovementMaxSpeed, 0.0f, 1.0f);

	FValorComputedShotData ShotData;
	const bool bIsCurrentlyCrouched = OwnerCharacter->GetCharacterMovement() && OwnerCharacter->GetCharacterMovement()->IsCrouching();
	if (!EquippedWeapon->PrepareAndConsumeShot(GetWorld()->GetTimeSeconds(), bIsADS, MovementAlpha, OwnerCharacter->IsWalkInputActive(), bIsCurrentlyCrouched, ShotData))
	{
		return;
	}

	const FVector ShotDirection = EquippedWeapon->ApplySpreadToDirection(ViewRotation.Vector(), ShotData);

	AValorCharacter* HitCharacter = nullptr;
	FVector ImpactPoint = TraceStart + (ShotDirection * EquippedWeapon->GetTraceDistance());
	float TravelDistance = EquippedWeapon->GetTraceDistance();
	bool bPenetratedSurface = false;
	const bool bHitCharacter = PerformServerHitScan(TraceStart, ShotDirection, EquippedWeapon->GetTraceDistance(), PendingClientShotTimestampSeconds, HitCharacter, ImpactPoint, TravelDistance, bPenetratedSurface);

	if (bHitCharacter && HitCharacter)
	{
		if (UValorAbilitySystemComponent* TargetASC = Cast<UValorAbilitySystemComponent>(HitCharacter->GetAbilitySystemComponent()))
		{
			UValorLagCompensationComponent* TargetLagComp = HitCharacter->GetLagCompensationComponent();
			FValorLagCompHitResult ConfirmedHit;
			if (TargetLagComp && TargetLagComp->ConfirmHitAtTime(TraceStart, ShotDirection, EquippedWeapon->GetTraceDistance(), PendingClientShotTimestampSeconds, ConfirmedHit))
			{
				float Damage = EquippedWeapon->ComputeDamage(TravelDistance, ConfirmedHit.HitZone);
				if (bPenetratedSurface)
				{
					Damage *= EquippedWeapon->GetPenetrationDamageMultiplier();
				}

				TargetASC->ApplyModToAttribute(UValorCombatAttributeSet::GetIncomingDamageAttribute(), EGameplayModOp::Additive, Damage);
			}
		}
	}

	MulticastSimulateFire(ImpactPoint, ShotData.RecoilKick, bHitCharacter);
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

void UValorCombatComponent::ServerRequestFire_Implementation(float ClientShotTimestampSeconds)
{
	PendingClientShotTimestampSeconds = FMath::Min(ClientShotTimestampSeconds, GetWorld()->GetTimeSeconds());

	if (!AbilitySystemComponent || !FireAbilityHandle.IsValid() || bIsReloading)
	{
		return;
	}

	AbilitySystemComponent->TryActivateAbility(FireAbilityHandle);
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

void UValorCombatComponent::MulticastSimulateFire_Implementation(FVector_NetQuantize TraceEnd, FRotator RecoilKick, bool bDidHitCharacter)
{
	if (!OwnerCharacter || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 애니메이션 인스턴스가 발사 몽타주나 리코일 레이어 타이밍을 잡을 수 있도록 최근 발사 시각을 남긴다.
	LastFireSimulationWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : LastFireSimulationWorldTime;

	if (OwnerCharacter->IsLocallyControlled() && OwnerCharacter->GetController())
	{
		// 로컬 소유자만 즉시 카메라 킥을 받아 사격 감각을 유지한다.
		OwnerCharacter->AddControllerPitchInput(-RecoilKick.Pitch);
		OwnerCharacter->AddControllerYawInput(RecoilKick.Yaw);
	}

	(void)TraceEnd;
	(void)bDidHitCharacter;
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
	if (bIsReloading)
	{
		return;
	}

	RefreshADSOnLocalClient();
}

void UValorCombatComponent::RequestSingleFireShot()
{
	if (!EquippedWeapon || bIsReloading)
	{
		return;
	}

	float ShotTimestampSeconds = GetWorld()->GetTimeSeconds();
	if (const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr)
	{
		// 랙 보상은 서버 시간축 기준으로 비교해야 하므로, 로컬 월드 시간이 아니라 서버 동기화 시간을 보낸다.
		ShotTimestampSeconds = static_cast<float>(GameState->GetServerWorldTimeSeconds());
	}

	ServerRequestFire(ShotTimestampSeconds);
}

void UValorCombatComponent::HandleLocalAutomaticFire()
{
	if (!bLocalFireHeld)
	{
		return;
	}

	RequestSingleFireShot();
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

	if (UValorCameraComponent* CameraLogicComponent = OwnerCharacter->GetCameraLogicComponent())
	{
		if (bIsADS && EquippedWeapon && !bIsReloading)
		{
			CameraLogicComponent->SetADSState(true, EquippedWeapon->GetADSFieldOfView(), EquippedWeapon->GetADSInterpSpeed());
			return;
		}

		CameraLogicComponent->SetADSState(false, 0.0f, 0.0f);
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

bool UValorCombatComponent::PerformServerHitScan(const FVector& TraceStart, const FVector& ShotDirection, float MaxDistance, float ClientShotTimestampSeconds, AValorCharacter*& OutHitCharacter, FVector& OutImpactPoint, float& OutTravelDistance, bool& bOutPenetratedSurface) const
{
	if (!GetWorld() || !OwnerCharacter)
	{
		return false;
	}

	FVector TraceEnd = TraceStart + (ShotDirection * MaxDistance);
	float BlockingDistance = MaxDistance;
	bool bBlockedByWorld = false;

	FCollisionQueryParams WorldTraceParams(SCENE_QUERY_STAT(ValorWeaponWorldTrace), false, OwnerCharacter);
	for (TActorIterator<AValorCharacter> It(GetWorld()); It; ++It)
	{
		WorldTraceParams.AddIgnoredActor(*It);
	}

	FHitResult WorldBlockingHit;
	if (GetWorld()->LineTraceSingleByChannel(WorldBlockingHit, TraceStart, TraceEnd, ECC_Visibility, WorldTraceParams))
	{
		BlockingDistance = WorldBlockingHit.Distance;
		OutImpactPoint = WorldBlockingHit.ImpactPoint;
		bBlockedByWorld = true;
	}

	float BestDistance = TNumericLimits<float>::Max();
	AValorCharacter* BestCharacter = nullptr;
	FVector BestImpactPoint = TraceEnd;
	bool bBestPenetrated = false;

	for (TActorIterator<AValorCharacter> It(GetWorld()); It; ++It)
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
			BestImpactPoint = CandidateHit.ImpactPoint;
			bBestPenetrated = bCanPenetrate && !bPassesWithoutPenetration;
		}
	}

	if (!BestCharacter)
	{
		OutTravelDistance = bBlockedByWorld ? BlockingDistance : MaxDistance;
		bOutPenetratedSurface = false;
		return false;
	}

	OutHitCharacter = BestCharacter;
	OutImpactPoint = BestImpactPoint;
	OutTravelDistance = BestDistance;
	bOutPenetratedSurface = bBestPenetrated;
	return true;
}

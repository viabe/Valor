#include "ValorWeaponBase.h"

#include "Components/SceneComponent.h"
#include "Interfaces/ValorWeaponOwnerInterface.h"
#include "Net/UnrealNetwork.h"
#include "Components/SkeletalMeshComponent.h"
#include "ValorCharacter.h"

AValorWeaponBase::AValorWeaponBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bNetUseOwnerRelevancy = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(Root);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	InitializeFallbackConfig();
	CurrentMagazineAmmo = FallbackWeaponConfig.MagazineSize;
	CurrentReserveAmmo = FallbackWeaponConfig.MaxReserveAmmo;
}

void AValorWeaponBase::BeginPlay()
{
	Super::BeginPlay();

	const FValorWeaponConfig& Config = GetWeaponConfig();
	CurrentMagazineAmmo = FMath::Clamp(CurrentMagazineAmmo, 0, Config.MagazineSize);
	CurrentReserveAmmo = FMath::Clamp(CurrentReserveAmmo, 0, Config.MaxReserveAmmo);
	WeaponRandomSeed = GetUniqueID() * 31u + 17u;
}

void AValorWeaponBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValorWeaponBase, OwningCharacter);
	DOREPLIFETIME(AValorWeaponBase, CurrentMagazineAmmo);
	DOREPLIFETIME(AValorWeaponBase, CurrentReserveAmmo);
}

void AValorWeaponBase::OnEquippedBy(AValorCharacter* NewOwnerCharacter)
{
	OwningCharacter = NewOwnerCharacter;
	SetOwner(NewOwnerCharacter);
	SetInstigator(NewOwnerCharacter);

	if (!NewOwnerCharacter)
	{
		return;
	}

	if (IValorWeaponOwnerInterface* WeaponOwner = Cast<IValorWeaponOwnerInterface>(NewOwnerCharacter))
	{
		if (USceneComponent* AttachComponent = WeaponOwner->GetWeaponAttachComponent())
		{
			AttachToComponent(AttachComponent, FAttachmentTransformRules::SnapToTargetIncludingScale, WeaponOwner->GetWeaponAttachSocketName());
		}
	}
}

void AValorWeaponBase::OnUnequipped()
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	OwningCharacter = nullptr;
	SetOwner(nullptr);
	SetInstigator(nullptr);
}

const FValorWeaponConfig& AValorWeaponBase::GetWeaponConfig() const
{
	return WeaponDataAsset ? WeaponDataAsset->WeaponConfig : FallbackWeaponConfig;
}

bool AValorWeaponBase::CanFire(float ServerWorldTimeSeconds) const
{
	const FValorWeaponConfig& Config = GetWeaponConfig();
	if (CurrentMagazineAmmo <= 0)
	{
		return false;
	}

	return (ServerWorldTimeSeconds - LastServerFireWorldTime) + KINDA_SMALL_NUMBER >= Config.TimeBetweenShots;
}

bool AValorWeaponBase::PrepareAndConsumeShot(float ServerWorldTimeSeconds, bool bIsADS, float MovementAlpha, bool bIsWalking, bool bIsCrouched, FValorComputedShotData& OutShotData)
{
	if (!CanFire(ServerWorldTimeSeconds))
	{
		return false;
	}

	RefreshSprayState(ServerWorldTimeSeconds);

	const FValorWeaponConfig& Config = GetWeaponConfig();
	const int32 ShotIndex = CurrentSprayShotCount;

	float SpreadAngle = Config.BaseFirstShotSpreadDegrees + (Config.AdditionalSpreadPerShotDegrees * ShotIndex);
	SpreadAngle += Config.MovementSpreadDegrees * FMath::Clamp(MovementAlpha, 0.0f, 1.0f);

	if (bIsWalking)
	{
		SpreadAngle += Config.WalkingSpreadDegrees;
	}

	if (bIsCrouched)
	{
		SpreadAngle *= Config.CrouchSpreadMultiplier;
	}

	if (bIsADS)
	{
		SpreadAngle *= Config.ADSSpreadMultiplier;
	}

	SpreadAngle = FMath::Clamp(SpreadAngle, 0.0f, Config.MaxSpreadDegrees);

	FRotator RecoilKick = FRotator::ZeroRotator;
	if (Config.RecoilPattern.IsValidIndex(ShotIndex))
	{
		const FValorRecoilStep& RecoilStep = Config.RecoilPattern[ShotIndex];
		RecoilKick.Pitch = RecoilStep.PitchKick;
		RecoilKick.Yaw = RecoilStep.YawKick;
		SpreadAngle += RecoilStep.AdditionalSpread;
	}
	else if (Config.RecoilPattern.Num() > 0)
	{
		FRandomStream Stream(WeaponRandomSeed + ShotIndex * 13u);
		const FValorRecoilStep& LastDeterministicStep = Config.RecoilPattern.Last();
		RecoilKick.Pitch = LastDeterministicStep.PitchKick + Stream.FRandRange(0.0f, Config.RandomPitchDeviation);
		RecoilKick.Yaw = LastDeterministicStep.YawKick + Stream.FRandRange(-Config.RandomYawDeviation, Config.RandomYawDeviation);
	}

	if (bIsCrouched)
	{
		RecoilKick *= Config.CrouchRecoilMultiplier;
	}

	if (bIsADS)
	{
		RecoilKick *= Config.ADSRecoilMultiplier;
	}

	OutShotData.ShotIndex = ShotIndex;
	OutShotData.SpreadAngleDegrees = SpreadAngle;
	OutShotData.RecoilKick = RecoilKick;
	OutShotData.RandomSeed = WeaponRandomSeed + ShotIndex * 23u;

	LastServerFireWorldTime = ServerWorldTimeSeconds;
	CurrentSprayShotCount++;
	CurrentMagazineAmmo = FMath::Max(0, CurrentMagazineAmmo - 1);
	return true;
}

bool AValorWeaponBase::CanReload() const
{
	const FValorWeaponConfig& Config = GetWeaponConfig();
	return CurrentMagazineAmmo < Config.MagazineSize && CurrentReserveAmmo > 0;
}

void AValorWeaponBase::ReloadFromReserve()
{
	if (!CanReload())
	{
		return;
	}

	const FValorWeaponConfig& Config = GetWeaponConfig();
	const int32 AmmoNeeded = Config.MagazineSize - CurrentMagazineAmmo;
	const int32 AmmoToLoad = FMath::Min(AmmoNeeded, CurrentReserveAmmo);

	CurrentMagazineAmmo += AmmoToLoad;
	CurrentReserveAmmo -= AmmoToLoad;
}

float AValorWeaponBase::GetReloadDuration() const
{
	return GetWeaponConfig().ReloadDuration;
}

float AValorWeaponBase::GetShotInterval() const
{
	return GetWeaponConfig().TimeBetweenShots;
}

bool AValorWeaponBase::IsAutomatic() const
{
	return GetWeaponConfig().bAutomatic;
}

float AValorWeaponBase::GetADSFieldOfView() const
{
	return GetWeaponConfig().ADSFieldOfView;
}

float AValorWeaponBase::GetADSInterpSpeed() const
{
	return GetWeaponConfig().ADSInterpSpeed;
}

float AValorWeaponBase::GetTraceDistance() const
{
	return GetWeaponConfig().TraceDistanceCm;
}

EValorWeaponAnimationType AValorWeaponBase::GetWeaponAnimationType() const
{
	return GetWeaponConfig().AnimationType;
}

EValorWallPenetrationTier AValorWeaponBase::GetPenetrationTier() const
{
	return GetWeaponConfig().PenetrationTier;
}

float AValorWeaponBase::GetPenetrationDepth() const
{
	return GetWeaponConfig().PenetrationDepthCm;
}

float AValorWeaponBase::GetPenetrationDamageMultiplier() const
{
	return GetWeaponConfig().PenetrationDamageMultiplier;
}

FVector AValorWeaponBase::ApplySpreadToDirection(const FVector& AimDirection, const FValorComputedShotData& ShotData) const
{
	FRandomStream Stream(ShotData.RandomSeed);
	const float PitchOffset = Stream.FRandRange(-ShotData.SpreadAngleDegrees, ShotData.SpreadAngleDegrees);
	const float YawOffset = Stream.FRandRange(-ShotData.SpreadAngleDegrees, ShotData.SpreadAngleDegrees);
	return FRotator(PitchOffset, YawOffset, 0.0f).RotateVector(AimDirection).GetSafeNormal();
}

float AValorWeaponBase::ComputeDamage(float DistanceCm, EValorHitZone HitZone) const
{
	const TArray<FValorDamageRangeStep>& DamageRanges = GetWeaponConfig().DamageRanges;
	if (DamageRanges.Num() == 0)
	{
		return 0.0f;
	}

	const FValorDamageRangeStep* SelectedRange = &DamageRanges.Last();
	for (const FValorDamageRangeStep& RangeStep : DamageRanges)
	{
		if (DistanceCm <= RangeStep.MaxDistanceCm)
		{
			SelectedRange = &RangeStep;
			break;
		}
	}

	switch (HitZone)
	{
	case EValorHitZone::Head:
		return SelectedRange->HeadDamage;
	case EValorHitZone::Leg:
		return SelectedRange->LegDamage;
	case EValorHitZone::Body:
	default:
		return SelectedRange->BodyDamage;
	}
}

void AValorWeaponBase::InitializeFallbackConfig()
{
	FallbackWeaponConfig.DisplayName = FText::FromString(TEXT("Prototype Rifle"));
	FallbackWeaponConfig.RecoilPattern =
	{
		{0.8f, 0.00f, 0.00f},
		{0.95f, 0.02f, 0.03f},
		{1.10f, 0.08f, 0.06f},
		{1.25f, 0.12f, 0.09f},
		{1.35f, -0.10f, 0.12f},
		{1.45f, -0.18f, 0.15f},
		{1.55f, 0.22f, 0.18f}
	};

	FallbackWeaponConfig.DamageRanges =
	{
		{1500.0f, 160.0f, 40.0f, 34.0f},
		{3000.0f, 150.0f, 37.0f, 31.0f},
		{50000.0f, 140.0f, 34.0f, 28.0f}
	};
}

void AValorWeaponBase::RefreshSprayState(float CurrentWorldTimeSeconds)
{
	const FValorWeaponConfig& Config = GetWeaponConfig();
	if ((CurrentWorldTimeSeconds - LastServerFireWorldTime) > Config.SpreadRecoveryDelay)
	{
		CurrentSprayShotCount = 0;
	}
}

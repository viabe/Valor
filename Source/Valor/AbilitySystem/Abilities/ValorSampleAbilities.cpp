#include "AbilitySystem/Abilities/ValorSampleAbilities.h"

#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ValorCharacter.h"
#include "World/ValorAbilityEffects.h"
#include "World/ValorAbilityProjectile.h"

namespace ValorSampleAbilities
{
	FActorSpawnParameters MakeSpawnParameters(AValorCharacter* Instigator)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.Instigator = Instigator;
		return SpawnParameters;
	}
}

void UValorAbility_Throw::ActivateAgentAbility()
{
	AValorCharacter* Character = GetValorCharacter();
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!World || !Character->HasAuthority() || !ProjectileClass)
	{
		return;
	}

	// 서버가 아는 시선(눈 위치·컨트롤 회전)에서 앞으로 조금 띄워 던진다.
	FVector ViewLocation;
	FRotator ViewRotation;
	Character->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	const FVector Direction = ViewRotation.Vector();
	const FTransform SpawnTransform(ViewRotation, ViewLocation + (Direction * 50.0f));
	if (AValorAbilityProjectile* Projectile = World->SpawnActor<AValorAbilityProjectile>(ProjectileClass, SpawnTransform, ValorSampleAbilities::MakeSpawnParameters(Character)))
	{
		Projectile->Launch(Direction);
	}
}

UValorAbility_Launch::UValorAbility_Launch()
{
	// 이동 스킬은 소유 클라가 기다리지 않고 바로 움직여야 한다(서버도 같은 발사를 한다).
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UValorAbility_Launch::ActivateAgentAbility()
{
	AValorCharacter* Character = GetValorCharacter();
	if (!Character)
	{
		return;
	}

	const FVector Direction = FRotator(0.0f, Character->GetControlRotation().Yaw, 0.0f).Vector();
	Character->LaunchCharacter((Direction * ForwardSpeed) + FVector(0.0f, 0.0f, UpSpeed), ForwardSpeed > 0.0f, true);
}

UValorAbility_Updraft::UValorAbility_Updraft()
{
	ForwardSpeed = 0.0f;
	UpSpeed = 1100.0f;
}

UValorAbility_HealOverTime::UValorAbility_HealOverTime()
{
	EffectClass = AValorHealOverTime::StaticClass();
}

void UValorAbility_HealOverTime::ActivateAgentAbility()
{
	AValorCharacter* Character = GetValorCharacter();
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!World || !Character->HasAuthority() || !EffectClass)
	{
		return;
	}

	if (AValorHealOverTime* Heal = World->SpawnActor<AValorHealOverTime>(EffectClass, Character->GetActorTransform(), ValorSampleAbilities::MakeSpawnParameters(Character)))
	{
		Heal->SetTarget(Character);
		Heal->AttachToActor(Character, FAttachmentTransformRules::KeepWorldTransform);
	}
}

UValorAbility_DeployWall::UValorAbility_DeployWall()
{
	WallClass = AValorAbilityWall::StaticClass();
}

void UValorAbility_DeployWall::ActivateAgentAbility()
{
	AValorCharacter* Character = GetValorCharacter();
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!World || !Character->HasAuthority() || !WallClass)
	{
		return;
	}

	// 조준점(벽·바닥에 닿은 곳, 없으면 최대 거리)에서 바닥을 찾아 그 위에 세운다. 벽 면은 시선과 수직.
	FVector ViewLocation;
	FRotator ViewRotation;
	Character->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ValorWallAim), false, Character);
	for (TActorIterator<AValorCharacter> It(World); It; ++It)
	{
		QueryParams.AddIgnoredActor(*It);
	}
	FHitResult Hit;
	const FVector AimEnd = ViewLocation + (ViewRotation.Vector() * MaxRangeCm);
	FVector Point = World->LineTraceSingleByChannel(Hit, ViewLocation, AimEnd, ECC_Visibility, QueryParams) ? Hit.ImpactPoint - (ViewRotation.Vector() * 50.0f) : AimEnd;
	if (World->LineTraceSingleByChannel(Hit, Point + FVector(0.0f, 0.0f, 50.0f), Point - FVector(0.0f, 0.0f, 1000.0f), ECC_Visibility, QueryParams))
	{
		Point = Hit.ImpactPoint;
	}

	const AValorAbilityWall* WallDefaults = WallClass->GetDefaultObject<AValorAbilityWall>();
	const FRotator Facing(0.0f, ViewRotation.Yaw, 0.0f);
	World->SpawnActor<AValorAbilityWall>(WallClass, FTransform(Facing, Point + FVector(0.0f, 0.0f, WallDefaults->Size.Z * 0.5f)), ValorSampleAbilities::MakeSpawnParameters(Character));
}

void UValorAbility_Overdrive::ActivateAgentAbility()
{
	AValorCharacter* Character = GetValorCharacter();
	UAbilitySystemComponent* AbilitySystem = Character ? Character->GetAbilitySystemComponent() : nullptr;
	if (!AbilitySystem || !Character->HasAuthority())
	{
		return;
	}

	const float MaxHealth = AbilitySystem->GetNumericAttribute(UValorCombatAttributeSet::GetMaxHealthAttribute());
	AbilitySystem->SetNumericAttributeBase(UValorCombatAttributeSet::GetHealthAttribute(), MaxHealth);
	const float MaxShield = FMath::Max(ShieldAmount, AbilitySystem->GetNumericAttribute(UValorCombatAttributeSet::GetMaxShieldAttribute()));
	AbilitySystem->SetNumericAttributeBase(UValorCombatAttributeSet::GetMaxShieldAttribute(), MaxShield);
	AbilitySystem->SetNumericAttributeBase(UValorCombatAttributeSet::GetShieldAttribute(), MaxShield);
}

UValorAbility_ThrowSmoke::UValorAbility_ThrowSmoke()
{
	ProjectileClass = AValorProjectile_Smoke::StaticClass();
}

UValorAbility_ThrowFlash::UValorAbility_ThrowFlash()
{
	ProjectileClass = AValorProjectile_Flash::StaticClass();
}

UValorAbility_ThrowRecon::UValorAbility_ThrowRecon()
{
	ProjectileClass = AValorProjectile_Recon::StaticClass();
}

namespace ValorSampleAbilities
{
	void SetSlot(FValorAbilitySlotConfig& Slot, const TCHAR* Name, TSubclassOf<UValorAgentAbility> AbilityClass, int32 Cost, int32 MaxCharges, int32 FreeCharges, int32 UltimatePoints)
	{
		Slot.DisplayName = FText::FromString(Name);
		Slot.AbilityClass = AbilityClass;
		Slot.Cost = Cost;
		Slot.MaxCharges = MaxCharges;
		Slot.FreeChargesPerRound = FreeCharges;
		Slot.UltimatePointsRequired = UltimatePoints;
	}
}

UValorAgent_Drift::UValorAgent_Drift()
{
	DisplayName = FText::FromString(TEXT("Drift"));
	ValorSampleAbilities::SetSlot(Ability1, TEXT("Mend"), UValorAbility_HealOverTime::StaticClass(), 200, 1, 0, 0);
	ValorSampleAbilities::SetSlot(Ability2, TEXT("Smoke Canister"), UValorAbility_ThrowSmoke::StaticClass(), 100, 2, 0, 0);
	ValorSampleAbilities::SetSlot(Signature, TEXT("Dash"), UValorAbility_Dash::StaticClass(), 0, 1, 1, 0);
	ValorSampleAbilities::SetSlot(Ultimate, TEXT("Overdrive"), UValorAbility_Overdrive::StaticClass(), 0, 0, 0, 6);
}

UValorAgent_Flare::UValorAgent_Flare()
{
	DisplayName = FText::FromString(TEXT("Flare"));
	ValorSampleAbilities::SetSlot(Ability1, TEXT("Updraft"), UValorAbility_Updraft::StaticClass(), 150, 2, 0, 0);
	ValorSampleAbilities::SetSlot(Ability2, TEXT("Flash Charge"), UValorAbility_ThrowFlash::StaticClass(), 200, 2, 0, 0);
	ValorSampleAbilities::SetSlot(Signature, TEXT("Recon Dart"), UValorAbility_ThrowRecon::StaticClass(), 0, 1, 1, 0);
	ValorSampleAbilities::SetSlot(Ultimate, TEXT("Barrier"), UValorAbility_DeployWall::StaticClass(), 0, 0, 0, 6);
}

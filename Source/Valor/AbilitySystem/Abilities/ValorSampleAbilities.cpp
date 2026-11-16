#include "AbilitySystem/Abilities/ValorSampleAbilities.h"

#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ValorCharacter.h"
#include "World/ValorSmokeCloud.h"

void UValorAbility_Mend::ActivateAgentAbility()
{
	AValorCharacter* Character = GetValorCharacter();
	UAbilitySystemComponent* AbilitySystem = Character ? Character->GetAbilitySystemComponent() : nullptr;
	if (!AbilitySystem || !Character->HasAuthority())
	{
		return;
	}

	const float Health = AbilitySystem->GetNumericAttribute(UValorCombatAttributeSet::GetHealthAttribute());
	const float MaxHealth = AbilitySystem->GetNumericAttribute(UValorCombatAttributeSet::GetMaxHealthAttribute());
	AbilitySystem->SetNumericAttributeBase(UValorCombatAttributeSet::GetHealthAttribute(), FMath::Min(MaxHealth, Health + HealAmount));
}

void UValorAbility_SmokeCanister::ActivateAgentAbility()
{
	AValorCharacter* Character = GetValorCharacter();
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!World || !Character->HasAuthority())
	{
		return;
	}

	// 서버가 아는 시선(컨트롤 회전)으로 조준점을 정한다. 벽에 닿으면 벽 앞에, 아니면 최대 거리에.
	FVector ViewLocation;
	FRotator ViewRotation;
	Character->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	const FVector End = ViewLocation + (ViewRotation.Vector() * MaxRangeCm);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ValorSmokeAim), false, Character);
	for (TActorIterator<AValorCharacter> It(World); It; ++It)
	{
		QueryParams.AddIgnoredActor(*It);
	}
	FHitResult Hit;
	const FVector SmokeLocation = World->LineTraceSingleByChannel(Hit, ViewLocation, End, ECC_Visibility, QueryParams)
		? Hit.ImpactPoint + (Hit.ImpactNormal * 100.0f)
		: End;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.Instigator = Character;
	if (AValorSmokeCloud* Smoke = World->SpawnActor<AValorSmokeCloud>(AValorSmokeCloud::StaticClass(), FTransform(SmokeLocation), SpawnParameters))
	{
		Smoke->SetLifeSpan(SmokeDurationSeconds);
	}
}

UValorAbility_Dash::UValorAbility_Dash()
{
	// 이동 스킬은 소유 클라가 기다리지 않고 바로 튀어 나가야 한다(서버도 같은 발사를 한다).
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UValorAbility_Dash::ActivateAgentAbility()
{
	AValorCharacter* Character = GetValorCharacter();
	if (!Character)
	{
		return;
	}

	const FVector Direction = FRotator(0.0f, Character->GetControlRotation().Yaw, 0.0f).Vector();
	Character->LaunchCharacter((Direction * DashSpeed) + FVector(0.0f, 0.0f, 150.0f), true, true);
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

UValorAgent_Drift::UValorAgent_Drift()
{
	DisplayName = FText::FromString(TEXT("Drift"));

	Ability1.DisplayName = FText::FromString(TEXT("Mend"));
	Ability1.AbilityClass = UValorAbility_Mend::StaticClass();
	Ability1.Cost = 200;
	Ability1.MaxCharges = 1;

	Ability2.DisplayName = FText::FromString(TEXT("Smoke Canister"));
	Ability2.AbilityClass = UValorAbility_SmokeCanister::StaticClass();
	Ability2.Cost = 100;
	Ability2.MaxCharges = 2;

	Signature.DisplayName = FText::FromString(TEXT("Dash"));
	Signature.AbilityClass = UValorAbility_Dash::StaticClass();
	Signature.MaxCharges = 1;
	Signature.FreeChargesPerRound = 1;

	Ultimate.DisplayName = FText::FromString(TEXT("Overdrive"));
	Ultimate.AbilityClass = UValorAbility_Overdrive::StaticClass();
	Ultimate.UltimatePointsRequired = 6;
}

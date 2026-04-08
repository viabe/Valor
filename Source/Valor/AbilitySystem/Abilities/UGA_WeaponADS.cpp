#include "UGA_WeaponADS.h"

#include "Components/ValorCombatComponent.h"
#include "ValorCharacter.h"

UUGA_WeaponADS::UUGA_WeaponADS()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UUGA_WeaponADS::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (AValorCharacter* Character = Cast<AValorCharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UValorCombatComponent* CombatComponent = Character->GetCombatComponent())
		{
			CombatComponent->SetADSStateFromAbility(true);
		}
	}
}

void UUGA_WeaponADS::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (AValorCharacter* Character = Cast<AValorCharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UValorCombatComponent* CombatComponent = Character->GetCombatComponent())
		{
			CombatComponent->SetADSStateFromAbility(false);
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

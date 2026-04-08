#include "UGA_WeaponFire.h"

#include "Components/ValorCombatComponent.h"
#include "ValorCharacter.h"

UUGA_WeaponFire::UUGA_WeaponFire()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UUGA_WeaponFire::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (AValorCharacter* Character = Cast<AValorCharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UValorCombatComponent* CombatComponent = Character->GetCombatComponent())
		{
			CombatComponent->ExecuteServerFireAbility();
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

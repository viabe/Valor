#include "UGA_WeaponReload.h"

#include "Components/ValorCombatComponent.h"
#include "ValorCharacter.h"

UUGA_WeaponReload::UUGA_WeaponReload()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UUGA_WeaponReload::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (AValorCharacter* Character = Cast<AValorCharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UValorCombatComponent* CombatComponent = Character->GetCombatComponent())
		{
			CombatComponent->ExecuteServerReloadAbility();
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

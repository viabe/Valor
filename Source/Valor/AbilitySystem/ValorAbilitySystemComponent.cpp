#include "ValorAbilitySystemComponent.h"

#include "Abilities/GameplayAbility.h"

void UValorAbilitySystemComponent::GrantAbilityIfAbsent(TSubclassOf<UGameplayAbility> AbilityClass, int32 AbilityLevel, FGameplayAbilitySpecHandle& InOutHandle)
{
	if (!AbilityClass || !IsOwnerActorAuthoritative())
	{
		return;
	}

	if (InOutHandle.IsValid())
	{
		return;
	}

	InOutHandle = GiveAbility(FGameplayAbilitySpec(AbilityClass, AbilityLevel));
}

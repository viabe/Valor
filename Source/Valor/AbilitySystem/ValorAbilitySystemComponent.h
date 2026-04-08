#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "ValorAbilitySystemComponent.generated.h"

UCLASS()
class VALOR_API UValorAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	void GrantAbilityIfAbsent(TSubclassOf<UGameplayAbility> AbilityClass, int32 AbilityLevel, FGameplayAbilitySpecHandle& InOutHandle);
};

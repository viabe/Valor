#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "UGA_WeaponFire.generated.h"

UCLASS()
class VALOR_API UUGA_WeaponFire : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UUGA_WeaponFire();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "UGA_WeaponReload.generated.h"

UCLASS()
class VALOR_API UUGA_WeaponReload : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UUGA_WeaponReload();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

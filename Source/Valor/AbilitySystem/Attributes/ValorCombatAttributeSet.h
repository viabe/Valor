#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectExtension.h"
#include "ValorCombatAttributeSet.generated.h"

#define VALOR_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

UCLASS()
class VALOR_API UValorCombatAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UValorCombatAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Health, Category="Valor|Attributes")
	FGameplayAttributeData Health;
	VALOR_ATTRIBUTE_ACCESSORS(UValorCombatAttributeSet, Health);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxHealth, Category="Valor|Attributes")
	FGameplayAttributeData MaxHealth;
	VALOR_ATTRIBUTE_ACCESSORS(UValorCombatAttributeSet, MaxHealth);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Shield, Category="Valor|Attributes")
	FGameplayAttributeData Shield;
	VALOR_ATTRIBUTE_ACCESSORS(UValorCombatAttributeSet, Shield);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxShield, Category="Valor|Attributes")
	FGameplayAttributeData MaxShield;
	VALOR_ATTRIBUTE_ACCESSORS(UValorCombatAttributeSet, MaxShield);

	UPROPERTY(BlueprintReadOnly, Category="Valor|Attributes")
	FGameplayAttributeData IncomingDamage;
	VALOR_ATTRIBUTE_ACCESSORS(UValorCombatAttributeSet, IncomingDamage);

protected:
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_Shield(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxShield(const FGameplayAttributeData& OldValue);

private:
	void ClampVitalAttributes();
};

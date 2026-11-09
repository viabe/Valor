#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectExtension.h"
#include "AbilitySystem/ValorDamageTypes.h"
#include "ValorCombatAttributeSet.generated.h"

// 서버가 피해를 적용한 직후 알린다(서버 전용). 소유 캐릭터가 바인딩해 사망 처리와 피격/명중 알림을 보낸다.
DECLARE_MULTICAST_DELEGATE_OneParam(FValorDamageAppliedSignature, const FValorDamageEvent& /*DamageEvent*/);

#define VALOR_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * 체력·실드 속성.
 *
 * 피해 흐름(서버 전용): 피해 GE(UValorDamageGameplayEffect)가 메타 속성 IncomingDamage에 피해량을 더한다 →
 *   PostGameplayEffectExecute가 ValorDamage::Resolve로 실드 → 체력 순서로 나눠 적용한다 → OnDamageApplied로 사건을 알린다.
 * 왜 메타 속성인가: 피해를 Health에 직접 더하면 실드 흡수 규칙을 GE마다 반복해야 한다. 메타 속성 한 곳에서 나누면
 *   총·스킬 어떤 피해든 같은 규칙을 탄다(GAS 표준 패턴).
 * 네트워크: Health/Shield만 복제(모두에게 — 적 체력바·사망 판정에 필요). IncomingDamage는 서버 계산용이라 복제하지 않는다.
 * 트레이드오프: 사망 "사건"은 여기서 알리기만 하고 처리(태그·입력 차단·킬 크레딧)는 캐릭터와 GameMode가 한다 → 속성 클래스가 게임 규칙을 모른다.
 */
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

	// 서버 전용 알림. 피해가 0보다 컸던 경우에만 브로드캐스트한다(이미 죽은 대상에 들어온 피해도 bKilled=false로 알린다).
	FValorDamageAppliedSignature OnDamageApplied;

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

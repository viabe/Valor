#include "AbilitySystem/Effects/ValorDamageGameplayEffect.h"

#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystem/ValorGameplayTags.h"

UValorDamageGameplayEffect::UValorDamageGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FSetByCallerFloat DamageMagnitude;
	DamageMagnitude.DataTag = ValorGameplayTags::SetByCaller_Damage;

	FGameplayModifierInfo DamageModifier;
	DamageModifier.Attribute = UValorCombatAttributeSet::GetIncomingDamageAttribute();
	DamageModifier.ModifierOp = EGameplayModOp::Additive;
	DamageModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(DamageMagnitude);
	Modifiers.Add(DamageModifier);
}

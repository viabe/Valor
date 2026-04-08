#include "ValorCombatAttributeSet.h"

#include "Net/UnrealNetwork.h"

UValorCombatAttributeSet::UValorCombatAttributeSet()
{
	InitHealth(100.0f);
	InitMaxHealth(100.0f);
	InitShield(0.0f);
	InitMaxShield(0.0f);
	InitIncomingDamage(0.0f);
}

void UValorCombatAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UValorCombatAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UValorCombatAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UValorCombatAttributeSet, Shield, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UValorCombatAttributeSet, MaxShield, COND_None, REPNOTIFY_Always);
}

void UValorCombatAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetShieldAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxShield());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Max(0.0f, NewValue);
	}
	else if (Attribute == GetMaxShieldAttribute())
	{
		NewValue = FMath::Max(0.0f, NewValue);
	}
}

void UValorCombatAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
	{
		const float DamageToApply = FMath::Max(0.0f, GetIncomingDamage());
		SetIncomingDamage(0.0f);

		if (DamageToApply <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		float RemainingDamage = DamageToApply;
		if (GetShield() > 0.0f)
		{
			const float NewShield = FMath::Max(0.0f, GetShield() - RemainingDamage);
			RemainingDamage = FMath::Max(0.0f, RemainingDamage - GetShield());
			SetShield(NewShield);
		}

		if (RemainingDamage > 0.0f)
		{
			SetHealth(FMath::Clamp(GetHealth() - RemainingDamage, 0.0f, GetMaxHealth()));
		}

		ClampVitalAttributes();
		return;
	}

	ClampVitalAttributes();
}

void UValorCombatAttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UValorCombatAttributeSet, Health, OldValue);
}

void UValorCombatAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UValorCombatAttributeSet, MaxHealth, OldValue);
}

void UValorCombatAttributeSet::OnRep_Shield(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UValorCombatAttributeSet, Shield, OldValue);
}

void UValorCombatAttributeSet::OnRep_MaxShield(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UValorCombatAttributeSet, MaxShield, OldValue);
}

void UValorCombatAttributeSet::ClampVitalAttributes()
{
	SetMaxHealth(FMath::Max(0.0f, GetMaxHealth()));
	SetHealth(FMath::Clamp(GetHealth(), 0.0f, GetMaxHealth()));
	SetMaxShield(FMath::Max(0.0f, GetMaxShield()));
	SetShield(FMath::Clamp(GetShield(), 0.0f, GetMaxShield()));
}

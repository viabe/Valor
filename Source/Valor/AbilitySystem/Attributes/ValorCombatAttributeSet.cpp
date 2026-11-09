#include "ValorCombatAttributeSet.h"

#include "AbilitySystem/ValorGameplayTags.h"
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

		// 실드가 먼저 100% 흡수하고 남은 피해가 체력을 깎는다(규칙은 ValorDamage::Resolve 한 곳에만 있다).
		FValorDamageEvent DamageEvent;
		DamageEvent.RequestedDamage = DamageToApply;
		DamageEvent.Resolution = ValorDamage::Resolve(GetHealth(), GetShield(), DamageToApply);
		SetShield(DamageEvent.Resolution.NewShield);
		SetHealth(DamageEvent.Resolution.NewHealth);
		ClampVitalAttributes();

		// 사건 정보: 사수/무기/발사 위치는 EffectContext에서, 부위/관통은 스펙의 동적 태그에서 읽는다.
		const FGameplayEffectContextHandle& Context = Data.EffectSpec.GetEffectContext();
		DamageEvent.Instigator = Context.GetOriginalInstigator();
		DamageEvent.DamageCauser = Context.GetEffectCauser();
		DamageEvent.bHasOrigin = Context.HasOrigin();
		DamageEvent.Origin = Context.GetOrigin();

		const FGameplayTagContainer& SpecTags = Data.EffectSpec.GetDynamicAssetTags();
		DamageEvent.HitZone = ValorDamage::HitZoneFromTags(SpecTags);
		DamageEvent.bPenetrated = SpecTags.HasTagExact(ValorGameplayTags::Damage_Penetrated);

		OnDamageApplied.Broadcast(DamageEvent);
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

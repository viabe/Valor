#include "AbilitySystem/ValorDamageTypes.h"

#include "AbilitySystem/ValorGameplayTags.h"

namespace ValorDamage
{
	namespace
	{
		int32 ZoneSeverity(EValorHitZone Zone)
		{
			switch (Zone)
			{
			case EValorHitZone::Head:
				return 3;
			case EValorHitZone::Body:
				return 2;
			case EValorHitZone::Leg:
				return 1;
			default:
				return 0;
			}
		}
	}

	FValorDamageResolution Resolve(float Health, float Shield, float Damage)
	{
		FValorDamageResolution Result;
		Health = FMath::Max(Health, 0.0f);
		Shield = FMath::Max(Shield, 0.0f);
		Damage = FMath::Max(Damage, 0.0f);

		Result.ShieldDamage = FMath::Min(Shield, Damage);
		Result.NewShield = Shield - Result.ShieldDamage;

		const float RemainingDamage = Damage - Result.ShieldDamage;
		Result.HealthDamage = FMath::Min(Health, RemainingDamage);
		Result.NewHealth = Health - Result.HealthDamage;

		Result.bKilled = Health > 0.0f && Result.NewHealth <= 0.0f;
		return Result;
	}

	EValorHitZone MoreSevereZone(EValorHitZone A, EValorHitZone B)
	{
		return ZoneSeverity(A) >= ZoneSeverity(B) ? A : B;
	}

	FGameplayTag HitZoneToTag(EValorHitZone HitZone)
	{
		switch (HitZone)
		{
		case EValorHitZone::Head:
			return ValorGameplayTags::Damage_Zone_Head;
		case EValorHitZone::Body:
			return ValorGameplayTags::Damage_Zone_Body;
		case EValorHitZone::Leg:
			return ValorGameplayTags::Damage_Zone_Leg;
		default:
			return FGameplayTag();
		}
	}

	EValorHitZone HitZoneFromTags(const FGameplayTagContainer& Tags)
	{
		if (Tags.HasTagExact(ValorGameplayTags::Damage_Zone_Head))
		{
			return EValorHitZone::Head;
		}

		if (Tags.HasTagExact(ValorGameplayTags::Damage_Zone_Body))
		{
			return EValorHitZone::Body;
		}

		if (Tags.HasTagExact(ValorGameplayTags::Damage_Zone_Leg))
		{
			return EValorHitZone::Leg;
		}

		return EValorHitZone::None;
	}
}

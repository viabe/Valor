#include "AbilitySystem/ValorAgentTypes.h"

const FValorAbilitySlotConfig& UValorAgentDefinition::GetSlot(EValorAbilitySlot Slot) const
{
	switch (Slot)
	{
	case EValorAbilitySlot::Ability1: return Ability1;
	case EValorAbilitySlot::Ability2: return Ability2;
	case EValorAbilitySlot::Signature: return Signature;
	default: return Ultimate;
	}
}

namespace ValorAbilityRules
{
	int32 ChargesAtRoundStart(int32 CurrentCharges, int32 FreeChargesPerRound, int32 MaxCharges, bool bEconomyReset)
	{
		const int32 Kept = bEconomyReset ? 0 : CurrentCharges;
		return FMath::Clamp(FMath::Max(Kept, FreeChargesPerRound), 0, FMath::Max(MaxCharges, FreeChargesPerRound));
	}

	bool CanBuyCharge(int32 CurrentCharges, int32 MaxCharges, int32 Cost, int32 Credits)
	{
		return Cost > 0 && CurrentCharges < MaxCharges && Credits >= Cost;
	}

	int32 AddUltimatePoints(int32 Current, int32 Amount, int32 Required)
	{
		return Required > 0 ? FMath::Clamp(Current + Amount, 0, Required) : FMath::Max(0, Current + Amount);
	}

	const UClass* PickFallbackAgent(const TArray<const UClass*>& Roster, const TArray<const UClass*>& TakenByTeam)
	{
		for (const UClass* Agent : Roster)
		{
			if (Agent && !TakenByTeam.Contains(Agent))
			{
				return Agent;
			}
		}
		return Roster.Num() > 0 ? Roster[0] : nullptr;
	}

	float ComputeFlashSeconds(float AngleDegrees, bool bLineOfSight, float DistanceCm, float RadiusCm, float FullSeconds, float PartialSeconds)
	{
		if (!bLineOfSight || DistanceCm > RadiusCm)
		{
			return 0.0f;
		}
		if (AngleDegrees <= 60.0f)
		{
			return FullSeconds;
		}
		return AngleDegrees <= 110.0f ? PartialSeconds : 0.0f;
	}
}

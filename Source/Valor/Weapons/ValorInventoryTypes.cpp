#include "Weapons/ValorInventoryTypes.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

namespace ValorInventory
{
	EValorWeaponSlot SlotForCategory(EValorWeaponCategory Category)
	{
		return Category == EValorWeaponCategory::Sidearm ? EValorWeaponSlot::Secondary : EValorWeaponSlot::Primary;
	}

	bool IsRequestAcknowledged(uint8 AckedRequestId, uint8 LocalRequestId)
	{
		return static_cast<int8>(static_cast<uint8>(AckedRequestId - LocalRequestId)) >= 0;
	}

	EValorWeaponSlot SelectFallbackSlot(bool bHasPrimary, bool bHasSecondary)
	{
		if (bHasPrimary)
		{
			return EValorWeaponSlot::Primary;
		}

		return bHasSecondary ? EValorWeaponSlot::Secondary : EValorWeaponSlot::Melee;
	}

	EValorWeaponSlot CycleSlot(EValorWeaponSlot Current, int32 Direction, bool bHasPrimary, bool bHasSecondary)
	{
		static constexpr EValorWeaponSlot Order[] = {EValorWeaponSlot::Primary, EValorWeaponSlot::Secondary, EValorWeaponSlot::Melee};
		constexpr int32 SlotCount = UE_ARRAY_COUNT(Order);

		const auto IsAvailable = [bHasPrimary, bHasSecondary](EValorWeaponSlot Slot)
		{
			return Slot == EValorWeaponSlot::Melee
				|| (Slot == EValorWeaponSlot::Primary && bHasPrimary)
				|| (Slot == EValorWeaponSlot::Secondary && bHasSecondary);
		};

		const int32 Step = Direction >= 0 ? 1 : -1;
		const int32 CurrentIndex = static_cast<int32>(Current);
		for (int32 Offset = 1; Offset <= SlotCount; ++Offset)
		{
			const int32 Index = ((CurrentIndex + (Step * Offset)) % SlotCount + SlotCount) % SlotCount;
			if (IsAvailable(Order[Index]))
			{
				return Order[Index];
			}
		}

		return Current;
	}

	float ClampEquipRequestTime(float ClaimedTime, double ServerNow, float MaxRewindSeconds)
	{
		const double MinTime = ServerNow - FMath::Max(MaxRewindSeconds, 0.0f);
		return static_cast<float>(FMath::Clamp(static_cast<double>(ClaimedTime), MinTime, ServerNow));
	}

	void DisableHeldWeaponCollision(AActor& WeaponActor)
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(&WeaponActor);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			PrimitiveComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			PrimitiveComponent->SetGenerateOverlapEvents(false);
			PrimitiveComponent->CanCharacterStepUpOn = ECB_No;
		}
	}
}

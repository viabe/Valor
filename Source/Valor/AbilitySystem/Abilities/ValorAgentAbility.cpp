#include "AbilitySystem/Abilities/ValorAgentAbility.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/ValorGameplayTags.h"
#include "Player/ValorPlayerState.h"
#include "ValorCharacter.h"

UValorAgentAbility::UValorAgentAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	ActivationBlockedTags.AddTag(ValorGameplayTags::State_Dead);
}

EValorAbilitySlot UValorAgentAbility::GetSlotFromSpec(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const
{
	const UAbilitySystemComponent* AbilitySystem = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const FGameplayAbilitySpec* Spec = AbilitySystem ? AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
	const int32 SlotIndex = Spec ? Spec->InputID - InputIdBase : 0;
	return static_cast<EValorAbilitySlot>(FMath::Clamp(SlotIndex, 0, ValorAbilityRules::SlotCount - 1));
}

AValorCharacter* UValorAgentAbility::GetValorCharacter() const
{
	return Cast<AValorCharacter>(GetAvatarActorFromActorInfo());
}

AValorPlayerState* UValorAgentAbility::GetValorPlayerState(const FGameplayAbilityActorInfo* ActorInfo) const
{
	const APawn* Pawn = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
	return Pawn ? Pawn->GetPlayerState<AValorPlayerState>() : nullptr;
}

bool UValorAgentAbility::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	const AValorPlayerState* PlayerState = GetValorPlayerState(ActorInfo);
	const UValorAgentDefinition* Agent = PlayerState ? PlayerState->GetAgent() : nullptr;
	if (!Agent)
	{
		return false;
	}

	const EValorAbilitySlot Slot = GetSlotFromSpec(Handle, ActorInfo);
	const FValorAbilitySlotConfig& Config = Agent->GetSlot(Slot);
	if (Config.UltimatePointsRequired > 0)
	{
		return PlayerState->GetUltimatePoints() >= Config.UltimatePointsRequired;
	}
	return PlayerState->GetAbilityCharges(Slot) > 0;
}

void UValorAgentAbility::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	// 충전·포인트는 서버만 바꾼다(소유 클라 예측 중에는 복제로 따라온다).
	AValorPlayerState* PlayerState = GetValorPlayerState(ActorInfo);
	const UValorAgentDefinition* Agent = PlayerState ? PlayerState->GetAgent() : nullptr;
	if (!Agent || !PlayerState->HasAuthority())
	{
		return;
	}

	const EValorAbilitySlot Slot = GetSlotFromSpec(Handle, ActorInfo);
	if (Agent->GetSlot(Slot).UltimatePointsRequired > 0)
	{
		PlayerState->SetUltimatePoints(0);
	}
	else
	{
		PlayerState->SetAbilityCharges(Slot, PlayerState->GetAbilityCharges(Slot) - 1);
	}
}

void UValorAgentAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActivateAgentAbility();
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

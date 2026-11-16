#include "Player/ValorPlayerState.h"

#include "Net/UnrealNetwork.h"
#include "Weapons/ValorWeaponBase.h"

AValorPlayerState::AValorPlayerState()
{
	// 엔진 기본 PlayerState는 초당 1번만 복제한다(APlayerState: NetUpdateFrequency = 1). 크레딧·구매·요원 선택처럼 클릭에 바로 답해야 하는 값이
	// 여기 있어서, 클라 화면에는 최대 1초(평균 0.5초) 늦게 보였다. 값을 바꿀 때마다 ForceNetUpdate로 다음 네트워크 틱에 바로 보내고,
	// 그 밖의 엔진 값(이름·핑)은 초당 10번으로 둔다. 항상 높은 빈도(예: 100)로 두는 것보다 변할 때만 보내는 쪽이 플레이어 수만큼 싸다.
	NetUpdateFrequency = 10.0f;
}

void AValorPlayerState::NotifyReplicatedStateChanged()
{
	// 독립 실행(네트워크 없음)에서는 아무 일도 하지 않는다.
	ForceNetUpdate();
}

void AValorPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValorPlayerState, Kills);
	DOREPLIFETIME(AValorPlayerState, Deaths);
	DOREPLIFETIME(AValorPlayerState, HeadshotKills);
	DOREPLIFETIME(AValorPlayerState, Team);
	DOREPLIFETIME(AValorPlayerState, Credits);
	DOREPLIFETIME_CONDITION(AValorPlayerState, RoundPurchases, COND_OwnerOnly);
	DOREPLIFETIME(AValorPlayerState, AgentClass);
	DOREPLIFETIME(AValorPlayerState, AbilityCharges);
	DOREPLIFETIME(AValorPlayerState, UltimatePoints);
	DOREPLIFETIME(AValorPlayerState, bAgentLocked);
}

void AValorPlayerState::AddKill(bool bHeadshot)
{
	if (!HasAuthority())
	{
		return;
	}

	++Kills;
	if (bHeadshot)
	{
		++HeadshotKills;
	}
	NotifyReplicatedStateChanged();
}

void AValorPlayerState::AddDeath()
{
	if (!HasAuthority())
	{
		return;
	}

	++Deaths;
	NotifyReplicatedStateChanged();
}

void AValorPlayerState::ResetScore()
{
	if (!HasAuthority())
	{
		return;
	}

	Kills = 0;
	Deaths = 0;
	HeadshotKills = 0;
	NotifyReplicatedStateChanged();
}

void AValorPlayerState::SetTeam(EValorTeam NewTeam)
{
	if (HasAuthority())
	{
		Team = NewTeam;
		NotifyReplicatedStateChanged();
	}
}

void AValorPlayerState::SetCredits(int32 NewCredits)
{
	if (HasAuthority())
	{
		Credits = ValorMatchRules::ClampCredits(NewCredits);
		NotifyReplicatedStateChanged();
	}
}

void AValorPlayerState::AddCredits(int32 Amount)
{
	SetCredits(Credits + Amount);
}

bool AValorPlayerState::HasPurchasedWeaponThisRound(const UClass* WeaponClass) const
{
	return WeaponClass && RoundPurchases.ContainsByPredicate([WeaponClass](const FValorRoundPurchase& Purchase) { return Purchase.WeaponClass == WeaponClass; });
}

bool AValorPlayerState::HasPurchasedShieldThisRound(int32 ShieldIndex) const
{
	return ShieldIndex != INDEX_NONE && RoundPurchases.ContainsByPredicate([ShieldIndex](const FValorRoundPurchase& Purchase) { return Purchase.ShieldIndex == ShieldIndex; });
}

void AValorPlayerState::AddRoundPurchase(const FValorRoundPurchase& Purchase)
{
	if (HasAuthority())
	{
		RoundPurchases.Add(Purchase);
		NotifyReplicatedStateChanged();
	}
}

bool AValorPlayerState::TakeWeaponPurchase(const UClass* WeaponClass, FValorRoundPurchase& OutPurchase)
{
	const int32 Index = RoundPurchases.FindLastByPredicate([WeaponClass](const FValorRoundPurchase& Purchase) { return WeaponClass && Purchase.WeaponClass == WeaponClass; });
	if (!HasAuthority() || Index == INDEX_NONE)
	{
		return false;
	}

	OutPurchase = RoundPurchases[Index];
	RoundPurchases.RemoveAt(Index);
	NotifyReplicatedStateChanged();
	return true;
}

bool AValorPlayerState::TakeShieldPurchase(int32 ShieldIndex, FValorRoundPurchase& OutPurchase)
{
	const int32 Index = RoundPurchases.FindLastByPredicate([ShieldIndex](const FValorRoundPurchase& Purchase) { return ShieldIndex != INDEX_NONE && Purchase.ShieldIndex == ShieldIndex; });
	if (!HasAuthority() || Index == INDEX_NONE)
	{
		return false;
	}

	OutPurchase = RoundPurchases[Index];
	RoundPurchases.RemoveAt(Index);
	NotifyReplicatedStateChanged();
	return true;
}

void AValorPlayerState::ClearRoundPurchases()
{
	if (HasAuthority())
	{
		RoundPurchases.Reset();
		NotifyReplicatedStateChanged();
	}
}

bool AValorPlayerState::HasPurchasedAbilityThisRound(EValorAbilitySlot Slot) const
{
	const int32 SlotIndex = static_cast<int32>(Slot);
	return RoundPurchases.ContainsByPredicate([SlotIndex](const FValorRoundPurchase& Purchase) { return Purchase.AbilitySlot == SlotIndex; });
}

bool AValorPlayerState::TakeAbilityPurchase(EValorAbilitySlot Slot, FValorRoundPurchase& OutPurchase)
{
	const int32 SlotIndex = static_cast<int32>(Slot);
	const int32 Index = RoundPurchases.FindLastByPredicate([SlotIndex](const FValorRoundPurchase& Purchase) { return Purchase.AbilitySlot == SlotIndex; });
	if (!HasAuthority() || Index == INDEX_NONE)
	{
		return false;
	}

	OutPurchase = RoundPurchases[Index];
	RoundPurchases.RemoveAt(Index);
	NotifyReplicatedStateChanged();
	return true;
}

const UValorAgentDefinition* AValorPlayerState::GetAgent() const
{
	return AgentClass ? AgentClass->GetDefaultObject<UValorAgentDefinition>() : nullptr;
}

int32 AValorPlayerState::GetAbilityCharges(EValorAbilitySlot Slot) const
{
	const int32 SlotIndex = static_cast<int32>(Slot);
	return AbilityCharges.IsValidIndex(SlotIndex) ? AbilityCharges[SlotIndex] : 0;
}

void AValorPlayerState::SetAgentClass(TSubclassOf<UValorAgentDefinition> NewAgentClass)
{
	if (HasAuthority())
	{
		AgentClass = NewAgentClass;
		AbilityCharges.Init(0, ValorAbilityRules::SlotCount);
		NotifyReplicatedStateChanged();
	}
}

void AValorPlayerState::SetAbilityCharges(EValorAbilitySlot Slot, int32 Charges)
{
	const int32 SlotIndex = static_cast<int32>(Slot);
	if (!HasAuthority())
	{
		return;
	}
	if (AbilityCharges.Num() < ValorAbilityRules::SlotCount)
	{
		AbilityCharges.SetNumZeroed(ValorAbilityRules::SlotCount);
	}
	AbilityCharges[SlotIndex] = FMath::Max(0, Charges);
	NotifyReplicatedStateChanged();
}

void AValorPlayerState::SetUltimatePoints(int32 Points)
{
	if (HasAuthority())
	{
		UltimatePoints = FMath::Max(0, Points);
		NotifyReplicatedStateChanged();
	}
}

void AValorPlayerState::SetAgentLocked(bool bLocked)
{
	if (HasAuthority())
	{
		bAgentLocked = bLocked;
		NotifyReplicatedStateChanged();
	}
}

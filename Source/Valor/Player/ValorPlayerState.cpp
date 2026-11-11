#include "Player/ValorPlayerState.h"

#include "Net/UnrealNetwork.h"
#include "Weapons/ValorWeaponBase.h"

void AValorPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValorPlayerState, Kills);
	DOREPLIFETIME(AValorPlayerState, Deaths);
	DOREPLIFETIME(AValorPlayerState, HeadshotKills);
	DOREPLIFETIME(AValorPlayerState, Team);
	DOREPLIFETIME(AValorPlayerState, Credits);
	DOREPLIFETIME_CONDITION(AValorPlayerState, RoundPurchases, COND_OwnerOnly);
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
}

void AValorPlayerState::AddDeath()
{
	if (!HasAuthority())
	{
		return;
	}

	++Deaths;
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
}

void AValorPlayerState::SetTeam(EValorTeam NewTeam)
{
	if (HasAuthority())
	{
		Team = NewTeam;
	}
}

void AValorPlayerState::SetCredits(int32 NewCredits)
{
	if (HasAuthority())
	{
		Credits = ValorMatchRules::ClampCredits(NewCredits);
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
	return true;
}

void AValorPlayerState::ClearRoundPurchases()
{
	if (HasAuthority())
	{
		RoundPurchases.Reset();
	}
}

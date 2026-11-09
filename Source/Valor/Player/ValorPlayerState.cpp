#include "Player/ValorPlayerState.h"

#include "Net/UnrealNetwork.h"

void AValorPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValorPlayerState, Kills);
	DOREPLIFETIME(AValorPlayerState, Deaths);
	DOREPLIFETIME(AValorPlayerState, HeadshotKills);
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

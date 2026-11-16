#include "World/ValorAbilityActor.h"

#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

AValorAbilityActor::AValorAbilityActor()
{
	bReplicates = true;
}

void AValorAbilityActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValorAbilityActor, OwnerTeam);
}

void AValorAbilityActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (HasAuthority())
	{
		OwnerTeam = ValorTeams::GetTeamOf(GetInstigator());
		if (LifetimeSeconds > 0.0f)
		{
			SetLifeSpan(LifetimeSeconds);
		}
	}
}

void AValorAbilityActor::DestroyAll(UWorld* World)
{
	if (!World)
	{
		return;
	}

	TArray<AValorAbilityActor*> Actors;
	for (TActorIterator<AValorAbilityActor> It(World); It; ++It)
	{
		Actors.Add(*It);
	}
	for (AValorAbilityActor* Actor : Actors)
	{
		Actor->Destroy();
	}
}

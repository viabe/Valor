#include "World/ValorSpikeSite.h"

#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

AValorSpikeSite::AValorSpikeSite()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = false;

	Area = CreateDefaultSubobject<UBoxComponent>(TEXT("Area"));
	Area->SetBoxExtent(FVector(400.0f, 400.0f, 200.0f));
	Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Area->ShapeColor = FColor(255, 200, 40);
	Area->SetHiddenInGame(true);
	RootComponent = Area;
}

void AValorSpikeSite::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValorSpikeSite, SiteName);
}

void AValorSpikeSite::SetAreaExtent(const FVector& Extent)
{
	Area->SetBoxExtent(Extent);
}

bool AValorSpikeSite::ContainsLocation(const FVector& Location) const
{
	const FVector Local = Area->GetComponentTransform().InverseTransformPosition(Location);
	const FVector Extent = Area->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

AValorSpikeSite* AValorSpikeSite::FindSiteContaining(const UWorld* World, const FVector& Location)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AValorSpikeSite> It(World); It; ++It)
	{
		if (It->ContainsLocation(Location))
		{
			return *It;
		}
	}
	return nullptr;
}

#include "World/ValorSpawnBarrier.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "ValorGameState.h"

AValorSpawnBarrier::AValorSpawnBarrier()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;

	Wall = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Wall"));
	RootComponent = Wall;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Wall->SetStaticMesh(CubeMesh.Object);
	}

	// 캐릭터만 막는다(탄 판정은 캐릭터 판정 도형과 월드 트레이스 Visibility를 쓰므로 무시한다).
	Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Wall->SetCollisionObjectType(ECC_WorldStatic);
	Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
	Wall->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Wall->SetCanEverAffectNavigation(false);
	Wall->CastShadow = false;
}

void AValorSpawnBarrier::BeginPlay()
{
	Super::BeginPlay();

	// 경기 모드가 아니면(데스매치) 처음부터 없는 것으로 둔다.
	ApplyActive(false);
}

void AValorSpawnBarrier::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	const AValorGameState* ValorGameState = World ? World->GetGameState<AValorGameState>() : nullptr;
	const bool bShouldBeActive = ValorGameState && ValorGameState->IsBuyPhase();
	if (!bInitialized || bShouldBeActive != bActive)
	{
		ApplyActive(bShouldBeActive);
	}
}

void AValorSpawnBarrier::ApplyActive(bool bNewActive)
{
	bInitialized = true;
	bActive = bNewActive;
	SetActorEnableCollision(bNewActive);
	SetActorHiddenInGame(!bNewActive);
}

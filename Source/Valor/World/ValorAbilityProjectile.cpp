#include "World/ValorAbilityProjectile.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AValorAbilityProjectile::AValorAbilityProjectile()
{
	SetReplicatingMovement(true);
	LifetimeSeconds = 10.0f;

	// 벽·바닥에만 부딪히고 캐릭터·카메라·탄 판정은 통과한다.
	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(8.0f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeScale3D(FVector(0.16f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Mesh->SetStaticMesh(SphereMesh.Object);
	}

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->bAutoActivate = false;
	Movement->bRotationFollowsVelocity = true;
}

void AValorAbilityProjectile::BeginPlay()
{
	Super::BeginPlay();

	Movement->ProjectileGravityScale = GravityScale;
	Movement->bShouldBounce = bBounce && !bStickOnHit;
	Movement->Bounciness = Bounciness;
	Movement->Friction = 0.4f;
	Movement->BounceVelocityStopSimulatingThreshold = 60.0f;

	if (HasAuthority())
	{
		Movement->OnProjectileStop.AddDynamic(this, &AValorAbilityProjectile::HandleStop);
		Movement->OnProjectileBounce.AddDynamic(this, &AValorAbilityProjectile::HandleBounce);
	}
}

void AValorAbilityProjectile::Launch(const FVector& Direction)
{
	if (!HasAuthority())
	{
		return;
	}

	Movement->Velocity = Direction.GetSafeNormal() * InitialSpeed;
	Movement->Activate(true);
	if (FuseSeconds > 0.0f)
	{
		GetWorldTimerManager().SetTimer(FuseTimerHandle, this, &AValorAbilityProjectile::Detonate, FuseSeconds, false);
	}
}

void AValorAbilityProjectile::HandleBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
	// 붙는 투척물은 튕기지 않고 처음 면에서 멈춘다(HandleStop이 처리).
}

void AValorAbilityProjectile::HandleStop(const FHitResult& ImpactResult)
{
	if (bStickOnHit || bDetonateOnStop)
	{
		Detonate();
	}
}

void AValorAbilityProjectile::Detonate()
{
	if (!HasAuthority() || bDetonated)
	{
		return;
	}

	bDetonated = true;
	GetWorldTimerManager().ClearTimer(FuseTimerHandle);
	if (PayloadClass)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.Instigator = GetInstigator();
		GetWorld()->SpawnActor<AValorAbilityActor>(PayloadClass, FTransform(GetActorLocation() + FVector(0.0f, 0.0f, PayloadHeightOffset)), SpawnParameters);
	}
	Destroy();
}

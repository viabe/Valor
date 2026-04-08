#include "ValorWeaponPickup.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "ValorCharacter.h"
#include "Weapons/ValorWeaponBase.h"

AValorWeaponPickup::AValorWeaponPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	SetRootComponent(InteractionSphere);
	InteractionSphere->InitSphereRadius(120.0f);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	PickupStaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupStaticMesh"));
	PickupStaticMesh->SetupAttachment(InteractionSphere);
	PickupStaticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	PickupSkeletalMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PickupSkeletalMesh"));
	PickupSkeletalMesh->SetupAttachment(InteractionSphere);
	PickupSkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AValorWeaponPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValorWeaponPickup, bIsAvailable);
}

AValorWeaponBase* AValorWeaponPickup::SpawnWeaponForPickup(AValorCharacter* PickingCharacter)
{
	if (!HasAuthority() || !bIsAvailable)
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	UClass* SpawnClass = WeaponClass ? WeaponClass.Get() : AValorWeaponBase::StaticClass();
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = PickingCharacter;
	SpawnParameters.Instigator = PickingCharacter;

	AValorWeaponBase* SpawnedWeapon = World->SpawnActor<AValorWeaponBase>(SpawnClass, GetActorTransform(), SpawnParameters);
	if (!SpawnedWeapon)
	{
		return nullptr;
	}

	bIsAvailable = false;
	if (bDestroyOnPickup)
	{
		Destroy();
	}
	else
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
	}

	return SpawnedWeapon;
}

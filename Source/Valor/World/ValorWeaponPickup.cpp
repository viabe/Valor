#include "ValorWeaponPickup.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "ValorCharacter.h"
#include "Weapons/ValorWeaponBase.h"

AValorWeaponPickup::AValorWeaponPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// 구는 "조준으로 고를 수 있는 범위"만 나타내고 충돌은 끈다.
	// 예전처럼 Visibility 채널을 막으면, 같은 채널을 쓰는 사격 트레이스가 총 주변의 보이지 않는 구에 막혀 탄이 허공에 박힌다.
	// 줍기 대상은 전투 컴포넌트가 조준선과 이 반경으로 직접 고른다(ComputeAimOffset). 여러 총이 반경 안에 있으면
	// 크로스헤어에 가장 가까운 총이 뽑히므로, 메시 피벗이 총 끝에 있어도 잡히도록 반경은 넉넉하게(기존 값 그대로) 둔다.
	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	SetRootComponent(InteractionSphere);
	InteractionSphere->InitSphereRadius(120.0f);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);

	PickupStaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupStaticMesh"));
	PickupStaticMesh->SetupAttachment(InteractionSphere);
	PickupStaticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	PickupSkeletalMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PickupSkeletalMesh"));
	PickupSkeletalMesh->SetupAttachment(InteractionSphere);
	PickupSkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AValorWeaponPickup::BeginPlay()
{
	Super::BeginPlay();

	// 레벨에 배치했지만 표시 메시를 지정하지 않은 픽업도 무기 메시로 보이게 한다.
	RefreshDisplayMesh();
}

void AValorWeaponPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValorWeaponPickup, bIsAvailable);
	DOREPLIFETIME(AValorWeaponPickup, WeaponClass);
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
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AValorWeaponBase* SpawnedWeapon = World->SpawnActor<AValorWeaponBase>(SpawnClass, GetActorTransform(), SpawnParameters);
	if (!SpawnedWeapon)
	{
		return nullptr;
	}

	// 떨어뜨린 총이면 그때의 탄약으로 되돌린다. 스폰할 때 BeginPlay가 데이터 에셋 기본 탄약(가득)으로 채운 뒤 덮어쓴다.
	if (StoredMagazineAmmo != INDEX_NONE)
	{
		SpawnedWeapon->RestoreAmmoState(StoredMagazineAmmo, StoredReserveAmmo);
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

void AValorWeaponPickup::InitializeFromDroppedWeapon(const AValorWeaponBase& DroppedWeapon)
{
	if (!HasAuthority())
	{
		return;
	}

	WeaponClass = DroppedWeapon.GetClass();
	StoredMagazineAmmo = DroppedWeapon.GetCurrentMagazineAmmo();
	StoredReserveAmmo = DroppedWeapon.GetCurrentReserveAmmo();
	bIsAvailable = true;
	bDestroyOnPickup = true;

	// 서버(리슨 호스트 화면)는 OnRep이 오지 않으므로 직접 표시를 갱신한다.
	RefreshDisplayMesh();
}

float AValorWeaponPickup::GetInteractionRadius() const
{
	return InteractionSphere ? InteractionSphere->GetScaledSphereRadius() : 0.0f;
}

bool AValorWeaponPickup::ComputeAimOffset(const FVector& ViewLocation, const FVector& ViewDirection, const FVector& TargetLocation,
	float InteractionRadius, float MaxDistance, float& OutDistanceFromAimLine)
{
	// 조준선 위로 투영한 거리(앞쪽만, 최대 거리까지)와 조준선에서 벗어난 거리로 판정한다.
	const FVector ToTarget = TargetLocation - ViewLocation;
	const float DistanceAlongAim = FVector::DotProduct(ToTarget, ViewDirection);
	if (DistanceAlongAim <= 0.0f || DistanceAlongAim > MaxDistance)
	{
		return false;
	}

	OutDistanceFromAimLine = (ToTarget - ViewDirection * DistanceAlongAim).Size();
	return OutDistanceFromAimLine <= InteractionRadius;
}

void AValorWeaponPickup::OnRep_WeaponClass()
{
	RefreshDisplayMesh();
}

void AValorWeaponPickup::RefreshDisplayMesh()
{
	// 데디케이티드 서버는 화면이 없으므로 표시 메시를 채울 필요가 없다.
	if (GetNetMode() == NM_DedicatedServer || !PickupSkeletalMesh || !WeaponClass)
	{
		return;
	}

	const bool bHasAuthoredDisplay = !bDisplayMeshFromWeapon
		&& (PickupSkeletalMesh->GetSkeletalMeshAsset() || (PickupStaticMesh && PickupStaticMesh->GetStaticMesh()));
	if (bHasAuthoredDisplay)
	{
		return;
	}

	// 블루프린트 무기 클래스의 기본 객체에는 블루프린트에서 지정한 메시가 들어 있다.
	const AValorWeaponBase* WeaponDefaults = WeaponClass->GetDefaultObject<AValorWeaponBase>();
	const USkeletalMeshComponent* WeaponMeshTemplate = WeaponDefaults ? WeaponDefaults->GetWeaponMesh() : nullptr;
	if (!WeaponMeshTemplate || !WeaponMeshTemplate->GetSkeletalMeshAsset())
	{
		return;
	}

	PickupSkeletalMesh->SetSkeletalMeshAsset(WeaponMeshTemplate->GetSkeletalMeshAsset());
	PickupSkeletalMesh->SetRelativeLocationAndRotation(WeaponMeshDisplayOffset, WeaponMeshDisplayRotation);
	bDisplayMeshFromWeapon = true;
}

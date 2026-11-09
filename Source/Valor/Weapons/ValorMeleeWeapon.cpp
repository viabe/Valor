#include "Weapons/ValorMeleeWeapon.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Interfaces/ValorWeaponOwnerInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "ValorCharacter.h"
#include "Weapons/ValorInventoryTypes.h"

namespace
{
	// 칼 메시의 그립 소켓 이름. 총(데이터 에셋 GripSocketName)과 같은 규칙으로, 있으면 손 소켓에 맞춰 정렬한다.
	const FName MeleeGripSocketName(TEXT("GripSocket"));
}

AValorMeleeWeapon::AValorMeleeWeapon()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bNetUseOwnerRelevancy = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	MeleeMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MeleeMesh"));
	MeleeMesh->SetupAttachment(Root);
	MeleeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 기본 칼 메시(Araxys 근접 무기). C++ 전용 프로젝트라 블루프린트 대신 기본값으로 연결한다. 없으면 메시 없이 동작한다.
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> DefaultMeleeMesh(TEXT("/Game/Asset/GUN/araxys/EQ_Melee_Antares_Lv2_Skelmesh_mo"));
	if (DefaultMeleeMesh.Succeeded())
	{
		MeleeMesh->SetSkeletalMesh(DefaultMeleeMesh.Object);
	}

	DisplayName = NSLOCTEXT("Valor", "MeleeDisplayName", "Knife");
}

void AValorMeleeWeapon::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	ValorInventory::DisableHeldWeaponCollision(*this);
}

void AValorMeleeWeapon::OnEquippedBy(AValorCharacter* NewOwnerCharacter)
{
	if (HasAuthority())
	{
		SetOwner(NewOwnerCharacter);
		SetInstigator(NewOwnerCharacter);
	}

	IValorWeaponOwnerInterface* WeaponOwner = Cast<IValorWeaponOwnerInterface>(NewOwnerCharacter);
	USceneComponent* AttachComponent = WeaponOwner ? WeaponOwner->GetWeaponAttachComponent() : nullptr;
	if (!AttachComponent)
	{
		return;
	}

	if (GetAttachParentActor() != NewOwnerCharacter)
	{
		AttachToComponent(AttachComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, WeaponOwner->GetWeaponAttachSocketName());
		RefreshMeshAlignment();
	}
}

void AValorMeleeWeapon::SetHolstered(bool bNewHolstered)
{
	// 총과 같이 루트부터 자식 전체에 적용한다(BP에서 추가한 부품도 함께 숨긴다).
	if (Root && Root->IsVisible() == bNewHolstered)
	{
		Root->SetVisibility(!bNewHolstered, true);
	}
}

void AValorMeleeWeapon::RefreshMeshAlignment()
{
	if (!MeleeMesh)
	{
		return;
	}

	MeleeMesh->SetRelativeTransform(FTransform::Identity);
	if (!MeleeMesh->DoesSocketExist(MeleeGripSocketName))
	{
		return;
	}

	// 총과 같은 방식: 그립 소켓이 손 소켓 위치에 오도록 메시를 역변환한다.
	const FTransform GripSocketTransform = MeleeMesh->GetSocketTransform(MeleeGripSocketName, RTS_Component);
	const FQuat MeshRelativeRotation = GripSocketTransform.GetRotation().Inverse();
	MeleeMesh->SetRelativeLocationAndRotation(MeshRelativeRotation.RotateVector(-GripSocketTransform.GetLocation()), MeshRelativeRotation);
}

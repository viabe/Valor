#include "Weapons/ValorMeleeWeapon.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Interfaces/ValorWeaponOwnerInterface.h"
#include "Net/UnrealNetwork.h"
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

	// 기본 칼 리그: 길이 방향 = 뼈 +Y, 날 높이 = 뼈 Z. 행 = 총 규칙 축(X 왼쪽, Y 아래, Z 앞)이 칼 뼈 공간에서 가리키는 방향.
	GripRotationOffset = FMatrix(FVector(-1.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f), FVector(0.0f, 1.0f, 0.0f), FVector::ZeroVector).Rotator();

	// 1인칭: 오른쪽 아래에서 날 끝이 화면 안쪽 위를 향하게 든다. 칼은 쏘지 않으므로 킥이 없다.
	Viewmodel.HipLocation = FVector(55.0f, 26.0f, -30.0f);
	Viewmodel.HipRotation = FRotator(30.0f, -30.0f, -45.0f);
	Viewmodel.KickBackCm = 0.0f;
	Viewmodel.KickPitchDegrees = 0.0f;
	Viewmodel.KickYawDegrees = 0.0f;
}

void AValorMeleeWeapon::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	ValorInventory::DisableHeldWeaponCollision(*this);
}

void AValorMeleeWeapon::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// 칼도 머신마다 다른 곳(1인칭 피벗 / 3인칭 손)에 붙는다. 서버의 부착을 복제하면 클라의 로컬 부착을 덮어쓴다.
	DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(AActor, AttachmentReplication, false);
}

void AValorMeleeWeapon::OnEquippedBy(AValorCharacter* NewOwnerCharacter)
{
	if (HasAuthority())
	{
		SetOwner(NewOwnerCharacter);
		SetInstigator(NewOwnerCharacter);
	}

	RefreshViewAttachment(NewOwnerCharacter);
}

void AValorMeleeWeapon::RefreshViewAttachment(AValorCharacter* ViewOwnerCharacter)
{
	if (!ViewOwnerCharacter)
	{
		return;
	}

	ValorViewmodel::AttachHeldItemToOwnerView(*this, ViewOwnerCharacter);
	RefreshMeshAlignment();
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

	// GripSocket(직접 단 소켓)은 이미 총 규칙 방향이라고 보고 보정 없이 쓴다. 없으면 리그의 손잡이 뼈 + 축 보정.
	const bool bHasGripSocket = MeleeMesh->DoesSocketExist(MeleeGripSocketName);
	const FName GripReferenceName = bHasGripSocket ? MeleeGripSocketName : GripBoneName;
	if (GripReferenceName.IsNone() || !MeleeMesh->DoesSocketExist(GripReferenceName))
	{
		return;
	}

	// 총과 같은 방식: 손잡이 기준점(총 규칙 방향)이 부착점과 겹치도록 메시를 역변환한다.
	const FTransform GripTransform = MeleeMesh->GetSocketTransform(GripReferenceName, RTS_Component);
	const FQuat GripRotation = bHasGripSocket ? GripTransform.GetRotation() : GripTransform.GetRotation() * GripRotationOffset.Quaternion();
	const FQuat MeshRelativeRotation = GripRotation.Inverse();
	MeleeMesh->SetRelativeLocationAndRotation(MeshRelativeRotation.RotateVector(-GripTransform.GetLocation()), MeshRelativeRotation);
}

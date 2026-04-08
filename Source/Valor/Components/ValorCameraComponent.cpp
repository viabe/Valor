#include "ValorCameraComponent.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/SpringArmComponent.h"

UValorCameraComponent::UValorCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UValorCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CachedFollowCamera.IsValid())
	{
		return;
	}

	const float NextFOV = FMath::FInterpTo(CachedFollowCamera->FieldOfView, CurrentTargetFOV, DeltaTime, ADSInterpSpeed);
	CachedFollowCamera->SetFieldOfView(NextFOV);
}

void UValorCameraComponent::ConfigureFirstPersonView(ACharacter* OwningCharacter, USpringArmComponent* CameraBoom, UCameraComponent* FollowCamera)
{
	if (!OwningCharacter || !CameraBoom || !FollowCamera)
	{
		return;
	}

	// FPS 시점은 캐릭터에 가까운 카메라 붐으로 유지해 추후 시점 흔들림 확장이 가능하도록 한다.
	CameraBoom->TargetArmLength = 0.0f;
	CameraBoom->SocketOffset = CameraSocketOffset;
	CameraBoom->bUsePawnControlRotation = true;

	CachedFollowCamera = FollowCamera;
	CurrentTargetFOV = HipFireFOV;
	FollowCamera->FieldOfView = HipFireFOV;
	FollowCamera->bUsePawnControlRotation = false;
}

void UValorCameraComponent::RefreshLocalPresentation(ACharacter* OwningCharacter, bool bIsLocallyControlled)
{
	if (!OwningCharacter || !OwningCharacter->GetMesh())
	{
		return;
	}

	// 로컬 플레이어는 자기 자신의 풀바디 메시를 숨겨 카메라 클리핑을 방지한다.
	OwningCharacter->GetMesh()->SetOwnerNoSee(bIsLocallyControlled);
}

void UValorCameraComponent::SetADSState(bool bNewADS, float ADSFieldOfView, float InterpSpeed)
{
	bIsADSActive = bNewADS;
	ADSInterpSpeed = InterpSpeed > 0.0f ? InterpSpeed : 18.0f;
	CurrentTargetFOV = bIsADSActive ? ADSFieldOfView : HipFireFOV;
}

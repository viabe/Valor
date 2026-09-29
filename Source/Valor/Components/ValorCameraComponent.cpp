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

	// 뷰 펀치 복귀(발로란트의 리코일 회복): 연사 중(마지막 킥 후 RecoveryDelay 이내)에는 화면이
	// 패턴 위치에 그대로 머물러 "밀려 올라간" 상태를 유지하고, 사격이 멈추면 그때부터 원래 조준점으로
	// 스르륵 되돌아온다. FollowCamera는 bUsePawnControlRotation=false라 상대 회전이 항상 비어 있으므로,
	// 여기서 채우는 상대 회전이 곧 "조준과 분리된 순수 연출 킥"이 된다.
	if (!CurrentViewPunch.IsNearlyZero(0.01f))
	{
		const float TimeSinceLastPunch = GetWorld() ? (GetWorld()->GetTimeSeconds() - LastViewPunchWorldTime) : 0.0f;
		if (TimeSinceLastPunch >= ViewPunchRecoveryDelaySeconds)
		{
			CurrentViewPunch = FMath::RInterpTo(CurrentViewPunch, FRotator::ZeroRotator, DeltaTime, ViewPunchRecoverySpeed);
			if (CurrentViewPunch.IsNearlyZero(0.01f))
			{
				// 복귀가 끝나면 정확히 0으로 스냅해 미세 잔여 회전이 남지 않게 한다.
				CurrentViewPunch = FRotator::ZeroRotator;
			}

			CachedFollowCamera->SetRelativeRotation(CurrentViewPunch);
		}
	}
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

void UValorCameraComponent::AddRecoilViewPunch(const FRotator& PunchDelta, float RecoveryDelaySeconds, float RecoverySpeed, float MaxDegrees)
{
	if (!CachedFollowCamera.IsValid())
	{
		return;
	}

	ViewPunchRecoveryDelaySeconds = FMath::Max(RecoveryDelaySeconds, 0.0f);
	ViewPunchRecoverySpeed = RecoverySpeed > 0.0f ? RecoverySpeed : 10.0f;
	// 마지막 킥 시각을 갱신한다 → 연사 중에는 Tick의 복귀 지연 조건이 계속 미뤄져 화면이 내려가지 않는다.
	LastViewPunchWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : LastViewPunchWorldTime;

	// 누적 펀치가 시야를 과도하게 밀어내지 않도록 ±상한으로 클램프한다.
	CurrentViewPunch.Pitch = FMath::Clamp(CurrentViewPunch.Pitch + PunchDelta.Pitch, -MaxDegrees, MaxDegrees);
	CurrentViewPunch.Yaw = FMath::Clamp(CurrentViewPunch.Yaw + PunchDelta.Yaw, -MaxDegrees, MaxDegrees);
	CurrentViewPunch.Roll = 0.0f;

	// 다음 틱을 기다리지 않고 발사 프레임에 즉시 반영해 타격감을 살린다.
	CachedFollowCamera->SetRelativeRotation(CurrentViewPunch);
}

#include "ValorCameraComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/ValorCombatComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/SpringArmComponent.h"
#include "ValorCharacter.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"

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

	UpdateRecoilCamera(DeltaTime);
}

void UValorCameraComponent::UpdateRecoilCamera(float DeltaTime)
{
	FRotator TargetViewRecoil = FRotator::ZeroRotator;
	if (const AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		if (const UValorCombatComponent* CombatComponent = OwnerCharacter->GetCombatComponent())
		{
			// 발사/회복/ADS 전환이 모두 스프레이 상태에서 계산된 값 하나로 표현된다.
			TargetViewRecoil = CombatComponent->GetCameraRecoilOffset();
		}
	}

	// 올라갈 때는 빠르게 붙고, 돌아올 때(목표가 원점 쪽으로 줄어드는 중)는 조금 느리게 따라와 발로란트처럼 완만하게 내려온다.
	const bool bReturning = FMath::Abs(TargetViewRecoil.Pitch) + 0.01 < FMath::Abs(CurrentViewRecoil.Pitch);
	CurrentViewRecoil = FMath::RInterpTo(CurrentViewRecoil, TargetViewRecoil, DeltaTime, bReturning ? RecoilCameraReturnInterpSpeed : RecoilCameraInterpSpeed);
	if (TargetViewRecoil.IsNearlyZero(0.001f) && CurrentViewRecoil.IsNearlyZero(0.01f))
	{
		// 복귀가 끝나면 정확히 0으로 맞춰 미세한 잔여 회전이 남지 않게 한다.
		CurrentViewRecoil = FRotator::ZeroRotator;
	}

	// 사격 킥 스프링 전진(쏘지 않으면 스스로 0으로 가라앉는다).
	KickSpring.Advance(DeltaTime, KickMaxDegrees);

	// 최종 카메라 오프셋 = 패턴 추종(ADS) + 매 발 카메라 킥.
	AppliedViewOffset = CurrentViewRecoil + KickSpring.ToRotator();

	// FollowCamera는 bUsePawnControlRotation=false라 상대 회전이 비어 있으므로, 여기서 넣는 값이 곧 "조준과 분리된 반동 연출"이다.
	// 값이 그대로면 트랜스폼 갱신(자식 컴포넌트 전파 포함)을 건너뛴다 — 사격하지 않을 때는 0으로 고정이다.
	if (!CachedFollowCamera->GetRelativeRotation().Equals(AppliedViewOffset, 0.0001f))
	{
		CachedFollowCamera->SetRelativeRotation(AppliedViewOffset);
	}
}

void UValorCameraComponent::AddFireKick(const FValorCameraKickConfig& KickConfig)
{
	// 연출용 무작위라 서버/클라 결정성이 필요 없다(조준·탄도에 영향 없음).
	const float PitchKick = KickConfig.PitchDegrees * FMath::FRandRange(1.0f - KickConfig.PitchVariance, 1.0f);
	const float YawKick = FMath::FRandRange(-KickConfig.YawDegrees, KickConfig.YawDegrees);
	const float RollKick = FMath::FRandRange(-KickConfig.RollDegrees, KickConfig.RollDegrees);

	KickSpring.AddKick(FVector(PitchKick, YawKick, RollKick), KickConfig.PeakTimeSeconds);
	KickMaxDegrees = KickConfig.MaxKickDegrees;
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
	// 카메라 연출은 로컬 플레이어 화면에만 의미가 있으므로 서버/원격 캐릭터에서는 Tick을 꺼 CPU를 아낀다.
	SetComponentTickEnabled(bIsLocallyControlled);

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

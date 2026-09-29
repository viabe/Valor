#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ValorCameraComponent.generated.h"

class ACharacter;
class UCameraComponent;
class USpringArmComponent;

UCLASS(ClassGroup=(Valor), meta=(BlueprintSpawnableComponent))
class VALOR_API UValorCameraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UValorCameraComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// FPS 시점에 맞게 카메라 붐과 카메라 설정을 적용한다.
	void ConfigureFirstPersonView(ACharacter* OwningCharacter, USpringArmComponent* CameraBoom, UCameraComponent* FollowCamera);

	// 로컬 소유자에게만 1인칭 프레젠테이션을 적용한다.
	void RefreshLocalPresentation(ACharacter* OwningCharacter, bool bIsLocallyControlled);

	// ADS는 로컬 카메라 연출만 즉시 반영하고, 실제 명중 판정은 서버 상태를 따른다.
	void SetADSState(bool bNewADS, float ADSFieldOfView, float InterpSpeed);

	// 발사 시 시각적 반동 킥을 누적한다(Pitch+ = 위, Yaw+ = 오른쪽). 연사 중에는 감쇠 없이 패턴을 따라
	// 화면이 밀려 올라가고, 마지막 발사 후 RecoveryDelay가 지나면 원래 조준점으로 부드럽게 복귀한다.
	// FollowCamera의 '상대 회전'에만 적용되므로 컨트롤 회전(조준)과 서버 탄도에는 전혀 영향이 없다.
	void AddRecoilViewPunch(const FRotator& PunchDelta, float RecoveryDelaySeconds, float RecoverySpeed, float MaxDegrees);

private:
	UPROPERTY(EditDefaultsOnly, Category="Valor|Camera")
	FVector CameraSocketOffset = FVector(0.0f, 0.0f, 64.0f);

	UPROPERTY(EditDefaultsOnly, Category="Valor|Camera")
	float HipFireFOV = 100.0f;

	TWeakObjectPtr<UCameraComponent> CachedFollowCamera;
	float CurrentTargetFOV = 100.0f;
	float ADSInterpSpeed = 18.0f;
	bool bIsADSActive = false;

	// 현재 남아 있는 뷰 펀치(도). FollowCamera의 상대 회전으로만 표현된다.
	// 연사 중(마지막 킥 후 RecoveryDelay 이내)에는 패턴 위치를 유지하고, 그 후 0으로 감쇠 복귀한다.
	FRotator CurrentViewPunch = FRotator::ZeroRotator;
	float ViewPunchRecoverySpeed = 10.0f;
	float ViewPunchRecoveryDelaySeconds = 0.15f;
	float LastViewPunchWorldTime = -1000.0f;
};

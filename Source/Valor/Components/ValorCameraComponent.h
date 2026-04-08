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

private:
	UPROPERTY(EditDefaultsOnly, Category="Valor|Camera")
	FVector CameraSocketOffset = FVector(0.0f, 0.0f, 64.0f);

	UPROPERTY(EditDefaultsOnly, Category="Valor|Camera")
	float HipFireFOV = 100.0f;

	TWeakObjectPtr<UCameraComponent> CachedFollowCamera;
	float CurrentTargetFOV = 100.0f;
	float ADSInterpSpeed = 18.0f;
	bool bIsADSActive = false;
};

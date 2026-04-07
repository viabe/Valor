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

	// FPS 시점에 맞게 카메라 붐과 카메라 설정을 적용한다.
	void ConfigureFirstPersonView(ACharacter* OwningCharacter, USpringArmComponent* CameraBoom, UCameraComponent* FollowCamera) const;

	// 로컬 소유자에게만 1인칭 프레젠테이션을 적용한다.
	void RefreshLocalPresentation(ACharacter* OwningCharacter, bool bIsLocallyControlled) const;

private:
	UPROPERTY(EditDefaultsOnly, Category="Valor|Camera")
	FVector CameraSocketOffset = FVector(0.0f, 0.0f, 64.0f);

	UPROPERTY(EditDefaultsOnly, Category="Valor|Camera")
	float TargetFOV = 100.0f;
};

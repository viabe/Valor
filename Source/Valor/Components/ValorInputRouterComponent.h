#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ValorInputRouterComponent.generated.h"

class APlayerController;
class UInputAction;
class UInputComponent;
class UInputMappingContext;
struct FInputActionValue;

UCLASS(ClassGroup=(Valor), meta=(BlueprintSpawnableComponent))
class VALOR_API UValorInputRouterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UValorInputRouterComponent();

	// Character가 소유한 입력 자산을 받아 바인딩을 구성한다.
	void SetupInput(UInputComponent* PlayerInputComponent, APlayerController* PlayerController, UInputMappingContext* MappingContext, UInputAction* MoveAction, UInputAction* LookAction, UInputAction* JumpAction, UInputAction* WalkAction, UInputAction* CrouchAction, UInputAction* FireAction, UInputAction* ReloadAction, UInputAction* ADSAction, UInputAction* InteractAction);

private:
	void HandleMove(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleJumpStarted(const FInputActionValue& Value);
	void HandleJumpCompleted(const FInputActionValue& Value);
	void HandleWalkStarted(const FInputActionValue& Value);
	void HandleWalkCompleted(const FInputActionValue& Value);
	void HandleCrouchStarted(const FInputActionValue& Value);
	void HandleCrouchCompleted(const FInputActionValue& Value);
	void HandleFireStarted(const FInputActionValue& Value);
	void HandleFireCompleted(const FInputActionValue& Value);
	void HandleReloadStarted(const FInputActionValue& Value);
	void HandleADSStarted(const FInputActionValue& Value);
	void HandleADSCompleted(const FInputActionValue& Value);
	void HandleInteractStarted(const FInputActionValue& Value);
};

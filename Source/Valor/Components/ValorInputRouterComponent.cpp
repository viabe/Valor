#include "ValorInputRouterComponent.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "../ValorCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogValorInputRouter, Log, All);

UValorInputRouterComponent::UValorInputRouterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UValorInputRouterComponent::SetupInput(UInputComponent* PlayerInputComponent, APlayerController* PlayerController, UInputMappingContext* MappingContext, UInputAction* MoveAction, UInputAction* LookAction, UInputAction* JumpAction, UInputAction* WalkAction, UInputAction* CrouchAction)
{
	AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner());
	if (!OwnerCharacter || !PlayerInputComponent || !PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (MappingContext)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			// 재시작 시 중복 등록되지 않도록 한 번 제거 후 다시 추가한다.
			Subsystem->RemoveMappingContext(MappingContext);
			Subsystem->AddMappingContext(MappingContext, 0);
		}
	}

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInputComponent)
	{
		UE_LOG(LogValorInputRouter, Error, TEXT("Enhanced Input Component를 찾지 못했습니다: %s"), *GetNameSafe(OwnerCharacter));
		return;
	}

	if (JumpAction)
	{
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &UValorInputRouterComponent::HandleJumpStarted);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &UValorInputRouterComponent::HandleJumpCompleted);
	}

	if (WalkAction)
	{
		// 걷기는 홀드형 입력이므로 누를 때 활성화하고 떼면 즉시 해제한다.
		EnhancedInputComponent->BindAction(WalkAction, ETriggerEvent::Started, this, &UValorInputRouterComponent::HandleWalkStarted);
		EnhancedInputComponent->BindAction(WalkAction, ETriggerEvent::Completed, this, &UValorInputRouterComponent::HandleWalkCompleted);
		EnhancedInputComponent->BindAction(WalkAction, ETriggerEvent::Canceled, this, &UValorInputRouterComponent::HandleWalkCompleted);
	}

	if (CrouchAction)
	{
		// 앉기도 홀드형 입력이므로 누르는 동안만 유지한다.
		EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Started, this, &UValorInputRouterComponent::HandleCrouchStarted);
		EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Completed, this, &UValorInputRouterComponent::HandleCrouchCompleted);
		EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Canceled, this, &UValorInputRouterComponent::HandleCrouchCompleted);
	}

	if (MoveAction)
	{
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &UValorInputRouterComponent::HandleMove);
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &UValorInputRouterComponent::HandleMove);
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Canceled, this, &UValorInputRouterComponent::HandleMove);
	}

	if (LookAction)
	{
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &UValorInputRouterComponent::HandleLook);
	}
}

void UValorInputRouterComponent::HandleMove(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleMoveInput(Value.Get<FVector2D>());
	}
}

void UValorInputRouterComponent::HandleLook(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleLookInput(Value.Get<FVector2D>());
	}
}

void UValorInputRouterComponent::HandleJumpStarted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleJumpInputPressed();
	}
}

void UValorInputRouterComponent::HandleJumpCompleted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleJumpInputReleased();
	}
}

void UValorInputRouterComponent::HandleWalkStarted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleWalkInputPressed();
	}
}

void UValorInputRouterComponent::HandleWalkCompleted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleWalkInputReleased();
	}
}

void UValorInputRouterComponent::HandleCrouchStarted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleCrouchInputPressed();
	}
}

void UValorInputRouterComponent::HandleCrouchCompleted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleCrouchInputReleased();
	}
}

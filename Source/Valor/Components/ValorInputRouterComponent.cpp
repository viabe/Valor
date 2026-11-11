#include "ValorInputRouterComponent.h"

#include "Player/ValorPlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "../ValorCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogValorInputRouter, Log, All);

UValorInputRouterComponent::UValorInputRouterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UValorInputRouterComponent::SetupInput(UInputComponent* PlayerInputComponent, APlayerController* PlayerController, UInputMappingContext* MappingContext, UInputAction* MoveAction, UInputAction* LookAction, UInputAction* JumpAction, UInputAction* WalkAction, UInputAction* CrouchAction, UInputAction* FireAction, UInputAction* ReloadAction, UInputAction* ADSAction, UInputAction* InteractAction)
{
	AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner());
	if (!OwnerCharacter || !PlayerInputComponent || !PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer());
	if (MappingContext && Subsystem)
	{
		// 재시작 시 중복 등록되지 않도록 한 번 제거 후 다시 추가한다.
		Subsystem->RemoveMappingContext(MappingContext);
		Subsystem->AddMappingContext(MappingContext, 0);
	}

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInputComponent)
	{
		UE_LOG(LogValorInputRouter, Error, TEXT("Enhanced Input Component를 찾지 못했습니다: %s"), *GetNameSafe(OwnerCharacter));
		return;
	}

	SetupWeaponSlotInput(EnhancedInputComponent, Subsystem, PlayerController);

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

	if (FireAction)
	{
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &UValorInputRouterComponent::HandleFireStarted);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &UValorInputRouterComponent::HandleFireCompleted);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Canceled, this, &UValorInputRouterComponent::HandleFireCompleted);
	}

	if (ReloadAction)
	{
		EnhancedInputComponent->BindAction(ReloadAction, ETriggerEvent::Started, this, &UValorInputRouterComponent::HandleReloadStarted);
	}

	if (ADSAction)
	{
		EnhancedInputComponent->BindAction(ADSAction, ETriggerEvent::Started, this, &UValorInputRouterComponent::HandleADSStarted);
		EnhancedInputComponent->BindAction(ADSAction, ETriggerEvent::Completed, this, &UValorInputRouterComponent::HandleADSCompleted);
		EnhancedInputComponent->BindAction(ADSAction, ETriggerEvent::Canceled, this, &UValorInputRouterComponent::HandleADSCompleted);
	}

	if (InteractAction)
	{
		EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &UValorInputRouterComponent::HandleInteractStarted);
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

UInputAction* UValorInputRouterComponent::FindOrCreateBooleanAction(UObject* Outer, const TCHAR* ActionName)
{
	if (UInputAction* Existing = FindObject<UInputAction>(Outer, ActionName))
	{
		return Existing;
	}

	UInputAction* Action = NewObject<UInputAction>(Outer, FName(ActionName), RF_Transient);
	Action->ValueType = EInputActionValueType::Boolean;
	return Action;
}

void UValorInputRouterComponent::SetupWeaponSlotInput(UEnhancedInputComponent* EnhancedInputComponent, UEnhancedInputLocalPlayerSubsystem* Subsystem, APlayerController* PlayerController)
{
	// 컨텍스트·액션은 컨트롤러 밑에 한 벌만 만들고, 캐릭터가 새로 스폰될 때마다 같은 것을 다시 바인딩한다.
	// 캐릭터(이 컴포넌트)마다 새로 만들면 죽거나 라운드 시작에 지워진 캐릭터의 컨텍스트가 입력 서브시스템에 그대로 남아
	// 같은 키(1/2/3/G/B/휠)를 먼저 가져간다(같은 우선순위, 액션 기본값이 입력 소비). 그러면 새 캐릭터의 바인딩이 불리지 않는다.
	UObject* Outer = PlayerController;
	WeaponSlotMappingContext = FindObject<UInputMappingContext>(Outer, TEXT("ValorWeaponSlotMappingContext"));
	EquipPrimaryAction = FindOrCreateBooleanAction(Outer, TEXT("IA_EquipPrimary"));
	EquipSecondaryAction = FindOrCreateBooleanAction(Outer, TEXT("IA_EquipSecondary"));
	EquipMeleeAction = FindOrCreateBooleanAction(Outer, TEXT("IA_EquipMelee"));
	NextWeaponAction = FindOrCreateBooleanAction(Outer, TEXT("IA_NextWeapon"));
	PreviousWeaponAction = FindOrCreateBooleanAction(Outer, TEXT("IA_PreviousWeapon"));
	DropWeaponAction = FindOrCreateBooleanAction(Outer, TEXT("IA_DropWeapon"));
	BuyMenuAction = FindOrCreateBooleanAction(Outer, TEXT("IA_BuyMenu"));

	if (!WeaponSlotMappingContext)
	{
		WeaponSlotMappingContext = NewObject<UInputMappingContext>(Outer, TEXT("ValorWeaponSlotMappingContext"), RF_Transient);

		// 발로란트 기본 키: 1 주무기 / 2 보조무기 / 3 근접 / G 버리기 / B 상점. 휠은 다음·이전 무기(아래 = 다음).
		WeaponSlotMappingContext->MapKey(EquipPrimaryAction, EKeys::One);
		WeaponSlotMappingContext->MapKey(EquipSecondaryAction, EKeys::Two);
		WeaponSlotMappingContext->MapKey(EquipMeleeAction, EKeys::Three);
		WeaponSlotMappingContext->MapKey(NextWeaponAction, EKeys::MouseScrollDown);
		WeaponSlotMappingContext->MapKey(PreviousWeaponAction, EKeys::MouseScrollUp);
		WeaponSlotMappingContext->MapKey(DropWeaponAction, EKeys::G);
		WeaponSlotMappingContext->MapKey(BuyMenuAction, EKeys::B);
	}

	if (Subsystem)
	{
		Subsystem->RemoveMappingContext(WeaponSlotMappingContext);
		Subsystem->AddMappingContext(WeaponSlotMappingContext, 1);
	}

	EnhancedInputComponent->BindAction(EquipPrimaryAction, ETriggerEvent::Started, this, &UValorInputRouterComponent::HandleEquipPrimaryStarted);
	EnhancedInputComponent->BindAction(EquipSecondaryAction, ETriggerEvent::Started, this, &UValorInputRouterComponent::HandleEquipSecondaryStarted);
	EnhancedInputComponent->BindAction(EquipMeleeAction, ETriggerEvent::Started, this, &UValorInputRouterComponent::HandleEquipMeleeStarted);
	EnhancedInputComponent->BindAction(NextWeaponAction, ETriggerEvent::Started, this, &UValorInputRouterComponent::HandleNextWeaponStarted);
	EnhancedInputComponent->BindAction(PreviousWeaponAction, ETriggerEvent::Started, this, &UValorInputRouterComponent::HandlePreviousWeaponStarted);
	EnhancedInputComponent->BindAction(DropWeaponAction, ETriggerEvent::Started, this, &UValorInputRouterComponent::HandleDropWeaponStarted);
	EnhancedInputComponent->BindAction(BuyMenuAction, ETriggerEvent::Started, this, &UValorInputRouterComponent::HandleBuyMenuStarted);
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

void UValorInputRouterComponent::HandleFireStarted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleFireInputPressed();
	}
}

void UValorInputRouterComponent::HandleFireCompleted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleFireInputReleased();
	}
}

void UValorInputRouterComponent::HandleReloadStarted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleReloadInputPressed();
	}
}

void UValorInputRouterComponent::HandleADSStarted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleADSInputPressed();
	}
}

void UValorInputRouterComponent::HandleADSCompleted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleADSInputReleased();
	}
}

void UValorInputRouterComponent::HandleInteractStarted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleInteractInputPressed();
	}
}

void UValorInputRouterComponent::HandleEquipPrimaryStarted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleEquipSlotInput(EValorWeaponSlot::Primary);
	}
}

void UValorInputRouterComponent::HandleEquipSecondaryStarted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleEquipSlotInput(EValorWeaponSlot::Secondary);
	}
}

void UValorInputRouterComponent::HandleEquipMeleeStarted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleEquipSlotInput(EValorWeaponSlot::Melee);
	}
}

void UValorInputRouterComponent::HandleNextWeaponStarted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleCycleWeaponInput(1);
	}
}

void UValorInputRouterComponent::HandlePreviousWeaponStarted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleCycleWeaponInput(-1);
	}
}

void UValorInputRouterComponent::HandleDropWeaponStarted(const FInputActionValue& Value)
{
	if (AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner()))
	{
		OwnerCharacter->HandleDropWeaponInput();
	}
}

void UValorInputRouterComponent::HandleBuyMenuStarted(const FInputActionValue& Value)
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (AValorPlayerController* PlayerController = OwnerPawn ? Cast<AValorPlayerController>(OwnerPawn->GetController()) : nullptr)
	{
		PlayerController->ToggleBuyMenu();
	}
}

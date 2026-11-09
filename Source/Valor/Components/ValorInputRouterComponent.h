#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ValorInputRouterComponent.generated.h"

class APlayerController;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputComponent;
class UInputMappingContext;
struct FInputActionValue;

/**
 * 입력 라우터: Enhanced Input 바인딩을 캐릭터 밖으로 분리한다(입력 → 캐릭터 핸들러 연결만 담당).
 *
 * 무기 슬롯 입력(1/2/3, 휠, G)은 별도 매핑 컨텍스트를 런타임에 C++로 만들어 추가한다.
 *   왜: 기본 입력은 BP_ValorCharacterBase가 IMC_Player 에셋으로 덮어쓴다. 새 키를 그 에셋에 넣으려면 에디터 작업이 필요하고,
 *   캐릭터의 기본 서브오브젝트로 만들면 BP가 같은 방식으로 덮어쓸 수 있다. 로컬 플레이어에서만 만드는 런타임 컨텍스트는
 *   에셋과 독립적이고, 다른 컨텍스트와 키가 겹치지 않으므로 우선순위 충돌도 없다.
 * 네트워크: 입력은 로컬 전용이다. 결과(교체 요청)는 전투 컴포넌트가 서버로 보낸다.
 */
UCLASS(ClassGroup=(Valor), meta=(BlueprintSpawnableComponent))
class VALOR_API UValorInputRouterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UValorInputRouterComponent();

	// Character가 소유한 입력 자산을 받아 바인딩을 구성한다.
	void SetupInput(UInputComponent* PlayerInputComponent, APlayerController* PlayerController, UInputMappingContext* MappingContext, UInputAction* MoveAction, UInputAction* LookAction, UInputAction* JumpAction, UInputAction* WalkAction, UInputAction* CrouchAction, UInputAction* FireAction, UInputAction* ReloadAction, UInputAction* ADSAction, UInputAction* InteractAction);

private:
	void SetupWeaponSlotInput(UEnhancedInputComponent* EnhancedInputComponent, UEnhancedInputLocalPlayerSubsystem* Subsystem);
	UInputAction* CreateBooleanAction(const TCHAR* ActionName);

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
	void HandleEquipPrimaryStarted(const FInputActionValue& Value);
	void HandleEquipSecondaryStarted(const FInputActionValue& Value);
	void HandleEquipMeleeStarted(const FInputActionValue& Value);
	void HandleNextWeaponStarted(const FInputActionValue& Value);
	void HandlePreviousWeaponStarted(const FInputActionValue& Value);
	void HandleDropWeaponStarted(const FInputActionValue& Value);

	// 무기 슬롯 입력 컨텍스트와 액션(로컬 플레이어에서만 런타임 생성). UPROPERTY로 GC에서 보호한다.
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> WeaponSlotMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> EquipPrimaryAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> EquipSecondaryAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> EquipMeleeAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> NextWeaponAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PreviousWeaponAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DropWeaponAction;
};

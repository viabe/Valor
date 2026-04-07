#pragma once

#include "CoreMinimal.h"
#include "Components/ValorCharacterStateComponent.h"
#include "GameFramework/Character.h"
#include "Interfaces/ValorWeaponOwnerInterface.h"
#include "ValorCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UValorCameraComponent;
class UValorCharacterStateComponent;
class UValorInputRouterComponent;

UCLASS(config=Game)
class VALOR_API AValorCharacter : public ACharacter, public IValorWeaponOwnerInterface
{
	GENERATED_BODY()

private:
	// 카메라 붐은 길이를 0으로 유지해 FPS 시점 기준점과 향후 카메라 흔들림 확장 지점으로 사용한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	// 실제 플레이어 시점을 제공하는 1인칭 카메라다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

	// 기존 블루프린트 자산과 호환되도록 Character가 입력 자산을 직접 소유한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* LookAction;

	// 기본은 뛰기이고, 홀드 입력으로 걷기 전환을 받는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* WalkAction;

	// 홀드 입력으로 앉기 상태를 제어한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* CrouchAction;

	// 상태 복제와 상태 전이를 담당하는 전용 컴포넌트다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valor|Components", meta = (AllowPrivateAccess = "true"))
	UValorCharacterStateComponent* CharacterStateComponent;

	// 카메라 설정과 로컬 1인칭 프레젠테이션을 담당하는 전용 컴포넌트다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valor|Components", meta = (AllowPrivateAccess = "true"))
	UValorCameraComponent* CameraLogicComponent;

	// 입력 바인딩을 Character 외부로 분리해 이후 액션 확장을 쉽게 만든다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valor|Components", meta = (AllowPrivateAccess = "true"))
	UValorInputRouterComponent* InputRouterComponent;

	// 무기 시스템이 사용할 기본 부착 소켓 이름이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valor|Weapon", meta = (AllowPrivateAccess = "true"))
	FName WeaponAttachSocketName = TEXT("hand_r_socket");

	// 기본 이동 속도는 달리기 기준이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valor|Movement", meta = (AllowPrivateAccess = "true"))
	float RunSpeed = 500.0f;

	// 걷기 입력을 누르는 동안 적용할 속도다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valor|Movement", meta = (AllowPrivateAccess = "true"))
	float WalkSpeed = 250.0f;

	// 앉아 있는 동안 적용할 속도다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valor|Movement", meta = (AllowPrivateAccess = "true"))
	float CrouchSpeed = 180.0f;

public:
	AValorCharacter();

	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_Controller() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// 입력 라우터가 전달한 이동 입력을 처리한다.
	void HandleMoveInput(const FVector2D& RawMoveInput);

	// 입력 라우터가 전달한 시점 입력을 처리한다.
	void HandleLookInput(const FVector2D& RawLookInput);

	// 입력 라우터가 전달한 점프 시작 입력을 처리한다.
	void HandleJumpInputPressed();

	// 입력 라우터가 전달한 점프 해제 입력을 처리한다.
	void HandleJumpInputReleased();

	// 입력 라우터가 전달한 걷기 시작 입력을 처리한다.
	void HandleWalkInputPressed();

	// 입력 라우터가 전달한 걷기 해제 입력을 처리한다.
	void HandleWalkInputReleased();

	// 입력 라우터가 전달한 앉기 시작 입력을 처리한다.
	void HandleCrouchInputPressed();

	// 입력 라우터가 전달한 앉기 해제 입력을 처리한다.
	void HandleCrouchInputReleased();

	// 서버가 마지막으로 승인한 이동 입력값을 조회한다.
	FVector2D GetLastValidatedMoveInput() const { return LastValidatedMoveInput; }
	bool IsWalkInputActive() const { return bWantsToWalk; }

	// IValorWeaponOwnerInterface
	virtual USceneComponent* GetWeaponAttachComponent() const override;
	virtual FName GetWeaponAttachSocketName() const override;
	virtual void GetWeaponViewPoint(FVector& OutLocation, FRotator& OutRotation) const override;

	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }
	FORCEINLINE UValorCharacterStateComponent* GetCharacterStateComponent() const { return CharacterStateComponent; }

protected:
	// 이동 입력은 클라이언트 예측을 유지하되, 서버에도 검증용으로 전달한다.
	UFUNCTION(Server, Unreliable)
	void ServerSetMoveInput(FVector2D MoveInput);

	// 점프 입력은 서버가 유효성을 판단하고 상태 전이/후속 이펙트를 확정한다.
	UFUNCTION(Server, Reliable)
	void ServerSetJumpPressed(bool bPressed);

	// 걷기 의도는 서버가 최종 이동 속도를 확정할 수 있도록 별도 RPC로 전달한다.
	UFUNCTION(Server, Reliable)
	void ServerSetWalkPressed(bool bPressed);

	// 앉기 의도는 서버가 최종 자세와 속도를 확정할 수 있도록 별도 RPC로 전달한다.
	UFUNCTION(Server, Reliable)
	void ServerSetCrouchPressed(bool bPressed);

	// 서버가 비정상 입력을 정규화했을 때 소유 클라이언트에 보정값을 전달한다.
	UFUNCTION(Client, Unreliable)
	void ClientReceiveMoveInputCorrection(FVector2D CorrectedMoveInput);

	// 서버가 점프를 거부했을 때 소유 클라이언트의 로컬 예측 상태를 빠르게 정리한다.
	UFUNCTION(Client, Reliable)
	void ClientHandleJumpDenied();

	// 서버가 앉기를 거부했을 때 소유 클라이언트의 로컬 예측 상태를 원복한다.
	UFUNCTION(Client, Reliable)
	void ClientHandleCrouchDenied();

	// 점프 승인 같은 일회성 이벤트는 Multicast로 열어두고, 지속 상태는 복제로 유지한다.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayJumpCue();

	// 지속 상태와 별개로 원격 캐릭터의 짧은 상태 이펙트를 붙일 확장 지점이다.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastHandleMovementStateChanged(EValorMovementState NewMovementState);

private:
	void RefreshFirstPersonPresentation();
	void ApplyMoveInput(const FVector2D& SanitizedMoveInput);
	FVector2D SanitizeMoveInput(const FVector2D& RawMoveInput) const;
	void HandleMovementStateChanged(EValorMovementState PreviousMovementState, EValorMovementState NewMovementState);
	void SetWalkIntent(bool bNewWantsToWalk);
	void SetCrouchIntent(bool bNewWantsToCrouch);
	void RefreshMovementSpeed();

	// 서버가 마지막으로 승인한 입력값이다. 상태 계산과 이후 무기/능력 판정의 기준 입력으로 사용한다.
	FVector2D LastValidatedMoveInput = FVector2D::ZeroVector;

	// 걷기 홀드 입력의 현재 의도값이다.
	bool bWantsToWalk = false;

	// 앉기 홀드 입력의 현재 의도값이다.
	bool bWantsToCrouch = false;
};

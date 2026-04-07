#include "ValorCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ValorCameraComponent.h"
#include "Components/ValorInputRouterComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

AValorCharacter::AValorCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	NetUpdateFrequency = 100.0f;
	MinNetUpdateFrequency = 33.0f;

	// FPS 캐릭터의 충돌 캡슐을 기본 플레이어 크기로 유지한다.
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);

	// FPS는 Yaw만 액터 회전에 사용하고, Pitch는 카메라 시점에만 반영한다.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	// 서버 권한 이동은 CharacterMovementComponent의 기본 네트워크 시스템을 그대로 활용한다.
	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	MovementComponent->bOrientRotationToMovement = false;
	MovementComponent->bUseControllerDesiredRotation = true;
	MovementComponent->RotationRate = FRotator(0.0f, 720.0f, 0.0f);
	MovementComponent->JumpZVelocity = 700.0f;
	MovementComponent->AirControl = 0.35f;
	MovementComponent->GetNavAgentPropertiesRef().bCanCrouch = true;
	MovementComponent->MaxWalkSpeed = RunSpeed;
	MovementComponent->MaxWalkSpeedCrouched = CrouchSpeed;
	MovementComponent->MinAnalogWalkSpeed = 20.0f;
	MovementComponent->BrakingDecelerationWalking = 2000.0f;
	MovementComponent->BrakingDecelerationFalling = 1500.0f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->TargetArmLength = 0.0f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	CharacterStateComponent = CreateDefaultSubobject<UValorCharacterStateComponent>(TEXT("CharacterStateComponent"));
	CameraLogicComponent = CreateDefaultSubobject<UValorCameraComponent>(TEXT("CameraLogicComponent"));
	InputRouterComponent = CreateDefaultSubobject<UValorInputRouterComponent>(TEXT("InputRouterComponent"));
}

void AValorCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (CharacterStateComponent)
	{
		CharacterStateComponent->OnMovementStateChanged.AddUObject(this, &AValorCharacter::HandleMovementStateChanged);
	}

	if (CameraLogicComponent)
	{
		CameraLogicComponent->ConfigureFirstPersonView(this, CameraBoom, FollowCamera);
	}

	RefreshMovementSpeed();
	RefreshFirstPersonPresentation();
}

void AValorCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	RefreshFirstPersonPresentation();
}

void AValorCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();

	RefreshFirstPersonPresentation();
}

void AValorCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (InputRouterComponent)
	{
		InputRouterComponent->SetupInput(PlayerInputComponent, Cast<APlayerController>(GetController()), DefaultMappingContext, MoveAction, LookAction, JumpAction, WalkAction, CrouchAction);
	}
}

void AValorCharacter::HandleMoveInput(const FVector2D& RawMoveInput)
{
	const FVector2D SanitizedMoveInput = SanitizeMoveInput(RawMoveInput);
	ApplyMoveInput(SanitizedMoveInput);

	if (HasAuthority())
	{
		LastValidatedMoveInput = SanitizedMoveInput;
		return;
	}

	// 실제 이동 권한은 CharacterMovementComponent의 서버 검증 이동에 맡기고,
	// 이 RPC는 서버가 입력 의미를 검증해 상태/무기/능력 판정의 기준값으로 사용할 수 있게 한다.
	ServerSetMoveInput(SanitizedMoveInput);
}

void AValorCharacter::HandleLookInput(const FVector2D& RawLookInput)
{
	if (!Controller)
	{
		return;
	}

	AddControllerYawInput(RawLookInput.X);
	AddControllerPitchInput(RawLookInput.Y);
}

void AValorCharacter::HandleJumpInputPressed()
{
	const bool bCanJumpOnThisMachine = CanJump();
	Jump();

	if (HasAuthority())
	{
		if (bCanJumpOnThisMachine)
		{
			if (CharacterStateComponent)
			{
				CharacterStateComponent->NotifyJumpRequested();
			}

			MulticastPlayJumpCue();
		}

		return;
	}

	ServerSetJumpPressed(true);
}

void AValorCharacter::HandleJumpInputReleased()
{
	StopJumping();

	if (HasAuthority())
	{
		if (CharacterStateComponent)
		{
			CharacterStateComponent->ServerEvaluateMovementState();
		}

		return;
	}

	ServerSetJumpPressed(false);
}

void AValorCharacter::HandleWalkInputPressed()
{
	SetWalkIntent(true);

	if (!HasAuthority())
	{
		ServerSetWalkPressed(true);
	}
}

void AValorCharacter::HandleWalkInputReleased()
{
	SetWalkIntent(false);

	if (!HasAuthority())
	{
		ServerSetWalkPressed(false);
	}
}

void AValorCharacter::HandleCrouchInputPressed()
{
	SetCrouchIntent(true);

	if (!HasAuthority())
	{
		ServerSetCrouchPressed(true);
	}
}

void AValorCharacter::HandleCrouchInputReleased()
{
	SetCrouchIntent(false);

	if (!HasAuthority())
	{
		ServerSetCrouchPressed(false);
	}
}

USceneComponent* AValorCharacter::GetWeaponAttachComponent() const
{
	if (GetMesh())
	{
		return GetMesh();
	}

	return GetRootComponent();
}

FName AValorCharacter::GetWeaponAttachSocketName() const
{
	return WeaponAttachSocketName;
}

void AValorCharacter::GetWeaponViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	if (FollowCamera)
	{
		OutLocation = FollowCamera->GetComponentLocation();
		OutRotation = FollowCamera->GetComponentRotation();
		return;
	}

	GetActorEyesViewPoint(OutLocation, OutRotation);
}

void AValorCharacter::ServerSetMoveInput_Implementation(FVector2D MoveInput)
{
	const FVector2D SanitizedMoveInput = SanitizeMoveInput(MoveInput);
	LastValidatedMoveInput = SanitizedMoveInput;

	if (!MoveInput.Equals(SanitizedMoveInput, KINDA_SMALL_NUMBER))
	{
		ClientReceiveMoveInputCorrection(SanitizedMoveInput);
	}
}

void AValorCharacter::ServerSetJumpPressed_Implementation(bool bPressed)
{
	if (!CharacterStateComponent)
	{
		return;
	}

	if (!bPressed)
	{
		CharacterStateComponent->ServerEvaluateMovementState();
		return;
	}

	// 점프 이동 자체는 CharacterMovementComponent가 서버에서 최종 검증하지만,
	// 이 RPC는 점프 요청의 유효성 확인과 후속 상태/이펙트 확정 지점으로 사용한다.
	if (!CanJump())
	{
		ClientHandleJumpDenied();
		return;
	}

	CharacterStateComponent->NotifyJumpRequested();
	MulticastPlayJumpCue();
}

void AValorCharacter::ServerSetWalkPressed_Implementation(bool bPressed)
{
	// 걷기 상태는 서버가 최종 속도를 확정해야 원격 클라이언트와 판정 기준이 어긋나지 않는다.
	SetWalkIntent(bPressed);

	if (CharacterStateComponent)
	{
		CharacterStateComponent->ServerEvaluateMovementState();
	}
}

void AValorCharacter::ServerSetCrouchPressed_Implementation(bool bPressed)
{
	// 앉기는 CharacterMovement의 기본 네트워크 지원을 활용하되,
	// 입력 의도와 실제 자세 전환은 서버가 최종 판정한다.
	if (bPressed && !CanCrouch())
	{
		ClientHandleCrouchDenied();
		return;
	}

	SetCrouchIntent(bPressed);

	if (CharacterStateComponent)
	{
		CharacterStateComponent->ServerEvaluateMovementState();
	}
}

void AValorCharacter::ClientReceiveMoveInputCorrection_Implementation(FVector2D CorrectedMoveInput)
{
	// 서버가 정규화한 입력값을 로컬에도 반영해 이후 확장 시스템의 기준값을 일치시킨다.
	LastValidatedMoveInput = CorrectedMoveInput;
}

void AValorCharacter::ClientHandleJumpDenied_Implementation()
{
	// 서버가 점프를 거부하면 로컬 예측 상태를 즉시 되돌린다.
	StopJumping();
}

void AValorCharacter::ClientHandleCrouchDenied_Implementation()
{
	// 서버가 앉기를 거부하면 로컬 예측 상태를 즉시 원복한다.
	SetCrouchIntent(false);
}

void AValorCharacter::MulticastPlayJumpCue_Implementation()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 추후 원격 점프 애니메이션, 카메라 흔들림, 사운드 재생을 연결할 확장 지점이다.
}

void AValorCharacter::MulticastHandleMovementStateChanged_Implementation(EValorMovementState NewMovementState)
{
	if (GetNetMode() == NM_DedicatedServer || IsLocallyControlled())
	{
		return;
	}

	(void)NewMovementState;

	// 추후 원격 캐릭터의 상태 기반 이펙트나 보정 연출을 연결할 수 있다.
}

void AValorCharacter::RefreshFirstPersonPresentation()
{
	if (CameraLogicComponent)
	{
		CameraLogicComponent->RefreshLocalPresentation(this, IsLocallyControlled());
	}
}

void AValorCharacter::ApplyMoveInput(const FVector2D& SanitizedMoveInput)
{
	if (!Controller)
	{
		return;
	}

	const FRotator ControlRotation = Controller->GetControlRotation();
	const FRotator YawRotation(0.0f, ControlRotation.Yaw, 0.0f);

	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	AddMovementInput(ForwardDirection, SanitizedMoveInput.Y);
	AddMovementInput(RightDirection, SanitizedMoveInput.X);
}

FVector2D AValorCharacter::SanitizeMoveInput(const FVector2D& RawMoveInput) const
{
	FVector2D SanitizedMoveInput = RawMoveInput;
	const double InputSizeSquared = SanitizedMoveInput.SizeSquared();
	if (InputSizeSquared > 1.0)
	{
		SanitizedMoveInput.Normalize();
	}

	return SanitizedMoveInput;
}

void AValorCharacter::HandleMovementStateChanged(EValorMovementState PreviousMovementState, EValorMovementState NewMovementState)
{
	if (!HasAuthority() || PreviousMovementState == NewMovementState)
	{
		return;
	}

	// 지속 상태는 RepNotify가 맡고, 이 Multicast는 짧은 연출성 후처리만 담당한다.
	MulticastHandleMovementStateChanged(NewMovementState);
}

void AValorCharacter::SetWalkIntent(bool bNewWantsToWalk)
{
	if (bWantsToWalk == bNewWantsToWalk)
	{
		return;
	}

	bWantsToWalk = bNewWantsToWalk;
	RefreshMovementSpeed();

	if (HasAuthority() && CharacterStateComponent)
	{
		CharacterStateComponent->ServerEvaluateMovementState();
	}
}

void AValorCharacter::SetCrouchIntent(bool bNewWantsToCrouch)
{
	if (bWantsToCrouch == bNewWantsToCrouch)
	{
		return;
	}

	bWantsToCrouch = bNewWantsToCrouch;

	if (bWantsToCrouch)
	{
		Crouch();
	}
	else
	{
		UnCrouch();
	}

	RefreshMovementSpeed();

	if (HasAuthority() && CharacterStateComponent)
	{
		CharacterStateComponent->ServerEvaluateMovementState();
	}
}

void AValorCharacter::RefreshMovementSpeed()
{
	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	if (!MovementComponent)
	{
		return;
	}

	MovementComponent->MaxWalkSpeedCrouched = CrouchSpeed;

	if (bWantsToCrouch || bIsCrouched)
	{
		MovementComponent->MaxWalkSpeed = CrouchSpeed;
		return;
	}

	MovementComponent->MaxWalkSpeed = bWantsToWalk ? WalkSpeed : RunSpeed;
}
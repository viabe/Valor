#include "ValorCharacter.h"

#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystem/ValorAbilitySystemComponent.h"
#include "AbilitySystem/ValorDamageTypes.h"
#include "AbilitySystem/ValorGameplayTags.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ValorCharacterMovementComponent.h"
#include "Components/ValorCombatComponent.h"
#include "Components/ValorCameraComponent.h"
#include "Components/ValorInputRouterComponent.h"
#include "Components/ValorLagCompensationComponent.h"
#include "Components/ValorViewmodelComponent.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/ValorPlayerController.h"
#include "ValorGameMode.h"

AValorCharacter::AValorCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UValorCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	NetUpdateFrequency = 100.0f;
	MinNetUpdateFrequency = 33.0f;

	DefaultMappingContext = CreateDefaultSubobject<UInputMappingContext>(TEXT("DefaultMappingContext"));
	MoveAction = CreateDefaultSubobject<UInputAction>(TEXT("MoveAction"));
	LookAction = CreateDefaultSubobject<UInputAction>(TEXT("LookAction"));
	JumpAction = CreateDefaultSubobject<UInputAction>(TEXT("JumpAction"));
	WalkAction = CreateDefaultSubobject<UInputAction>(TEXT("WalkAction"));
	CrouchAction = CreateDefaultSubobject<UInputAction>(TEXT("CrouchAction"));
	FireAction = CreateDefaultSubobject<UInputAction>(TEXT("FireAction"));
	ReloadAction = CreateDefaultSubobject<UInputAction>(TEXT("ReloadAction"));
	ADSAction = CreateDefaultSubobject<UInputAction>(TEXT("ADSAction"));
	InteractAction = CreateDefaultSubobject<UInputAction>(TEXT("InteractAction"));

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

	// 3인칭 총 방향 보정. 마네킹 소총 대기 자세(조준 피치 0)에서 hand_r_socket 축을 캐릭터 기준으로 잰 값
	// (2026-11-09, ViewmodelCapture 도구, 총 5종 평균): 소켓 X는 거의 뒤(약간 오른쪽·위), Z는 거의 위(약간 앞·왼쪽).
	// 그 자세에서 총은 리그 Z(총구) = 앞, 리그 X(총의 왼쪽) = 왼쪽이어야 하므로 보정 = 소켓 회전의 역 x 원하는 총 회전.
	// 축을 반올림해(소켓 X = 정확히 뒤) 쓰면 총구가 약 6도 아래, 10도 왼쪽으로 기운다. 남는 차이(조준 피치, 눈과 총의 높이 차)는
	// 무기의 3인칭 조준 정렬(AValorWeaponBase::UpdateThirdPersonAim)이 매 프레임 맞춘다.
	const FQuat MeasuredHandSocket = FRotationMatrix::MakeFromXZ(FVector(-0.969f, 0.207f, 0.133f), FVector(0.103f, -0.151f, 0.983f)).ToQuat();
	const FQuat DesiredWeaponRig = FRotationMatrix::MakeFromXZ(FVector(0.0f, -1.0f, 0.0f), FVector(1.0f, 0.0f, 0.0f)).ToQuat();
	ThirdPersonWeaponRotation = (MeasuredHandSocket.Inverse() * DesiredWeaponRig).Rotator();

	// 카메라의 자식이라 시선·카메라 반동 연출을 그대로 따라가고, 그 위에 총 자체의 흔들림·킥을 더한다.
	ViewmodelComponent = CreateDefaultSubobject<UValorViewmodelComponent>(TEXT("ViewmodelComponent"));
	ViewmodelComponent->SetupAttachment(FollowCamera);

	CharacterStateComponent = CreateDefaultSubobject<UValorCharacterStateComponent>(TEXT("CharacterStateComponent"));
	CameraLogicComponent = CreateDefaultSubobject<UValorCameraComponent>(TEXT("CameraLogicComponent"));
	CombatComponent = CreateDefaultSubobject<UValorCombatComponent>(TEXT("CombatComponent"));
	AbilitySystemComponent = CreateDefaultSubobject<UValorAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	CombatAttributeSet = CreateDefaultSubobject<UValorCombatAttributeSet>(TEXT("CombatAttributeSet"));
	LagCompensationComponent = CreateDefaultSubobject<UValorLagCompensationComponent>(TEXT("LagCompensationComponent"));
	InputRouterComponent = CreateDefaultSubobject<UValorInputRouterComponent>(TEXT("InputRouterComponent"));

	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	BuildDefaultInputMapping();
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

	RefreshFirstPersonPresentation();
	InitializeAbilityActorInfo();

	// 피해 결과는 서버에서만 확정되므로 서버만 구독한다(클라는 Health/bIsDead 복제로 결과만 받는다).
	if (HasAuthority() && CombatAttributeSet)
	{
		CombatAttributeSet->OnDamageApplied.AddUObject(this, &AValorCharacter::HandleDamageApplied);
	}
}

void AValorCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValorCharacter, bIsDead);
}

void AValorCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	InitializeAbilityActorInfo();
	RefreshFirstPersonPresentation();
}

void AValorCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();

	InitializeAbilityActorInfo();
	RefreshFirstPersonPresentation();
}

void AValorCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (InputRouterComponent)
	{
		InputRouterComponent->SetupInput(PlayerInputComponent, Cast<APlayerController>(GetController()), DefaultMappingContext, MoveAction, LookAction, JumpAction, WalkAction, CrouchAction, FireAction, ReloadAction, ADSAction, InteractAction);
	}
}

void AValorCharacter::HandleMoveInput(const FVector2D& RawMoveInput)
{
	if (bIsDead)
	{
		return;
	}

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
	if (bIsDead)
	{
		return;
	}

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
	if (bIsDead)
	{
		return;
	}

	// 걷기는 이동 예측 데이터(압축 플래그)로 서버에 간다. 별도 RPC가 없어야 "걷기 시작 직전 이동"과 "직후 이동"이 서버에서도
	// 정확히 같은 속도로 재시뮬레이션된다.
	SetWalkIntent(true);
}

void AValorCharacter::HandleWalkInputReleased()
{
	SetWalkIntent(false);
}

void AValorCharacter::HandleCrouchInputPressed()
{
	if (bIsDead)
	{
		return;
	}

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

void AValorCharacter::HandleFireInputPressed()
{
	if (CombatComponent)
	{
		CombatComponent->HandleFireInputPressed();
	}
}

void AValorCharacter::HandleFireInputReleased()
{
	if (CombatComponent)
	{
		CombatComponent->HandleFireInputReleased();
	}
}

void AValorCharacter::HandleReloadInputPressed()
{
	if (CombatComponent)
	{
		CombatComponent->HandleReloadInputPressed();
	}
}

void AValorCharacter::HandleADSInputPressed()
{
	if (CombatComponent)
	{
		CombatComponent->HandleADSInputPressed();
	}
}

void AValorCharacter::HandleADSInputReleased()
{
	if (CombatComponent)
	{
		CombatComponent->HandleADSInputReleased();
	}
}

void AValorCharacter::HandleInteractInputPressed()
{
	if (CombatComponent)
	{
		CombatComponent->HandleInteractInputPressed();
	}
}

void AValorCharacter::HandleEquipSlotInput(EValorWeaponSlot Slot)
{
	if (CombatComponent)
	{
		CombatComponent->HandleEquipSlotInput(Slot);
	}
}

void AValorCharacter::HandleCycleWeaponInput(int32 Direction)
{
	if (CombatComponent)
	{
		CombatComponent->HandleCycleWeaponInput(Direction);
	}
}

void AValorCharacter::HandleDropWeaponInput()
{
	if (CombatComponent)
	{
		CombatComponent->HandleDropWeaponInput();
	}
}

bool AValorCharacter::IsWalkInputActive() const
{
	const UValorCharacterMovementComponent* MovementComponent = GetValorMovement();
	return MovementComponent && MovementComponent->WantsToWalk();
}

UValorCharacterMovementComponent* AValorCharacter::GetValorMovement() const
{
	return Cast<UValorCharacterMovementComponent>(GetCharacterMovement());
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

USceneComponent* AValorCharacter::GetFirstPersonWeaponAttachComponent() const
{
	return IsLocalPlayerView() ? ViewmodelComponent : nullptr;
}

bool AValorCharacter::IsLocalPlayerView() const
{
	// 컨트롤러는 소유 클라에만 복제되므로, 다른 플레이어의 캐릭터에서는 항상 false다. 리슨 호스트도 자기 캐릭터만 true.
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	return PlayerController && PlayerController->IsLocalController() && !bIsDead;
}

FRotator AValorCharacter::GetBaseAimRotation() const
{
	// 엔진 기본 구현은 컨트롤러가 있으면 PlayerController::GetPlayerViewPoint(카메라 캐시)를 쓴다.
	// 서버에서 원격 플레이어의 카메라 캐시는 클라가 "움직일 때만" 보내는 카메라 갱신으로 채워진다(PlayerController.cpp).
	// 그래서 제자리에 서서 마우스로만 돌면 갱신되지 않아, 리슨 호스트 화면에서 그 플레이어의 상체·총 조준이 예전 방향에 멈춘다.
	// 컨트롤 회전은 매 이동 패킷(ServerMove)에 실려 오므로 서버에서도 최신이다. 로컬 플레이어도 같은 값(카메라 연출 킥 제외)이다.
	// 컨트롤러가 없는 머신(다른 클라의 시뮬레이티드 프록시)은 기본 구현(액터 요 + 복제된 피치)을 그대로 쓴다.
	if (const AController* CurrentController = GetController())
	{
		return CurrentController->GetControlRotation();
	}

	return Super::GetBaseAimRotation();
}

void AValorCharacter::GetWeaponViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	if (FollowCamera)
	{
		OutLocation = FollowCamera->GetComponentLocation();
		// 방향은 카메라 컴포넌트 회전이 아니라 컨트롤 회전(GetViewRotation)을 쓴다.
		// FollowCamera에는 시각적 반동 펀치(연출용 상대 회전)가 실릴 수 있는데, 사격/상호작용 방향은
		// 펀치와 무관하게 순수 조준을 따라야 하기 때문이다. 특히 리슨서버 호스트는 서버가 자기
		// FollowCamera를 직접 읽으므로, 여기서 분리하지 않으면 연출 킥이 실제 탄도에 새어 들어간다.
		OutRotation = GetViewRotation();
		return;
	}

	GetActorEyesViewPoint(OutLocation, OutRotation);
}

UAbilitySystemComponent* AValorCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AValorCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (const UWorld* World = GetWorld())
	{
		LastLandedWorldTime = World->GetTimeSeconds();
	}
}

bool AValorCharacter::IsAlive() const
{
	return !bIsDead && CombatAttributeSet && CombatAttributeSet->GetHealth() > 0.0f;
}

void AValorCharacter::HandleDamageApplied(const FValorDamageEvent& DamageEvent)
{
	if (!HasAuthority())
	{
		return;
	}

	const float AppliedDamage = DamageEvent.Resolution.HealthDamage + DamageEvent.Resolution.ShieldDamage;

	// 피격자 화면: 피해가 온 방향 표시(사수 눈 위치). 비신뢰 RPC — 한 번 빠져도 다음 피격에 다시 표시되는 연출이다.
	if (AValorPlayerController* VictimController = Cast<AValorPlayerController>(GetController()))
	{
		VictimController->ClientNotifyDamageTaken(DamageEvent.bHasOrigin ? DamageEvent.Origin : GetActorLocation(), AppliedDamage);
	}

	// 사수 화면: 명중 확인(부위, 처치 여부, 실제 깎인 양). 서버 판정 결과라서 클라 예측 탄흔과 달리 "진짜로 맞았다"는 신호다.
	const APawn* InstigatorPawn = Cast<APawn>(DamageEvent.Instigator.Get());
	if (InstigatorPawn && InstigatorPawn != this)
	{
		if (AValorPlayerController* ShooterController = Cast<AValorPlayerController>(InstigatorPawn->GetController()))
		{
			FValorHitConfirm HitConfirm;
			HitConfirm.Damage = AppliedDamage;
			HitConfirm.HitZone = DamageEvent.HitZone;
			HitConfirm.bKilled = DamageEvent.Resolution.bKilled;
			ShooterController->ClientNotifyHitConfirmed(HitConfirm);
		}
	}

	if (DamageEvent.Resolution.bKilled)
	{
		Die(DamageEvent);
	}
}

void AValorCharacter::Die(const FValorDamageEvent& KillingEvent)
{
	if (!HasAuthority() || bIsDead)
	{
		return;
	}

	bIsDead = true;

	// GAS 쪽 사망 상태: 서버 로컬 태그(서버 전용 어빌리티 차단) + 복제 루즈 태그(클라 ASC도 같은 태그를 갖게 — 이후 스킬 UI/예측용).
	// 복제 루즈 태그는 클라 쪽 카운트만 갱신하므로 서버 자신의 카운트는 로컬 태그로 따로 올린다.
	// 태그를 먼저 붙인 뒤 어빌리티를 취소해야, 취소 콜백 중에 다시 활성화되는 경로가 없다.
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->AddLooseGameplayTag(ValorGameplayTags::State_Dead);
		AbilitySystemComponent->AddReplicatedLooseGameplayTag(ValorGameplayTags::State_Dead);
		AbilitySystemComponent->CancelAllAbilities();
	}

	if (CombatComponent)
	{
		CombatComponent->HandleOwnerDeath();
	}

	// 시체는 히트스캔 대상이 아니므로 랙 보상 히트박스 기록도 멈춘다(서버 틱 비용 절감).
	if (LagCompensationComponent)
	{
		LagCompensationComponent->SetComponentTickEnabled(false);
	}

	ApplyDeathPresentation();

	// 킬 크레딧·킬피드·리스폰은 게임 규칙이므로 GameMode(서버에만 존재)가 정한다.
	if (UWorld* World = GetWorld())
	{
		if (AValorGameMode* GameMode = World->GetAuthGameMode<AValorGameMode>())
		{
			GameMode->HandleCharacterKilled(this, KillingEvent);
		}
	}
}

void AValorCharacter::OnRep_IsDead()
{
	if (!bIsDead)
	{
		return;
	}

	if (CombatComponent)
	{
		CombatComponent->HandleOwnerDeath();
	}

	ApplyDeathPresentation();
}

void AValorCharacter::ApplyDeathPresentation()
{
	// 이동 정지: 서버와 소유 클라가 같이 끈다(소유 클라가 계속 이동을 예측하면 서버 보정이 반복된다).
	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->StopMovementImmediately();
		MovementComponent->DisableMovement();
	}

	// 캡슐은 길을 막지 않게 끈다. 탄 판정은 원래 캡슐이 아니라 랙 보상 히트박스이고, 서버 히트스캔이 시체를 건너뛴다.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 래그돌은 화면이 있는 머신에서만 시뮬레이션한다(전용 서버는 그릴 화면이 없으므로 물리 비용을 쓰지 않는다).
	// 시체가 탄/시야/카메라 트레이스를 막지 않게 해서, 클라 예측 탄흔과 사망 시점 카메라가 시체에 걸리지 않게 한다.
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (GetNetMode() != NM_DedicatedServer && CharacterMesh)
	{
		CharacterMesh->SetCollisionProfileName(TEXT("Ragdoll"));
		CharacterMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		CharacterMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		CharacterMesh->SetAllBodiesSimulatePhysics(true);
		CharacterMesh->SetSimulatePhysics(true);
		CharacterMesh->WakeAllRigidBodies();
	}

	if (IsLocallyControlled() && CameraLogicComponent)
	{
		CameraLogicComponent->EnterDeathView(this, CameraBoom, DeathCameraArmLength);
	}

	// 사망 시점은 3인칭이라 카메라에 붙은 1인칭 총을 숨긴다. 총은 서버가 곧 떨어뜨리거나 없애므로 다시 붙이지 않는다.
	if (ViewmodelComponent && ViewmodelComponent->IsFirstPersonActive())
	{
		ViewmodelComponent->SetFirstPersonActive(false);
		ViewmodelComponent->SetVisibility(false, true);
	}
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
	if (bIsDead)
	{
		return;
	}

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

void AValorCharacter::ServerSetCrouchPressed_Implementation(bool bPressed)
{
	if (bIsDead)
	{
		return;
	}

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

	// 로컬 조종 여부가 확정되는 시점(빙의·컨트롤러 복제)이다. 총이 그보다 먼저 복제돼 3인칭 손에 붙었을 수 있으므로 다시 붙인다.
	if (ViewmodelComponent)
	{
		ViewmodelComponent->SetFirstPersonActive(IsLocalPlayerView());
	}

	if (CombatComponent)
	{
		CombatComponent->RefreshEquipmentViewAttachment();
	}
}

void AValorCharacter::InitializeAbilityActorInfo()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	AbilitySystemComponent->InitAbilityActorInfo(this, this);

	if (CombatComponent)
	{
		CombatComponent->InitializeAbilityBindings(AbilitySystemComponent);
	}
}

void AValorCharacter::BuildDefaultInputMapping()
{
	if (!DefaultMappingContext || !MoveAction || !LookAction || !JumpAction || !WalkAction || !CrouchAction || !FireAction || !ReloadAction || !ADSAction || !InteractAction)
	{
		return;
	}

	MoveAction->ValueType = EInputActionValueType::Axis2D;
	LookAction->ValueType = EInputActionValueType::Axis2D;
	JumpAction->ValueType = EInputActionValueType::Boolean;
	WalkAction->ValueType = EInputActionValueType::Boolean;
	CrouchAction->ValueType = EInputActionValueType::Boolean;
	FireAction->ValueType = EInputActionValueType::Boolean;
	ReloadAction->ValueType = EInputActionValueType::Boolean;
	ADSAction->ValueType = EInputActionValueType::Boolean;
	InteractAction->ValueType = EInputActionValueType::Boolean;

	DefaultMappingContext->MapKey(LookAction, EKeys::Mouse2D);
	DefaultMappingContext->MapKey(JumpAction, EKeys::SpaceBar);
	DefaultMappingContext->MapKey(WalkAction, EKeys::LeftShift);
	DefaultMappingContext->MapKey(CrouchAction, EKeys::LeftControl);
	DefaultMappingContext->MapKey(FireAction, EKeys::LeftMouseButton);
	DefaultMappingContext->MapKey(ReloadAction, EKeys::R);
	DefaultMappingContext->MapKey(ADSAction, EKeys::RightMouseButton);
	DefaultMappingContext->MapKey(InteractAction, EKeys::F);
	

	FEnhancedActionKeyMapping& MoveForwardMapping = DefaultMappingContext->MapKey(MoveAction, EKeys::W);
	UInputModifierSwizzleAxis* MoveForwardSwizzle = NewObject<UInputModifierSwizzleAxis>(DefaultMappingContext);
	MoveForwardSwizzle->Order = EInputAxisSwizzle::YXZ;
	MoveForwardMapping.Modifiers.Add(MoveForwardSwizzle);

	FEnhancedActionKeyMapping& MoveBackwardMapping = DefaultMappingContext->MapKey(MoveAction, EKeys::S);
	UInputModifierSwizzleAxis* MoveBackwardSwizzle = NewObject<UInputModifierSwizzleAxis>(DefaultMappingContext);
	MoveBackwardSwizzle->Order = EInputAxisSwizzle::YXZ;
	UInputModifierNegate* MoveBackwardNegate = NewObject<UInputModifierNegate>(DefaultMappingContext);
	MoveBackwardMapping.Modifiers.Add(MoveBackwardSwizzle);
	MoveBackwardMapping.Modifiers.Add(MoveBackwardNegate);

	FEnhancedActionKeyMapping& MoveRightMapping = DefaultMappingContext->MapKey(MoveAction, EKeys::D);
	(void)MoveRightMapping;

	FEnhancedActionKeyMapping& MoveLeftMapping = DefaultMappingContext->MapKey(MoveAction, EKeys::A);
	UInputModifierNegate* MoveLeftNegate = NewObject<UInputModifierNegate>(DefaultMappingContext);
	MoveLeftMapping.Modifiers.Add(MoveLeftNegate);
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
	UValorCharacterMovementComponent* MovementComponent = GetValorMovement();
	if (!MovementComponent || MovementComponent->WantsToWalk() == bNewWantsToWalk)
	{
		return;
	}

	MovementComponent->SetWantsToWalk(bNewWantsToWalk);

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

	if (HasAuthority() && CharacterStateComponent)
	{
		CharacterStateComponent->ServerEvaluateMovementState();
	}
}

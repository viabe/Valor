#include "ValorAnimInstance.h"

#include "Components/ValorCombatComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ValorCharacter.h"
#include "Weapons/ValorWeaponBase.h"

void UValorAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	CacheAnimationSources();
}

void UValorAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	CacheAnimationSources();
	if (!OwnerCharacter)
	{
		return;
	}

	UpdateLocomotionData();
	UpdateCombatData(DeltaSeconds);
	UpdateAimData();
	UpdateExtensionData();
}

void UValorAnimInstance::CacheAnimationSources()
{
	OwnerCharacter = Cast<AValorCharacter>(TryGetPawnOwner());
	if (!OwnerCharacter)
	{
		MovementComponent = nullptr;
		CharacterStateComponent = nullptr;
		CombatComponent = nullptr;
		EquippedWeapon = nullptr;
		return;
	}

	MovementComponent = OwnerCharacter->GetCharacterMovement();
	CharacterStateComponent = OwnerCharacter->GetCharacterStateComponent();
	CombatComponent = OwnerCharacter->GetCombatComponent();
	EquippedWeapon = CombatComponent ? CombatComponent->GetEquippedWeapon() : nullptr;
}

void UValorAnimInstance::UpdateLocomotionData()
{
	if (!OwnerCharacter)
	{
		return;
	}

	const FVector Velocity = OwnerCharacter->GetVelocity();
	GroundSpeed = Velocity.Size2D();
	VerticalSpeed = Velocity.Z;

	float MaxGroundSpeed = 1.0f;
	if (MovementComponent)
	{
		MaxGroundSpeed = FMath::Max(MovementComponent->GetMaxSpeed(), 1.0f);
		bIsFalling = MovementComponent->IsFalling();
		bIsCrouched = MovementComponent->IsCrouching();
	}
	else
	{
		bIsFalling = false;
		bIsCrouched = false;
	}

	NormalizedGroundSpeed = FMath::Clamp(GroundSpeed / MaxGroundSpeed, 0.0f, 1.0f);
	bShouldMove = GroundSpeed > 3.0f && !bIsFalling;

	if (bShouldMove)
	{
		// FPS는 캐릭터 메시가 아니라 현재 조준/시선 방향 기준으로 스트레이프 애니메이션을 계산해야 자연스럽다.
		const FRotator AimYawRotation(0.0f, OwnerCharacter->GetBaseAimRotation().Yaw, 0.0f);
		const FVector LocalVelocity = AimYawRotation.UnrotateVector(FVector(Velocity.X, Velocity.Y, 0.0f));
		MoveDirection = FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y, LocalVelocity.X));
	}
	else
	{
		MoveDirection = 0.0f;
	}

	MovementState = CharacterStateComponent ? CharacterStateComponent->GetCurrentMovementState() : ResolveMovementStateFallback();
	AnimationProfileName = OwnerCharacter->GetAnimationProfileName();
}

void UValorAnimInstance::UpdateCombatData(float DeltaSeconds)
{
	bHasEquippedWeapon = EquippedWeapon != nullptr;
	EquippedWeaponType = EquippedWeapon ? EquippedWeapon->GetWeaponAnimationType() : EValorWeaponAnimationType::None;

	if (!CombatComponent)
	{
		bIsADS = false;
		bIsReloading = false;
		bIsFireInputHeld = false;
		TimeSinceLastFire = FMath::Min(TimeSinceLastFire + DeltaSeconds, 999.0f);
		return;
	}

	bIsADS = CombatComponent->IsADSActive();
	bIsReloading = CombatComponent->IsReloading();
	bIsFireInputHeld = CombatComponent->IsFireInputHeld();

	const float LastFireTime = CombatComponent->GetLastFireSimulationWorldTime();
	if (LastFireTime <= 0.0f || !GetWorld())
	{
		TimeSinceLastFire = FMath::Min(TimeSinceLastFire + DeltaSeconds, 999.0f);
		return;
	}

	TimeSinceLastFire = FMath::Max(0.0f, GetWorld()->GetTimeSeconds() - LastFireTime);
}

void UValorAnimInstance::UpdateAimData()
{
	if (!OwnerCharacter)
	{
		return;
	}

	const FRotator ActorRotation = OwnerCharacter->GetActorRotation();
	const FRotator BaseAimRotation = OwnerCharacter->GetBaseAimRotation();
	const FRotator AimDelta = (BaseAimRotation - ActorRotation).GetNormalized();

	AimPitch = AimDelta.Pitch;
	if (AimPitch > 180.0f)
	{
		AimPitch -= 360.0f;
	}

	AimYawOffset = AimDelta.Yaw;
}

void UValorAnimInstance::UpdateExtensionData()
{
	// 추후 GAS 능력 상태, 피격 리액션, 사망 몽타주 트리거가 들어올 자리를 먼저 고정한다.
	AbilityAnimationState = EValorAbilityAnimationState::None;
	HitReactType = EValorHitReactType::None;
	DeathAnimationState = OwnerCharacter && OwnerCharacter->IsAlive()
		? EValorDeathAnimationState::Alive
		: EValorDeathAnimationState::Dead;
}

EValorMovementState UValorAnimInstance::ResolveMovementStateFallback() const
{
	if (bIsFalling)
	{
		return EValorMovementState::Jumping;
	}

	if (bIsCrouched)
	{
		return EValorMovementState::Crouching;
	}

	if (GroundSpeed > 3.0f)
	{
		return OwnerCharacter && OwnerCharacter->IsWalkInputActive()
			? EValorMovementState::Walking
			: EValorMovementState::Moving;
	}

	return EValorMovementState::Idle;
}

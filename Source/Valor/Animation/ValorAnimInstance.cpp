#include "ValorAnimInstance.h"

#include "Components/ValorCombatComponent.h"
#include "Engine/Engine.h"
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
	HandleFireMontage();
	UpdateAimData();
	UpdateExtensionData();

#if !UE_BUILD_SHIPPING
	// 디버그 확인용: 실제 폰의 AnimInstance가 받는 값을 화면에 직접 출력해 에디터 디버거와 무관하게 진단한다. (원인 확정 후 제거)
	if (GEngine && OwnerCharacter && OwnerCharacter->IsLocallyControlled())
	{
		const FColor DebugColor = bHasEquippedWeapon ? FColor::Green : FColor::Red;
		GEngine->AddOnScreenDebugMessage(7001, 0.0f, DebugColor,
			FString::Printf(TEXT("[Aim] Equipped=%d  Alpha=%.2f  Pitch=%.1f  Weapon=%s"),
				bHasEquippedWeapon ? 1 : 0,
				AimOffsetAlpha,
				AimPitch,
				EquippedWeapon ? *EquippedWeapon->GetName() : TEXT("None")));
	}
#endif
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
		LastConsumedFireSimulationWorldTime = -1000.0f;
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

void UValorAnimInstance::HandleFireMontage()
{
	if (!CombatComponent)
	{
		LastConsumedFireSimulationWorldTime = -1000.0f;
		return;
	}

	const float LastFireTime = CombatComponent->GetLastFireSimulationWorldTime();
	if (LastFireTime <= LastConsumedFireSimulationWorldTime + KINDA_SMALL_NUMBER)
	{
		return;
	}

	// 서버가 승인한 새 발사만 소비해 자동 사격에서도 사격 수와 애니메이션 수가 어긋나지 않게 맞춘다.
	LastConsumedFireSimulationWorldTime = LastFireTime;

	if (!EquippedWeapon || bIsReloading)
	{
		return;
	}

	UAnimMontage* FireMontage = EquippedWeapon->GetFireMontage();
	if (!FireMontage)
	{
		return;
	}

	Montage_Play(FireMontage, EquippedWeapon->GetFireMontagePlayRate(), EMontagePlayReturnType::MontageLength, 0.0f, false);
}

void UValorAnimInstance::UpdateAimData()
{
	if (!OwnerCharacter)
	{
		return;
	}

	AimOffsetAlpha = bHasEquippedWeapon ? 1.0f : 0.0f;
	if (!bHasEquippedWeapon)
	{
		// 비장착 상태에서는 AnimGraph 끝에 AimOffset이 있어도 기본 이동 포즈를 오염시키지 않는다.
		AimPitch = 0.0f;
		AimYawOffset = 0.0f;
		return;
	}

	const FRotator ActorRotation = OwnerCharacter->GetActorRotation();
	const FRotator BaseAimRotation = OwnerCharacter->GetBaseAimRotation();
	const FRotator AimDelta = (BaseAimRotation - ActorRotation).GetNormalized();

	// GetBaseAimRotation 기준으로는 위를 볼 때 Pitch가 음수로 나온다.
	// 그런데 AimOffset 에셋은 위 포즈를 +90, 아래 포즈를 -90으로 두는 표준 규약을 쓰므로
	// 부호를 뒤집어 줘야 시선 방향과 상체 조준 방향이 일치한다. (안 뒤집으면 위/아래가 반대로 적용됨)
	AimPitch = -AimDelta.Pitch;

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

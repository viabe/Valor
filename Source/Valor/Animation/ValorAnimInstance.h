#pragma once

#include "Animation/AnimInstance.h"
#include "ValorAnimTypes.h"
#include "Components/ValorCharacterStateComponent.h"
#include "ValorAnimInstance.generated.h"

class AValorCharacter;
class AValorWeaponBase;
class UCharacterMovementComponent;
class UValorCharacterStateComponent;
class UValorCombatComponent;

UCLASS(BlueprintType, Blueprintable)
class VALOR_API UValorAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	// AnimBP가 공통으로 참조할 수 있도록 캐시한 캐릭터 루트다.
	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|References", meta=(AllowPrivateAccess="true"))
	TObjectPtr<AValorCharacter> OwnerCharacter;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|References", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UCharacterMovementComponent> MovementComponent;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|References", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UValorCharacterStateComponent> CharacterStateComponent;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|References", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UValorCombatComponent> CombatComponent;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|References", meta=(AllowPrivateAccess="true"))
	TObjectPtr<AValorWeaponBase> EquippedWeapon;

	// 하체 이동 레이어가 공통으로 소비할 기초 값이다.
	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Locomotion", meta=(AllowPrivateAccess="true"))
	float GroundSpeed = 0.0f;

	// 점프 상승, 낙하 루프, 착지 전환에 쓸 수 있도록 수직 속도를 별도로 노출한다.
	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Locomotion", meta=(AllowPrivateAccess="true"))
	float VerticalSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Locomotion", meta=(AllowPrivateAccess="true"))
	float NormalizedGroundSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Locomotion", meta=(AllowPrivateAccess="true"))
	float MoveDirection = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Locomotion", meta=(AllowPrivateAccess="true"))
	bool bShouldMove = false;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Locomotion", meta=(AllowPrivateAccess="true"))
	bool bIsFalling = false;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Locomotion", meta=(AllowPrivateAccess="true"))
	bool bIsCrouched = false;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Locomotion", meta=(AllowPrivateAccess="true"))
	EValorMovementState MovementState = EValorMovementState::Idle;

	// 상체 전투 레이어가 공통으로 참조할 기초 값이다.
	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Combat", meta=(AllowPrivateAccess="true"))
	bool bHasEquippedWeapon = false;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Combat", meta=(AllowPrivateAccess="true"))
	bool bIsADS = false;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Combat", meta=(AllowPrivateAccess="true"))
	bool bIsReloading = false;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Combat", meta=(AllowPrivateAccess="true"))
	bool bIsFireInputHeld = false;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Combat", meta=(AllowPrivateAccess="true"))
	float TimeSinceLastFire = 999.0f;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Combat", meta=(AllowPrivateAccess="true"))
	EValorWeaponAnimationType EquippedWeaponType = EValorWeaponAnimationType::None;

	// 조준/에임 오프셋 레이어 기초 값이다.
	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Aim", meta=(AllowPrivateAccess="true"))
	float AimPitch = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Aim", meta=(AllowPrivateAccess="true"))
	float AimYawOffset = 0.0f;

	// 캐릭터 공통 데이터 계약은 유지하되, 이후 캐릭터별 오버라이드에 사용할 수 있도록 남겨둔다.
	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Profile", meta=(AllowPrivateAccess="true"))
	FName AnimationProfileName = TEXT("Default");

	// 아래 값들은 이후 스킬, 피격, 사망 레이어를 위한 확장 포인트다.
	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Extension", meta=(AllowPrivateAccess="true"))
	EValorAbilityAnimationState AbilityAnimationState = EValorAbilityAnimationState::None;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Extension", meta=(AllowPrivateAccess="true"))
	EValorHitReactType HitReactType = EValorHitReactType::None;

	UPROPERTY(BlueprintReadOnly, Category="Valor|Animation|Extension", meta=(AllowPrivateAccess="true"))
	EValorDeathAnimationState DeathAnimationState = EValorDeathAnimationState::Alive;

private:
	void CacheAnimationSources();
	void UpdateLocomotionData();
	void UpdateCombatData(float DeltaSeconds);
	void UpdateAimData();
	void UpdateExtensionData();
	EValorMovementState ResolveMovementStateFallback() const;
};

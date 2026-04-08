#pragma once

#include "Animation/ValorAnimTypes.h"
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ValorWeaponDataAsset.generated.h"

UENUM(BlueprintType)
enum class EValorWallPenetrationTier : uint8
{
	Low,
	Medium,
	High
};

UENUM(BlueprintType)
enum class EValorHitZone : uint8
{
	None,
	Body,
	Head,
	Leg
};

USTRUCT(BlueprintType)
struct FValorDamageRangeStep
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float MaxDistanceCm = 1500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float HeadDamage = 160.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float BodyDamage = 40.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float LegDamage = 34.0f;
};

USTRUCT(BlueprintType)
struct FValorRecoilStep
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float PitchKick = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float YawKick = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float AdditionalSpread = 0.0f;
};

USTRUCT(BlueprintType)
struct FValorWeaponConfig
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	FName WeaponId = TEXT("Rifle_Default");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	FText DisplayName;

	// 애니메이션 레이어가 어떤 무기군 포즈를 써야 하는지 분류한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	EValorWeaponAnimationType AnimationType = EValorWeaponAnimationType::Rifle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	bool bAutomatic = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	int32 MagazineSize = 25;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	int32 MaxReserveAmmo = 75;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float TimeBetweenShots = 0.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float ReloadDuration = 1.9f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float TraceDistanceCm = 50000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float ADSFieldOfView = 82.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float ADSInterpSpeed = 18.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float BaseFirstShotSpreadDegrees = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float AdditionalSpreadPerShotDegrees = 0.16f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float MaxSpreadDegrees = 4.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float SpreadRecoveryDelay = 0.18f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float MovementSpreadDegrees = 2.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float WalkingSpreadDegrees = 0.85f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float CrouchSpreadMultiplier = 0.75f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float ADSSpreadMultiplier = 0.65f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float CrouchRecoilMultiplier = 0.85f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float ADSRecoilMultiplier = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float RandomPitchDeviation = 0.22f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float RandomYawDeviation = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float PenetrationDepthCm = 45.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float PenetrationDamageMultiplier = 0.7f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	EValorWallPenetrationTier PenetrationTier = EValorWallPenetrationTier::Medium;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	TArray<FValorRecoilStep> RecoilPattern;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	TArray<FValorDamageRangeStep> DamageRanges;
};

UCLASS(BlueprintType)
class VALOR_API UValorWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	FValorWeaponConfig WeaponConfig;
};

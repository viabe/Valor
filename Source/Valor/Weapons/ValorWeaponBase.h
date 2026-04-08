#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"
#include "ValorWeaponBase.generated.h"

class AValorCharacter;
class USkeletalMeshComponent;
class USceneComponent;
class UValorWeaponDataAsset;

USTRUCT()
struct FValorComputedShotData
{
	GENERATED_BODY()

	UPROPERTY()
	int32 ShotIndex = 0;

	UPROPERTY()
	float SpreadAngleDegrees = 0.0f;

	UPROPERTY()
	FRotator RecoilKick = FRotator::ZeroRotator;

	UPROPERTY()
	uint32 RandomSeed = 0;
};

UCLASS()
class VALOR_API AValorWeaponBase : public AActor
{
	GENERATED_BODY()

public:
	AValorWeaponBase();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void OnEquippedBy(AValorCharacter* NewOwnerCharacter);
	void OnUnequipped();

	const FValorWeaponConfig& GetWeaponConfig() const;

	bool CanFire(float ServerWorldTimeSeconds) const;
	bool PrepareAndConsumeShot(float ServerWorldTimeSeconds, bool bIsADS, float MovementAlpha, bool bIsWalking, bool bIsCrouched, FValorComputedShotData& OutShotData);
	bool CanReload() const;
	void ReloadFromReserve();

	int32 GetCurrentMagazineAmmo() const { return CurrentMagazineAmmo; }
	int32 GetCurrentReserveAmmo() const { return CurrentReserveAmmo; }
	float GetReloadDuration() const;
	float GetShotInterval() const;
	bool IsAutomatic() const;
	float GetADSFieldOfView() const;
	float GetADSInterpSpeed() const;
	float GetTraceDistance() const;
	EValorWeaponAnimationType GetWeaponAnimationType() const;
	EValorWallPenetrationTier GetPenetrationTier() const;
	float GetPenetrationDepth() const;
	float GetPenetrationDamageMultiplier() const;

	FVector ApplySpreadToDirection(const FVector& AimDirection, const FValorComputedShotData& ShotData) const;
	float ComputeDamage(float DistanceCm, EValorHitZone HitZone) const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Valor|Weapon")
	USceneComponent* Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Valor|Weapon")
	USkeletalMeshComponent* WeaponMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	TObjectPtr<UValorWeaponDataAsset> WeaponDataAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	FValorWeaponConfig FallbackWeaponConfig;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Weapon")
	TObjectPtr<AValorCharacter> OwningCharacter;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Weapon")
	int32 CurrentMagazineAmmo = 0;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Weapon")
	int32 CurrentReserveAmmo = 0;

private:
	void InitializeFallbackConfig();
	void RefreshSprayState(float CurrentWorldTimeSeconds);

	float LastServerFireWorldTime = -1000.0f;
	int32 CurrentSprayShotCount = 0;
	uint32 WeaponRandomSeed = 1337u;
};

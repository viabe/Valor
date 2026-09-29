#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"
#include "ValorWeaponBase.generated.h"

class AValorCharacter;
class UAnimMontage;
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

	// 결정적 반동 패턴을 '탄착 오프셋'으로 적용하기 위한 누적 각도(도). 카메라가 아니라 탄 방향에 더해진다.
	// 발로란트처럼 조준점(시야)은 고정되고, 탄이 조준점 대비 위(+Pitch)/오른쪽(+Yaw)으로 이만큼 벌어진다.
	UPROPERTY()
	float RecoilPitchDegrees = 0.0f;

	UPROPERTY()
	float RecoilYawDegrees = 0.0f;

	// 이번 발의 반동 '증분'(도). 탄도에는 이미 위 누적값으로 반영됐고, 이 값은 화면에 짧게 튀는
	// 시각적 뷰 펀치(카메라 연출)의 크기를 정하는 데만 쓰인다.
	UPROPERTY()
	float RecoilStepPitchDegrees = 0.0f;

	UPROPERTY()
	float RecoilStepYawDegrees = 0.0f;

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
	UAnimMontage* GetFireMontage() const;
	float GetFireMontagePlayRate() const;
	float GetTraceDistance() const;
	EValorWeaponAnimationType GetWeaponAnimationType() const;
	EValorWallPenetrationTier GetPenetrationTier() const;
	float GetPenetrationDepth() const;
	float GetPenetrationDamageMultiplier() const;

	FVector ApplySpreadToDirection(const FVector& AimDirection, const FValorComputedShotData& ShotData) const;
	float ComputeDamage(float DistanceCm, EValorHitZone HitZone) const;

	// 런타임 확정 반동 프로파일(빈 패턴이면 기본 패턴이 주입된 상태). 뷰 펀치 파라미터 조회 등에 쓴다.
	const FValorRecoilProfile& GetRecoilProfile() const { return ResolvedRecoilProfile; }

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

	// 데이터 자산이 Pattern을 비워둔 경우, WeaponId에 맞는 코드 내장 기본 패턴을 채워 준다.
	// === 새 총기 확장 지점 ===
	// 새 무기를 추가할 때는 (1) 데이터 자산에서 RecoilProfile.Pattern을 직접 채우거나,
	//                      (2) 아래 함수에 WeaponId 분기와 BuildDefault***Pattern을 추가하면 된다.
	static void BuildDefaultPatternForWeapon(FName WeaponId, TArray<FValorRecoilStep>& OutPattern);

	// 발로란트 밴달의 결정적 스프레이 패턴 기본값이다(사진 기준: 긴 수직 줄기 + 상단 좌우 스윙).
	static void BuildDefaultVandalPattern(TArray<FValorRecoilStep>& OutPattern);

private:
	void InitializeFallbackConfig();
	void RefreshWeaponMeshAlignment();
	void RefreshSprayState(float CurrentWorldTimeSeconds);
	// 데이터 자산/폴백 설정으로부터 실제로 사용할 반동 프로파일을 확정한다(Pattern이 비면 WeaponId 기본 패턴 주입).
	void ResolveRecoilProfile();

	float LastServerFireWorldTime = -1000.0f;
	int32 CurrentSprayShotCount = 0;
	uint32 WeaponRandomSeed = 1337u;

	// 현재 스프레이에서 지금까지 누적된 반동 오프셋(도). 탄착이 조준점 대비 얼마나 벌어졌는지를 나타내며,
	// 사격을 멈춰 스프레이가 리셋되면 0으로 돌아간다. 카메라가 아니라 탄 방향(ApplySpreadToDirection)에만 쓰인다.
	float AccumulatedRecoilPitch = 0.0f;
	float AccumulatedRecoilYaw = 0.0f;

	// 런타임에서 실제로 참조하는 반동 프로파일이다. GetWeaponConfig()의 프로파일을 복사한 뒤
	// 패턴이 비어 있으면 기본 밴달 패턴을 채워 넣어 항상 유효한 상태를 유지한다.
	FValorRecoilProfile ResolvedRecoilProfile;
};

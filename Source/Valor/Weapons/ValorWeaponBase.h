#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"
#include "Weapons/ValorWeaponTypes.h"
#include "ValorWeaponBase.generated.h"

class AValorCharacter;
class UAnimMontage;
class USkeletalMeshComponent;
class USceneComponent;
class UValorWeaponDataAsset;

/**
 * 총기 액터(발로란트의 Equippable에 해당).
 *
 * 책임: 무기 데이터(PrimaryDataAsset) 보관, 탄약(서버 권위·복제), 스프레이 상태(반동/탄퍼짐) 보관과 계산 위임,
 *       발사 연출(트레이서/탄흔/총구 화염) 재생. 입력 처리·검증·피격 판정은 UValorCombatComponent가 맡는다.
 *
 * 네트워크:
 *  - 탄약은 서버만 바꾸고 복제한다.
 *  - 스프레이 상태는 복제하지 않는다. 서버 인스턴스(권위)와 소유 클라 인스턴스(예측)가 같은 발사 요청을 같은 순서로
 *    처리해 각자 같은 값을 계산한다(ValorSpray 결정적 시뮬레이션).
 *  - 예측 난수가 서버와 같아지도록 무기 시드(RecoilSeed)와 서버 누적 발사 수(ServerShotCount)만 소유자에게 복제한다.
 * 트레이드오프: 소유 클라가 시드를 알기 때문에 "탄퍼짐 예측 핵"이 이론상 가능하다. 발로란트도 클라 예측과 서버 판정의
 *   일치를 택했고(넷코드 블로그), 이는 안티치트(Vanguard) 영역으로 분리한다.
 */
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

	// === 사격(결정적 시뮬레이션) ===

	// 한 발을 계산하고 스프레이 상태를 한 발 전진시킨다. 서버(권위)와 소유 클라(예측)가 같은 입력으로 호출한다.
	FValorComputedShotData SimulateShot(double ShotTime, const FValorShooterStance& Stance);

	// 상태를 바꾸지 않는 "지금 쏜다면" 평가. ADS 카메라와 크로스헤어가 매 프레임 읽는다(로컬 전용).
	FValorSprayEvaluation EvaluateSpray(double Now, const FValorShooterStance& Stance) const;

	// 조준 방향 + 반동 + 탄퍼짐 샘플 → 최종 탄 방향.
	FVector ComputeShotDirection(const FRotator& AimRotation, const FValorComputedShotData& ShotData) const;

	float GetFireInterval(bool bIsADS) const;
	float GetCameraRecoilFollowRatio(bool bIsADS) const;
	const FValorCameraKickConfig& GetCameraKickConfig(bool bIsADS) const;

	// === 탄약 ===

	bool HasAmmo() const { return CurrentMagazineAmmo > 0; }

	// 서버 전용: 승인된 발사 1회만큼 탄약을 소모한다.
	void ConsumeAmmo();

	// 소유 클라 예측용 탄약: 서버가 아직 처리하지 않은(비행 중인) 예측 발 수를 뺀다.
	int32 GetPredictedMagazineAmmo() const;

	bool CanReload() const;
	void ReloadFromReserve();

	int32 GetCurrentMagazineAmmo() const { return CurrentMagazineAmmo; }
	int32 GetCurrentReserveAmmo() const { return CurrentReserveAmmo; }

	// === 연출(로컬 전용) ===

	// 트레이서/탄흔/총구 화염/사운드를 재생한다. 서버 판정과 무관하며 데디케이티드 서버에서는 아무것도 하지 않는다.
	void PlayFireEffects(const FVector& ImpactPoint, const FVector& ImpactNormal, bool bBlockingHit, bool bHitCharacter) const;

	FVector GetMuzzleLocation() const;

	// === 조회 ===

	float GetReloadDuration() const;
	bool IsAutomatic() const;
	float GetADSFieldOfView(float HipFieldOfView) const;
	float GetADSInterpSpeed() const;
	UAnimMontage* GetFireMontage() const;
	float GetFireMontagePlayRate() const;
	float GetTraceDistance() const;
	EValorWeaponAnimationType GetWeaponAnimationType() const;
	EValorWallPenetrationTier GetPenetrationTier() const;
	float GetPenetrationDepth() const;
	float GetPenetrationDamageMultiplier() const;
	float ComputeDamage(float DistanceCm, EValorHitZone HitZone) const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Valor|Weapon")
	USceneComponent* Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Valor|Weapon")
	USkeletalMeshComponent* WeaponMesh;

	// 무기 정의(발로란트의 AKPrimaryAsset 같은 PrimaryDataAsset). 비어 있으면 FallbackWeaponConfig(= 밴달 기본값)를 쓴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	TObjectPtr<UValorWeaponDataAsset> WeaponDataAsset;

	// 데이터 자산이 없을 때 쓰는 설정. 구조체 기본값이 밴달이므로 별도 초기화 없이도 밴달로 동작한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	FValorWeaponConfig FallbackWeaponConfig;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Weapon")
	TObjectPtr<AValorCharacter> OwningCharacter;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Weapon")
	int32 CurrentMagazineAmmo = 0;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Weapon")
	int32 CurrentReserveAmmo = 0;

	// 무기별 반동 난수 시드. 서버가 생성해 소유자에게만 복제한다(수평 방향 전환/탄퍼짐 예측을 서버와 일치시키기 위함).
	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Weapon")
	int32 RecoilSeed = 0;

	// 서버가 승인한 누적 발사 수. 소유 클라는 이 값으로 (1) 예측 탄약을 보정하고 (2) 새 스프레이 시작 시 난수 번호를 재동기화한다.
	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Weapon")
	int32 ServerShotCount = 0;

private:
	void RefreshWeaponMeshAlignment();

	// 반동/탄퍼짐 스프레이 상태(서버 = 권위, 소유 클라 = 예측). 복제하지 않는다.
	FValorSprayState SprayState;

	// 이 인스턴스가 계산한 누적 발사 수(= 다음 발의 난수 번호). 서버에서는 ServerShotCount와 같다.
	int32 ShotCounter = 0;
};

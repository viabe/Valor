#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"
#include "Weapons/ValorInventoryTypes.h"
#include "Weapons/ValorWeaponTypes.h"
#include "ValorWeaponBase.generated.h"

class AValorCharacter;
class UAnimMontage;
class UMaterialInterface;
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

	// 블루프린트 컴포넌트까지 만들어진 뒤 무기 충돌을 모두 끈다(ValorInventory::DisableHeldWeaponCollision 참고).
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// 서버의 부착 상태(AttachmentReplication)를 복제하지 않는다. 부착은 머신마다 다르다(ValorViewmodel::AttachHeldItemToOwnerView).
	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;

	void OnEquippedBy(AValorCharacter* NewOwnerCharacter);
	void OnUnequipped();

	// 이 머신에서 소유자 시점에 맞게 다시 붙인다(로컬 플레이어 = 1인칭 뷰모델, 그 외 = 3인칭 손).
	// 로컬 조종 여부는 컨트롤러 복제 뒤에야 확정되므로, 캐릭터가 빙의·컨트롤러 복제 때 다시 부른다.
	void RefreshViewAttachment(AValorCharacter* InViewOwnerCharacter);

	// 이 머신에서 1인칭 뷰모델로 보이는 중인가(로컬 연출 전용).
	bool IsInFirstPersonView() const { return bFirstPersonView; }

	const FValorWeaponConfig& GetWeaponConfig() const;

	// 1인칭 배치(데이터 에셋 Viewmodel)와 조준선 높이(cm). 조준선 높이는 설정이 0이면 메시 크기로 한 번 계산해 둔다.
	const FValorViewmodelConfig& GetViewmodelConfig() const { return GetWeaponConfig().Viewmodel; }
	float GetViewmodelSightHeightCm() const;

	// === 슬롯 / 장착 / 이동 속도 ===

	// 이 총이 들어가는 슬롯(권총 = 보조무기, 나머지 = 주무기).
	EValorWeaponSlot GetInventorySlot() const;

	// 꺼내서 쏠 수 있을 때까지의 시간(초). 데이터 에셋 EquipTimeSeconds(위키 "Equip Speeds ... (Normal)").
	float GetEquipTime() const;

	// 이 총을 들고 있을 때 이동 속도 배율(칼 6.75m/s 대비). 조준 중이면 ADS 배율(힙 대비 값)을 곱한다.
	// 예) 밴달 0.8 → 5.4m/s, 조준 0.8 × 0.76 → 4.1m/s(위키 "Move Speed 76% (4.104 m/sec)").
	float GetMoveSpeedMultiplier(bool bIsADS) const;

	// 집어넣기/꺼내기(로컬 연출 전용 숨김). 조준경 숨김과 합쳐 메시 가시성을 정하므로 서로 덮어쓰지 않는다.
	void SetHolstered(bool bNewHolstered);

	// 집어넣을 때 진행 중이던 점사를 끊는다. 서버와 예측 클라가 같은 발사 순서 위치에서 호출하므로(교체 RPC가 발사 RPC와
	// 같은 신뢰성 순서로 도착) 두 쪽의 "다음 발 간격" 계산이 계속 일치한다.
	void CancelBurst();

	// === 사격(결정적 시뮬레이션) ===

	// 한 발을 계산하고 스프레이 상태를 한 발 전진시킨다. 서버(권위)와 소유 클라(예측)가 같은 입력으로 호출한다.
	FValorComputedShotData SimulateShot(double ShotTime, const FValorShooterStance& Stance);

	// 상태를 바꾸지 않는 "지금 쏜다면" 평가. ADS 카메라와 크로스헤어가 매 프레임 읽는다(로컬 전용).
	FValorSprayEvaluation EvaluateSpray(double Now, const FValorShooterStance& Stance) const;

	// 조준 방향 + 반동 + 탄퍼짐 샘플 → 최종 탄 방향.
	FVector ComputeShotDirection(const FRotator& AimRotation, const FValorComputedShotData& ShotData) const;

	// 산탄 펠릿 하나의 방향(0번 = ComputeShotDirection). 발 시드 + 펠릿 번호라 서버/클라가 같은 산탄 모양을 만든다.
	FVector ComputePelletDirection(const FRotator& AimRotation, const FValorComputedShotData& ShotData, int32 PelletIndex) const;

	// 탄퍼짐 없이 반동만 적용한 방향(버키 캐니스터의 비행 방향).
	FVector ComputeRecoilDirection(const FRotator& AimRotation, const FValorComputedShotData& ShotData) const;

	float GetFireInterval(bool bIsADS) const;
	float GetCameraRecoilFollowRatio(bool bIsADS) const;
	const FValorCameraKickConfig& GetCameraKickConfig(bool bIsADS) const;

	// === 발사 간격(점사·발사 속도 가속) ===

	// 마지막 발 이후 다음 발까지 필요한 최소 간격(초). 서버는 연사 속도 검증에, 클라는 다음 발 예약에 쓴다.
	float GetNextShotCooldown() const { return SprayState.NextShotCooldown; }

	// 점사 도중인지(한 번 누른 점사는 버튼을 떼도 끝까지 쏜다).
	bool IsBurstInProgress() const { return SprayState.ShotsInBurst > 0; }

	// 마지막 발이 보조 발사 모드(ADS/우클릭 공격)였는지.
	bool WasLastShotAltMode() const { return SprayState.bLastShotAltMode; }

	// 이 무기의 가장 빠른 순간 발사 속도(점사 안 속도·가속 최고 속도 포함). 서버 수신 속도 제한에 쓴다.
	float GetMaxFireRate() const;

	// === 우클릭 ===

	EValorAltFireType GetAltFireType() const;

	// 우클릭이 조준이 아니라 "보조 공격"인 총인지(클래식 산탄, 버키 캐니스터).
	bool IsAltFireAttack() const;

	float GetAirBurstDistance() const;

	// === 탄약 ===

	bool HasAmmo() const { return CurrentMagazineAmmo > 0; }

	// 이 모드로 한 발 쏠 때 쓰는 탄 수(AmmoPerShot, 남은 탄이 모자라면 남은 만큼).
	int32 GetRoundsForShot(bool bAltMode, int32 AvailableRounds) const;

	// 이번 발의 펠릿 수. 탄을 덜 쓴 경우(클래식 우클릭을 1~2발 남기고 쏜 경우) 펠릿도 그 비율로 줄어든다.
	int32 GetPelletCountForShot(bool bAltMode, int32 RoundsUsed) const;

	// 서버 전용: 승인된 발사 1회만큼 탄약을 소모하고, 실제로 쓴 탄 수를 돌려준다.
	int32 ConsumeAmmoForShot(bool bAltMode);

	// 소유 클라 전용: 방금 예측한 발이 쓴 탄 수를 기록한다(예측 탄약 계산용).
	void NotePredictedShotRounds(int32 Rounds);

	// 소유 클라 예측용 탄약: 서버가 아직 처리하지 않은(비행 중인) 예측 발들이 쓴 탄 수를 뺀다.
	int32 GetPredictedMagazineAmmo() const;

	bool CanReload() const;
	void ReloadFromReserve();

	int32 GetCurrentMagazineAmmo() const { return CurrentMagazineAmmo; }
	int32 GetCurrentReserveAmmo() const { return CurrentReserveAmmo; }

	// 서버 전용: 바닥에 떨어졌던 총을 다시 주웠을 때 떨어뜨릴 때의 탄약으로 되돌린다.
	// 발로란트 규칙: 바닥의 총은 탄창·예비 탄약을 그대로 가진다(반쯤 쏜 총을 주우면 반만 남아 있다).
	void RestoreAmmoState(int32 MagazineAmmo, int32 ReserveAmmo);

	// 바닥 픽업이 같은 메시를 보여 줄 수 있게 노출한다(클래스 기본 객체에서도 읽는다).
	USkeletalMeshComponent* GetWeaponMesh() const { return WeaponMesh; }

	// === 연출(로컬 전용) ===

	// 총구 화염·발사음은 한 번, 트레이서·탄착 이펙트·탄흔은 탄착(펠릿)마다 재생한다.
	// 서버 판정과 무관하며 데디케이티드 서버에서는 아무것도 하지 않는다.
	void PlayFireEffects(const FValorShotEffects& ShotEffects) const;

	FVector GetMuzzleLocation() const;

	// === 줌/조준경 ===

	// 조준 단계 수: 0 = 조준 없음(우클릭이 공격이거나 없음), 1 = 1단, 2 = 2단 줌(오퍼레이터).
	int32 GetMaxZoomLevel() const;

	// 줌 단계별 화면 FOV. 1단 = ADSZoomMultiplier, 2단 = SecondaryADSZoomMultiplier.
	float GetZoomFieldOfView(float HipFieldOfView, int32 ZoomLevel) const;

	bool UsesScopeOverlay() const;
	UMaterialInterface* GetScopeOverlayMaterial() const;
	bool ShouldUnscopeAfterShot() const;
	bool IsSniper() const;

	// 조준을 시작한 뒤 쏠 수 있을 때까지의 지연(초, 불독·스팅어 점사 모드).
	float GetADSFireDelay() const;

	// 로컬 전용: 조준경 화면일 때 1인칭 총 메시를 숨긴다(가시성은 복제되지 않으므로 다른 플레이어 화면에는 영향 없음).
	void SetScopedViewHidden(bool bHideForScope);

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

	// 메시 가시성 = 손에 들고 있고(집어넣지 않음) 조준경 화면이 아님.
	void RefreshMeshVisibility();

	// 3인칭 조준 정렬(남이 보는 총의 총구를 그 캐릭터의 실제 조준점으로 돌림). 원격 캐릭터를 그리는 머신에서만 매 프레임 실행한다.
	// 왜: 3인칭 총 방향은 애니메이션 손 자세가 정한다. 그런데 손 자세는 조준 피치(복제 시 1.4도 단위)와 정확히 맞지 않고,
	//   총은 눈보다 약 40cm 아래(가슴 앞)에 있어 시선과 평행해도 머리를 겨눈 총이 머리 아래를 가리킨다.
	//   손잡이를 축으로 총만 살짝 돌려 총구가 시선이 닿는 지점을 향하게 한다. 판정과 무관한 연출이다.
	// 트레이드오프: 총이 손바닥 안에서 몇 도 돌아간다(왼손 IK가 생기면 왼손이 총을 따라간다).
	//   재장전처럼 자세가 조준과 크게 다를 때는 보정을 서서히 끈다(억지로 돌리면 더 어색하다).
	void UpdateThirdPersonAim(float DeltaSeconds);
	void RefreshThirdPersonAimTick();

	bool bHolstered = false;
	bool bScopeViewHidden = false;
	bool bFirstPersonView = false;

	// 지금 이 머신에서 붙어 있는 캐릭터(부착 기준). OwningCharacter는 복제가 늦을 수 있어 부착 때 받은 값을 따로 둔다.
	TWeakObjectPtr<AValorCharacter> ViewOwnerCharacter;

	// 3인칭 조준 정렬의 수렴 거리(cm). 시선 트레이스 결과를 부드럽게 따라간다.
	float ThirdPersonAimDistance = 2000.0f;

	// 머리를 겨눌 때 조준점을 겨눠진 사람의 눈으로 옮기는 양(월드, 부드럽게 보간).
	FVector ThirdPersonAimEyeOffset = FVector::ZeroVector;

	// 자동 조준선 높이 계산 결과(음수 = 아직 계산 안 함). 메시가 바뀌지 않으므로 한 번만 계산한다.
	mutable float CachedSightHeightCm = -1.0f;

	// 반동/탄퍼짐 스프레이 상태(서버 = 권위, 소유 클라 = 예측). 복제하지 않는다.
	FValorSprayState SprayState;

	// 이 인스턴스가 계산한 누적 발사 수(= 다음 발의 난수 번호). 서버에서는 ServerShotCount와 같다.
	int32 ShotCounter = 0;

	// 소유 클라: 최근 예측 발들이 쓴 탄 수(발 번호 % 크기로 순환). 한 발에 여러 탄을 쓰는 클래식 우클릭 때문에 필요하다.
	static constexpr int32 PredictedRoundsHistorySize = 32;
	uint8 PredictedRoundsByShot[PredictedRoundsHistorySize];
};

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "Templates/Function.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"
#include "Weapons/ValorWeaponTypes.h"
#include "ValorCombatComponent.generated.h"

class AValorCharacter;
class AValorWeaponBase;
class AValorWeaponPickup;
class UGameplayAbility;
class UValorAbilitySystemComponent;
class UValorCombatAttributeSet;

// 서버 히트스캔 결과(서버 내부 전용, 복제하지 않음).
struct FValorHitScanResult
{
	AValorCharacter* HitCharacter = nullptr;
	EValorHitZone HitZone = EValorHitZone::None;
	FVector ImpactPoint = FVector::ZeroVector;
	FVector ImpactNormal = FVector::UpVector;
	float TravelDistance = 0.0f;
	bool bBlockingHit = false;
	bool bPenetratedSurface = false;
};

/**
 * 전투 컴포넌트: 사격/재장전/ADS/줍기·교체의 입력 → 서버 요청 → 서버 판정 → 연출 흐름을 담당한다.
 *
 * 줍기·교체 흐름(서버 권위): F 입력 → ServerInteractWithPickup → 서버가 조준선으로 픽업을 고른다 → 새 총 스폰 →
 *  들고 있던 총은 탄약을 가진 채 바닥 픽업으로 떨어뜨림 → 새 총 장착(복제된 EquippedWeapon으로 클라에 반영).
 *
 * 사격 흐름(발로란트 넷코드 원칙: 클라 예측 + 서버 권위 + 발사 시각 기준 되감기):
 *  1) 소유 클라가 발사 입력을 받으면 로컬 발사 루프(연사 속도 타이머)가 한 발 요청(시각 + 조준 방향)을 만든다.
 *  2) 클라는 그 요청으로 반동/탄퍼짐을 "예측" 계산해 트레이서·탄흔·ADS 카메라·크로스헤어를 즉시 갱신한다(연출 전용).
 *  3) 같은 요청을 ServerFireShot(신뢰성 RPC = 반드시 도착·순서 보장)으로 보낸다.
 *  4) 서버는 요청을 검증(시각 범위, 연사 속도, 수신 속도 제한, 탄약, 조준 정합성)한 뒤 GAS 발사 어빌리티를 활성화하고,
 *     "자기 스프레이 상태"로 반동/탄퍼짐을 다시 계산해 랙 보상 히트스캔과 피해를 확정한다. 클라가 계산한 값은 쓰지 않는다.
 *  5) 다른 클라에게는 비신뢰 멀티캐스트로 연출만 보낸다(사수 본인은 이미 예측으로 재생했으므로 건너뜀).
 *
 * 트레이드오프:
 *  - 서버는 클라가 주장한 발사 시각을 반동 회복 계산에 사용한다(결정성). 대신 범위/간격/실수신 속도를 검증해 조작 여지를 막는다.
 *  - 예측과 서버 결과가 어긋나는 경우(패킷 거부 등)는 다음 스프레이 시작 시 난수 번호를 서버 값으로 맞춰 자동 복구된다.
 */
UCLASS(ClassGroup=(Valor), meta=(BlueprintSpawnableComponent))
class VALOR_API UValorCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UValorCombatComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitializeAbilityBindings(UValorAbilitySystemComponent* InAbilitySystemComponent);

	void HandleFireInputPressed();
	void HandleFireInputReleased();
	void HandleReloadInputPressed();
	void HandleADSInputPressed();
	void HandleADSInputReleased();
	void HandleInteractInputPressed();

	// GA_WeaponFire(서버 전용 어빌리티)가 활성화되면 호출된다. 대기 중인 발사 요청 1개를 권위적으로 처리한다.
	void ExecuteServerFireAbility();
	void ExecuteServerReloadAbility();
	void SetADSStateFromAbility(bool bNewADS);

	UFUNCTION(BlueprintPure, Category="Valor|Combat")
	AValorWeaponBase* GetEquippedWeapon() const { return EquippedWeapon; }

	bool IsADSActive() const { return bIsADS; }

	UFUNCTION(BlueprintPure, Category="Valor|Combat")
	bool IsReloading() const { return bIsReloading; }

	bool IsFireInputHeld() const { return bLocalFireHeld || bLocalAltFireHeld; }
	float GetLastFireSimulationWorldTime() const { return LastFireSimulationWorldTime; }

	// === UI(UMG 위젯)에서 읽는 값: 로컬 플레이어 기준 ===

	// 탄창/예비 탄약. 소유 클라는 서버 복제를 기다리지 않도록 "예측 탄약"(비행 중인 발 제외)을 돌려준다.
	UFUNCTION(BlueprintPure, Category="Valor|Combat|UI")
	int32 GetDisplayedMagazineAmmo() const;

	UFUNCTION(BlueprintPure, Category="Valor|Combat|UI")
	int32 GetDisplayedReserveAmmo() const;

	UFUNCTION(BlueprintPure, Category="Valor|Combat|UI")
	FText GetEquippedWeaponDisplayName() const;

	// 현재 줌 단계: 0 = 비조준, 1 = 1단, 2 = 2단(오퍼레이터 5배).
	UFUNCTION(BlueprintPure, Category="Valor|Combat|UI")
	int32 GetZoomLevel() const;

	// 조준경 화면(저격총 조준 중)인지. true면 HUD가 조준경을 그리고 일반 크로스헤어/무기 UI를 숨기면 된다.
	UFUNCTION(BlueprintPure, Category="Valor|Combat|UI")
	bool IsScopeOverlayActive() const;

	// 지금 F로 주울 수 있는 총(줍기 안내 UI용). 서버 판정과 같은 규칙(조준선·거리·시야)을 로컬에서 계산한다.
	UFUNCTION(BlueprintPure, Category="Valor|Combat|UI")
	AValorWeaponPickup* GetPickupInView() const { return FindPickupInView(); }

	// 게임플레이 판단에 쓸 ADS 여부. 소유 클라는 RPC 왕복을 기다리지 않고 입력 의도로 예측하고, 서버는 복제 상태를 쓴다.
	bool IsADSForGameplay() const;

	// 사격 자세 스냅샷(서버/클라 공용). 반동/탄퍼짐 배율 계산의 입력이다.
	FValorShooterStance BuildShooterStance() const;

	// 서버 동기화 시간(초). 발사 시각, 스프레이 회복, 랙 보상이 모두 이 시간축을 쓴다.
	double GetSynchronizedTime() const;

	// 로컬 표시용: 현재 스프레이 평가(크로스헤어 탄퍼짐, 디버그 표시). 무기가 없으면 false.
	bool GetCurrentSprayEvaluation(FValorSprayEvaluation& OutEvaluation, FValorShooterStance& OutStance) const;

	// 로컬 표시용: 카메라가 따라가야 할 반동 오프셋(= 반동 × 현재 모드의 CameraRecoilFollowRatio). 힙은 0, ADS는 반동 그대로.
	FRotator GetCameraRecoilOffset() const;

protected:
	UPROPERTY(ReplicatedUsing=OnRep_EquippedWeapon, VisibleInstanceOnly, Category="Valor|Combat")
	TObjectPtr<AValorWeaponBase> EquippedWeapon;

	UPROPERTY(ReplicatedUsing=OnRep_IsADS, VisibleInstanceOnly, Category="Valor|Combat")
	bool bIsADS = false;

	UPROPERTY(ReplicatedUsing=OnRep_IsReloading, VisibleInstanceOnly, Category="Valor|Combat")
	bool bIsReloading = false;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat")
	TSubclassOf<UGameplayAbility> FireAbilityClass;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat")
	TSubclassOf<UGameplayAbility> ReloadAbilityClass;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat")
	TSubclassOf<UGameplayAbility> ADSAbilityClass;

	// === 서버 발사 검증(안티치트) ===

	// 과거로 되감을 수 있는 최대 시간(초). 랙 보상 스냅샷 보관 시간과 맞춘다. Riot: "put some limits on how far the server is willing to rewind".
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Validation", meta=(ClampMin="0.0"))
	float MaxShotRewindSeconds = 0.25f;

	// 클라 시계 오차로 발사 시각이 서버보다 약간 앞설 수 있는 허용치(초). 넘으면 서버 현재 시각으로 끌어내린다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Validation", meta=(ClampMin="0.0"))
	float MaxShotTimeLeadSeconds = 0.05f;

	// 주장된 발사 시각 간격이 (발사 간격 × 이 값)보다 짧으면 연사 속도 위반으로 거부한다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Validation", meta=(ClampMin="0.1", ClampMax="1.0"))
	float FireIntervalTolerance = 0.9f;

	// 실제 수신 기준 속도 제한(토큰 버킷)의 최대 누적 토큰. 패킷이 몰려 도착하는 지터를 흡수한다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Validation", meta=(ClampMin="1.0"))
	float ShotRateBurstCapacity = 3.0f;

	// 클라 조준과 서버가 아는 조준의 최대 허용 차이(도). 넘으면 서버 조준으로 대체한다(빠른 플릭을 고려해 넉넉하게).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Validation", meta=(ClampMin="1.0"))
	float MaxAimDivergenceDegrees = 45.0f;

	// === 줍기 / 교체 / 떨어뜨리기 ===

	// 줍기 판정 거리(cm, 눈 기준 조준선 방향). 조준선이 픽업의 줍기 반경 안을 지나가야 후보가 된다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Pickup", meta=(ClampMin="50.0"))
	float PickupInteractDistance = 350.0f;

	// 떨어뜨린 총을 나타낼 픽업 클래스. 표시 메시가 비어 있는 클래스여야 총마다 자기 메시로 보인다(기본: C++ 픽업).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Pickup")
	TSubclassOf<AValorWeaponPickup> DroppedWeaponPickupClass;

	// 총을 떨어뜨리는 위치: 캐릭터 앞 이 거리(cm)의 바닥.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Pickup", meta=(ClampMin="0.0"))
	float DropForwardDistance = 100.0f;

	// 앞에 벽이 있으면 벽에서 이만큼(cm) 띄워 내려놓는다(총이 벽에 박히지 않게).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Pickup", meta=(ClampMin="0.0"))
	float DropWallClearance = 30.0f;

	// 떨어뜨릴 바닥을 아래로 찾는 최대 깊이(cm). 못 찾으면 캐릭터 발밑에 둔다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Pickup", meta=(ClampMin="10.0"))
	float DropGroundSearchDepth = 400.0f;

	// === 조준 입력 방식(발로란트 설정 메뉴의 항목들. 나중에 설정 UI/세이브 데이터로 옮길 값) ===

	// 저격총 조준: true = 토글(발로란트 기본). 우클릭할 때마다 1단 → 2단(오퍼레이터) → 해제. false = 누르고 있는 동안 1단.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Aim")
	bool bToggleSniperZoom = true;

	// 그 외 총의 ADS: false = 누르고 있는 동안(기본), true = 토글.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Aim")
	bool bToggleADS = false;

	// 쏘면 조준이 풀리는 저격총(오퍼레이터·마샬)을 장전이 끝나는 대로 다시 조준한다(발로란트 "자동 재조준" 설정).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Aim")
	bool bAutoReScopeAfterShot = false;

	// 발사 요청 1발. 신뢰성 RPC로 보내 유실/순서 뒤바뀜이 없게 한다(유실되면 서버/클라 스프레이 진행도가 한 발 어긋남).
	UFUNCTION(Server, Reliable)
	void ServerFireShot(const FValorShotRequest& Request);

	UFUNCTION(Server, Reliable)
	void ServerRequestReload();

	UFUNCTION(Server, Reliable)
	void ServerSetADSInput(bool bNewADS);

	UFUNCTION(Server, Reliable)
	void ServerInteractWithPickup();

	// 다른 클라용 발사 연출(트레이서/탄흔/발사 몽타주, 산탄총은 펠릿별 탄착). 연출은 한 발 놓쳐도 게임에 영향이 없으므로 비신뢰로 보낸다.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSimulateFire(const FValorShotEffects& ShotEffects);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayReloadCue();

	UFUNCTION()
	void OnRep_EquippedWeapon(AValorWeaponBase* PreviousWeapon);

	UFUNCTION()
	void OnRep_IsADS();

	UFUNCTION()
	void OnRep_IsReloading();

private:
	// 로컬 발사 루프: 입력 유지 중 연사 속도에 맞춰 한 발씩 요청을 만든다(소유 클라/리슨 호스트 전용).
	void TryFireLocalShot();

	// 발사 요청을 GAS 발사 어빌리티로 넘긴다(서버 전용). 어빌리티가 ExecuteServerFireAbility를 호출한다.
	void SubmitShotToFireAbility(const FValorShotRequest& Request);

	// 원격 소유 클라 전용: 서버와 같은 계산으로 이번 발을 예측하고 연출을 즉시 재생한다(피해 판정 없음).
	void PredictLocalShot(const FValorShotRequest& Request);

	// 서버 검증. 통과하면 요청을 정규화(시각 보정, 조준 정리)해서 true를 반환한다.
	bool ValidateShotRequest(FValorShotRequest& InOutRequest);

	// 거부 사유를 기록한다. bSuspicious면 경고(연사 속도 위반 등 조작 의심), 아니면 상세 로그(탄창 끝 예측 초과 등 정상 상황).
	void RejectShot(const TCHAR* Reason, bool bSuspicious);

	// 로컬 사수(예측 클라 또는 리슨 호스트)의 발사 연출.
	void PlayLocalShotPresentation(const FValorShotEffects& ShotEffects);

	// 이번 발의 발사 모드(true = AltFire 수치). 우클릭이 공격인 총은 요청의 버튼 정보, 조준 총은 조준 상태로 정한다.
	bool ResolveShotAltMode(const FValorShotRequest& Request) const;

	// 한 발의 탄 궤적을 순서대로 추적한다: 일반 1발 / 산탄 N펠릿 / 버키 캐니스터(폭발 전 명중 or 폭발 후 N펠릿).
	// TraceRay(시작, 방향, 최대 거리, 피해 거리 보정, 탄착 기록) → 무언가에 맞았으면 true. 서버(판정)와 예측 클라(연출)가 같이 쓴다.
	void TraceShot(const FValorShotRequest& Request, const FValorComputedShotData& ShotData, bool bAltMode, int32 PelletCount,
		const FVector& TraceStart, FValorShotEffects& OutEffects,
		TFunctionRef<bool(const FVector& RayStart, const FVector& RayDirection, float MaxDistance, float DamageDistanceOffset, FValorShotImpact& OutImpact)> TraceRay) const;

	// 쏘면 조준이 풀리는 저격총(오퍼레이터·마샬): 서버는 조준 어빌리티를 끝내고, 로컬 사수는 줌을 풀고 (설정 시) 재조준을 예약한다.
	void HandleUnscopeAfterShot();
	void ReScopeAfterShot();

	// 로컬 줌 단계를 바꾼다(소유 클라/리슨 호스트). 조준 on/off가 바뀔 때만 서버에 알린다(1단↔2단은 화면만 바뀜).
	void SetLocalZoomLevel(int32 NewZoomLevel);

	// 서버 전용: 새 총을 장착한다. 이미 들고 있던 총은 DropEquippedWeapon으로 바닥에 떨어뜨린다(발로란트식 교체).
	void EquipWeapon(AValorWeaponBase* NewWeapon);

	// 서버 전용: 들고 있는 총을 바닥 픽업으로 바꿔 내려놓는다(총 종류·탄약 유지).
	// 교체에서 쓰고, 이후 사망 시 드롭·버리기 키도 같은 함수를 쓰면 된다.
	void DropEquippedWeapon();

	// 서버 전용: 떨어뜨릴 위치와 방향(캐릭터 앞 바닥, 벽 앞에서 멈춤).
	FTransform ComputeDropTransform() const;

	void ApplyEquippedWeaponAttachment() const;
	void RefreshADSOnLocalClient() const;
	void FinishReload();

	// 서버 전용: 조준선에 가장 가까운 줍기 가능한 픽업(거리·줍기 반경·시야 확인). 없으면 nullptr.
	AValorWeaponPickup* FindPickupInView() const;

	void PerformServerHitScan(const FVector& TraceStart, const FVector& ShotDirection, float MaxDistance, float ClientShotTimestampSeconds, FValorHitScanResult& OutResult) const;

	UPROPERTY()
	TObjectPtr<AValorCharacter> OwnerCharacter;

	UPROPERTY()
	TObjectPtr<UValorAbilitySystemComponent> AbilitySystemComponent;

	FGameplayAbilitySpecHandle FireAbilityHandle;
	FGameplayAbilitySpecHandle ReloadAbilityHandle;
	FGameplayAbilitySpecHandle ADSAbilityHandle;

	FTimerHandle LocalFireTimerHandle;
	FTimerHandle ReloadTimerHandle;

	// 로컬 입력 상태(소유 클라).
	bool bLocalFireHeld = false;

	// 조준 의도(= 로컬 줌 단계 > 0). 예측 클라는 서버 승인을 기다리지 않고 이 값으로 조준 여부를 예측한다.
	bool bLocalADSIntent = false;

	// 로컬 줌 단계(0 = 비조준, 1 = 1단, 2 = 2단). 화면 FOV와 조준경 UI에만 쓰는 값이라 복제하지 않는다.
	int32 LocalZoomLevel = 0;

	// 우클릭 버튼이 눌려 있는지(누르고 있는 동안 조준/자동 재조준 판단용).
	bool bLocalADSButtonHeld = false;

	// 우클릭 공격(클래식 산탄·버키 캐니스터) 버튼 유지, 발사 루프가 지금 우클릭 공격을 쏘는 중인지.
	bool bLocalAltFireHeld = false;
	bool bLocalFiringAltAttack = false;

	// 자동 재조준(쏘면 풀리는 저격총) 예약과 돌아갈 줌 단계.
	FTimerHandle ReScopeTimerHandle;
	int32 PendingReScopeZoomLevel = 0;

	double LastLocalShotTime = -1000.0;

	// 연사 속도보다 빠른 탭이 "가장 이른 발사 가능 시각"으로 예약돼 있는지(버튼을 떼도 이 한 발은 쏜다).
	bool bFireShotBuffered = false;

	// 서버: GAS 어빌리티로 넘길 대기 중 발사 요청.
	FValorShotRequest PendingShotRequest;
	bool bHasPendingShotRequest = false;

	// 서버: 검증 상태.
	double LastAcceptedShotTime = -1000.0;
	double ShotRateTokens = 0.0;
	double LastShotRateRefillTime = -1000.0;
	int32 RejectedShotCount = 0;

	// 애니메이션 인스턴스가 발사 몽타주 타이밍을 잡는 기준(로컬 월드 시간).
	float LastFireSimulationWorldTime = -1000.0f;
};

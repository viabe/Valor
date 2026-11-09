#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "Templates/Function.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"
#include "Weapons/ValorInventoryTypes.h"
#include "Weapons/ValorWeaponTypes.h"
#include "ValorCombatComponent.generated.h"

class AValorCharacter;
class AValorMeleeWeapon;
class AValorWeaponBase;
class AValorWeaponPickup;
class UGameplayAbility;
class UValorAbilitySystemComponent;
class UValorCombatAttributeSet;

// 한 발이 한 대상에게 준 피해(펠릿 합산). 서버 내부 전용.
struct FValorPendingTargetDamage
{
	float Damage = 0.0f;
	EValorHitZone HitZone = EValorHitZone::None;
	bool bPenetrated = false;
};

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
 * 무기 슬롯(발로란트 1 주무기 / 2 보조무기 / 3 칼):
 *  - 슬롯별 총(PrimaryWeapon/SecondaryWeapon)과 칼(MeleeWeapon)은 서버가 스폰·소유하고 복제한다. 손에 든 슬롯과 장착 시작 시각은
 *    FValorEquipState 하나로 복제한다. EquippedWeapon은 복제하지 않는 "지금 든 총" 캐시다(칼이면 nullptr).
 *  - 교체(1/2/3·휠): 소유 클라가 즉시 자기 사본을 바꿔 예측하고(총 보이기/숨기기, 이동 속도, 장착 타이머) 요청 번호와 요청 시각을
 *    서버에 보낸다. 서버는 요청 시각을 되감기 한도 안으로 잘라 장착 시작 시각으로 쓰고, 처리한 요청 번호를 복제해 돌려준다.
 *    클라는 자기 마지막 요청까지 처리된 상태만 받아들인다(FValorEquipState 설명 참고).
 *  - 장착 시간: 장착 시작 + 무기의 EquipTime이 지나기 전에는 발사·조준·재장전이 안 된다. 서버는 발사 요청의 발사 시각을
 *    장착 완료 시각과 비교해 거부한다(발사 시각 검증과 같은 시간축이라 클라 예측과 판정이 일치한다).
 *  - 이동 속도: 든 무기·조준 상태의 배율을 UValorCharacterMovementComponent가 이동 예측에 실어 서버로 보낸다(그쪽 설명 참고).
 *
 * 줍기·교체 흐름(서버 권위): F 입력 → ServerInteractWithPickup → 서버가 조준선으로 픽업을 고른다 → 새 총 스폰 →
 *  같은 슬롯에 있던 총은 탄약을 가진 채 바닥 픽업으로 떨어뜨림 → 새 총을 그 슬롯에 넣고 꺼낸다(장착 시간 적용).
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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitializeAbilityBindings(UValorAbilitySystemComponent* InAbilitySystemComponent);

	void HandleFireInputPressed();
	void HandleFireInputReleased();
	void HandleReloadInputPressed();
	void HandleADSInputPressed();
	void HandleADSInputReleased();
	void HandleInteractInputPressed();

	// 1/2/3: 그 슬롯을 꺼낸다(비어 있거나 이미 든 슬롯이면 무시). 소유 클라는 즉시 예측하고 서버에 요청한다.
	void HandleEquipSlotInput(EValorWeaponSlot Slot);

	// 휠: 다음(+1)/이전(-1) 슬롯으로 순환(빈 슬롯 건너뜀).
	void HandleCycleWeaponInput(int32 Direction);

	// G: 손에 든 총을 바닥에 버린다(칼은 버릴 수 없다). 픽업 스폰이 필요하므로 예측하지 않고 서버가 처리한다.
	void HandleDropWeaponInput();

	// GA_WeaponFire(서버 전용 어빌리티)가 활성화되면 호출된다. 대기 중인 발사 요청 1개를 권위적으로 처리한다.
	void ExecuteServerFireAbility();
	void ExecuteServerReloadAbility();
	void SetADSStateFromAbility(bool bNewADS);

	// 소유 캐릭터가 죽었을 때 호출한다(서버: 캐릭터의 사망 처리, 클라: OnRep_IsDead).
	// 모든 머신: 발사 루프·조준 상태 정리. 서버: 재장전 취소 + 가장 좋은 총 하나(주무기, 없으면 보조무기)를 바닥에 떨어뜨리고
	// 나머지 총과 칼은 없앤다(발로란트: 시체 옆에는 총 한 자루만 남는다).
	void HandleOwnerDeath();

	// 서버 전용: 무기 클래스를 스폰해 맞는 슬롯에 넣는다(스폰 시 기본 무기). 같은 슬롯의 총은 교체 규칙대로 바닥에 떨어진다.
	// bMakeActive = 바로 꺼내 든다. bInstantEquip = 장착 시간 없이 바로 쏠 수 있다(스폰 지급용).
	void GiveWeapon(TSubclassOf<AValorWeaponBase> WeaponClass, bool bMakeActive = true, bool bInstantEquip = false);

	// 지금 든 총. 칼을 들고 있거나 빈 손이면 nullptr. 소유 클라는 교체 예측이 반영된 값이다.
	UFUNCTION(BlueprintPure, Category="Valor|Combat")
	AValorWeaponBase* GetEquippedWeapon() const { return EquippedWeapon; }

	// === 슬롯 조회(HUD/애니메이션) ===

	// 슬롯의 총(칼 슬롯은 항상 nullptr — 칼은 GetMeleeWeapon).
	UFUNCTION(BlueprintPure, Category="Valor|Combat|Inventory")
	AValorWeaponBase* GetWeaponInSlot(EValorWeaponSlot Slot) const;

	UFUNCTION(BlueprintPure, Category="Valor|Combat|Inventory")
	AValorMeleeWeapon* GetMeleeWeapon() const { return MeleeWeapon; }

	// 지금 든 슬롯(소유 클라는 예측값).
	UFUNCTION(BlueprintPure, Category="Valor|Combat|Inventory")
	EValorWeaponSlot GetActiveSlot() const { return GetEffectiveEquipState().ActiveSlot; }

	// 장착이 끝나 쏠 수 있는가(서버는 서버 시각, 소유 클라는 예측 상태 + 동기화 시간 기준).
	UFUNCTION(BlueprintPure, Category="Valor|Combat|Inventory")
	bool IsEquipReady() const;

	// 장착 진행도 0~1(HUD 표시용).
	UFUNCTION(BlueprintPure, Category="Valor|Combat|Inventory")
	float GetEquipProgress() const;

	// 슬롯에 표시할 이름(총 이름 또는 칼 이름). 빈 슬롯이면 빈 텍스트.
	FText GetSlotDisplayName(EValorWeaponSlot Slot) const;

	// 지금 장비 상태의 이동 속도 배율(칼 6.75m/s 대비: 든 무기 × 조준 배율). 이동 컴포넌트와 정확도 계산이 읽는다.
	// 소유 클라는 교체·조준 예측이 반영되고, 서버는 서버 권위 상태다.
	float GetEquipMoveSpeedMultiplier() const;

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
	// === 인벤토리(서버 권위, 모두에게 복제) ===
	// 다른 플레이어 화면에서도 든 총을 보여 주고 애니메이션 무기군을 고르려면 슬롯 무기와 장착 상태가 필요하다.
	// 무기 액터 자체는 소유자 관련성으로 캐릭터와 함께 복제되고, 여기서는 참조(NetGUID)만 보낸다.

	UPROPERTY(ReplicatedUsing=OnRep_PrimaryWeapon, VisibleInstanceOnly, Category="Valor|Combat|Inventory")
	TObjectPtr<AValorWeaponBase> PrimaryWeapon;

	UPROPERTY(ReplicatedUsing=OnRep_SecondaryWeapon, VisibleInstanceOnly, Category="Valor|Combat|Inventory")
	TObjectPtr<AValorWeaponBase> SecondaryWeapon;

	UPROPERTY(ReplicatedUsing=OnRep_MeleeWeapon, VisibleInstanceOnly, Category="Valor|Combat|Inventory")
	TObjectPtr<AValorMeleeWeapon> MeleeWeapon;

	UPROPERTY(ReplicatedUsing=OnRep_EquipState, VisibleInstanceOnly, Category="Valor|Combat|Inventory")
	FValorEquipState EquipState;

	// 모든 캐릭터가 갖는 칼 클래스.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Inventory")
	TSubclassOf<AValorMeleeWeapon> MeleeWeaponClass;

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
	// 발로란트 기본값은 둘 다 토글이다: 우클릭 한 번으로 조준(불독·스팅어는 점사 모드)에 들어가고, 한 번 더 누르면 풀린다.

	// 저격총 조준: true = 토글(기본). 우클릭할 때마다 1단 → 2단(오퍼레이터) → 해제. false = 누르고 있는 동안 1단.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Aim")
	bool bToggleSniperZoom = true;

	// 그 외 총의 ADS: true = 토글(기본), false = 누르고 있는 동안만 조준.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat|Aim")
	bool bToggleADS = true;

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

	// 슬롯 교체 요청. 신뢰성: 발사 RPC와 같은 순서로 처리돼야 "교체 전에 쏜 발은 옛 총으로, 뒤에 쏜 발은 새 총으로" 판정된다.
	// ClientRequestTime = 누른 순간의 동기화 시간(장착 시작 시각 주장), RequestId = 예측 조정용 요청 번호.
	UFUNCTION(Server, Reliable)
	void ServerRequestEquipSlot(EValorWeaponSlot Slot, float ClientRequestTime, uint8 RequestId);

	UFUNCTION(Server, Reliable)
	void ServerDropActiveWeapon();

	// 다른 클라용 발사 연출(트레이서/탄흔/발사 몽타주, 산탄총은 펠릿별 탄착). 연출은 한 발 놓쳐도 게임에 영향이 없으므로 비신뢰로 보낸다.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSimulateFire(const FValorShotEffects& ShotEffects);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayReloadCue();

	UFUNCTION()
	void OnRep_PrimaryWeapon(AValorWeaponBase* PreviousWeapon);

	UFUNCTION()
	void OnRep_SecondaryWeapon(AValorWeaponBase* PreviousWeapon);

	UFUNCTION()
	void OnRep_MeleeWeapon();

	UFUNCTION()
	void OnRep_EquipState();

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

	// 지금 든 총이 토글 조준 방식인지(저격총 설정/일반 ADS 설정).
	bool UsesToggleZoom() const;

	// 재장전이 시작되면 토글 조준을 푼다(발로란트: 재장전은 조준을 끊고, 끝난 뒤 다시 우클릭해야 조준한다).
	// 누르고 있는 방식이면 재장전 동안만 조준이 꺼졌다가 버튼을 계속 누르고 있으면 다시 조준된다.
	void ExitToggledZoomForReload();

	// === 인벤토리 내부 ===

	// 이 머신이 쓰는 장착 상태: 교체를 예측하는 소유 클라는 자기 사본, 서버·다른 클라는 복제된 서버 상태.
	const FValorEquipState& GetEffectiveEquipState() const;

	// 원격 소유 클라(서버가 아닌데 내가 조종하는 캐릭터)인가 = 교체를 예측하는 머신인가.
	bool IsPredictingOwner() const;

	// 클라의 첫 복제(OnRep)는 BeginPlay보다 먼저 올 수 있어, 그 경로에서도 소유 캐릭터를 확보한다.
	void CacheOwnerCharacter();

	// 지금 장착 상태로 EquippedWeapon을 다시 정하고, 슬롯 무기들의 보임/숨김을 맞춘다(모든 머신, 여러 번 불려도 같은 결과).
	void RefreshActiveEquipment();

	// 든 무기가 바뀐 순간의 정리: 이전 총 점사 취소, 로컬 발사 루프·조준 해제, 서버는 재장전·조준 취소.
	void HandleActiveEquipmentChanged(AValorWeaponBase* PreviousWeapon);

	bool CanEquipSlot(EValorWeaponSlot Slot) const;
	void SetSlotWeapon(EValorWeaponSlot Slot, AValorWeaponBase* Weapon);

	// 슬롯의 장착 시간(칼은 칼 액터, 아직 복제 전이면 칼 클래스 기본값).
	float GetEquipDurationForSlot(EValorWeaponSlot Slot) const;

	// 장착 완료 시각(동기화 시간축) = 장착 시작 + 장착 시간.
	double GetEquipReadyTime() const;

	// 서버 전용: 교체 요청 처리(원격 클라 RPC와 리슨 호스트 입력이 같이 쓴다).
	void ProcessEquipRequestAuthority(EValorWeaponSlot Slot, float ClientRequestTime);

	// 서버 전용: 손에 든 슬롯을 바꾸고 장착을 시작한다(StartTime = 장착 시작 시각, 아주 과거 값이면 즉시 장착).
	void SetActiveSlotAuthority(EValorWeaponSlot Slot, float StartTime);

	// 서버 전용: 총을 맞는 슬롯에 넣는다. 같은 슬롯의 총은 바닥에 떨어뜨린다(발로란트식 교체).
	void AddWeaponToInventory(AValorWeaponBase* NewWeapon, bool bMakeActive, bool bInstantEquip);

	// 서버 전용: 슬롯의 총을 바닥 픽업으로 바꿔 내려놓는다(총 종류·탄약 유지). 교체·버리기·사망 드롭이 같이 쓴다.
	// bSelectFallback이면, 든 총을 버린 경우 남은 가장 좋은 무기를 꺼낸다(교체처럼 곧바로 다른 총을 넣을 때는 false).
	void DropWeaponInSlot(EValorWeaponSlot Slot, bool bSelectFallback);

	// 서버 전용: 칼이 없으면 스폰한다(스폰 직후·첫 지급 시).
	void EnsureMeleeWeapon();

	// 서버 전용: 남은 총과 칼을 모두 없앤다(사망·캐릭터 제거).
	void DestroyInventory();

	// 서버 전용: 떨어뜨릴 위치와 방향(캐릭터 앞 바닥, 벽 앞에서 멈춤).
	FTransform ComputeDropTransform() const;

	void RefreshADSOnLocalClient() const;
	void FinishReload();

	// 서버 전용: 조준선에 가장 가까운 줍기 가능한 픽업(거리·줍기 반경·시야 확인). 없으면 nullptr.
	AValorWeaponPickup* FindPickupInView() const;

	// 서버 전용: 한 대상에게 이번 발의 피해를 GAS 피해 GE로 적용한다(사수/무기/발사 위치/부위를 스펙에 싣는다).
	void ApplyDamageToTarget(AValorCharacter* Target, const FValorPendingTargetDamage& PendingDamage, const FVector& ShotOrigin);

	void PerformServerHitScan(const FVector& TraceStart, const FVector& ShotDirection, float MaxDistance, float ClientShotTimestampSeconds, FValorHitScanResult& OutResult) const;

	UPROPERTY()
	TObjectPtr<AValorCharacter> OwnerCharacter;

	UPROPERTY()
	TObjectPtr<UValorAbilitySystemComponent> AbilitySystemComponent;

	// 지금 든 총(복제하지 않는 캐시). RefreshActiveEquipment만 바꾼다.
	UPROPERTY(Transient)
	TObjectPtr<AValorWeaponBase> EquippedWeapon;

	// 소유 클라의 장착 상태 사본(예측). 서버 상태를 받아들일 수 있을 때마다 서버 값으로 덮어쓴다.
	FValorEquipState LocalEquipState;

	// 소유 클라가 마지막으로 보낸 교체 요청 번호.
	uint8 LocalEquipRequestId = 0;

	// RefreshActiveEquipment가 마지막으로 반영한 슬롯(같은 총 → 같은 총이어도 슬롯이 바뀌면 교체로 본다).
	EValorWeaponSlot LastAppliedActiveSlot = EValorWeaponSlot::Melee;

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

	// 조준을 시작한 시각(동기화 시간). 불독·스팅어는 여기서 ADSFireDelay가 지날 때까지 발사 입력을 미룬다(4.07).
	double LocalADSStartTime = -1000.0;

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

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
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
 * 전투 컴포넌트: 사격/재장전/ADS/줍기의 입력 → 서버 요청 → 서버 판정 → 연출 흐름을 담당한다.
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

	AValorWeaponBase* GetEquippedWeapon() const { return EquippedWeapon; }
	bool IsADSActive() const { return bIsADS; }
	bool IsReloading() const { return bIsReloading; }
	bool IsFireInputHeld() const { return bLocalFireHeld; }
	float GetLastFireSimulationWorldTime() const { return LastFireSimulationWorldTime; }

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

	// 발사 요청 1발. 신뢰성 RPC로 보내 유실/순서 뒤바뀜이 없게 한다(유실되면 서버/클라 스프레이 진행도가 한 발 어긋남).
	UFUNCTION(Server, Reliable)
	void ServerFireShot(const FValorShotRequest& Request);

	UFUNCTION(Server, Reliable)
	void ServerRequestReload();

	UFUNCTION(Server, Reliable)
	void ServerSetADSInput(bool bNewADS);

	UFUNCTION(Server, Reliable)
	void ServerInteractWithPickup();

	// 다른 클라용 발사 연출(트레이서/탄흔/발사 몽타주). 연출은 한 발 놓쳐도 게임에 영향이 없으므로 비신뢰로 보낸다.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSimulateFire(FVector_NetQuantize ImpactPoint, FVector_NetQuantizeNormal ImpactNormal, bool bBlockingHit, bool bHitCharacter);

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
	void PlayLocalShotPresentation(const FVector& ImpactPoint, const FVector& ImpactNormal, bool bBlockingHit, bool bHitCharacter);

	void EquipWeapon(AValorWeaponBase* NewWeapon);
	void ApplyEquippedWeaponAttachment() const;
	void RefreshADSOnLocalClient() const;
	void FinishReload();
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
	bool bLocalADSIntent = false;
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

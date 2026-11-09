#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "ValorCharacterMovementComponent.generated.h"

class UValorCombatComponent;

// =====================================================================================================
// 이동 속도 계산(순수 함수). 서버·소유 클라·테스트가 같은 식을 쓴다.
// =====================================================================================================
namespace ValorMovement
{
	// 장비 속도 배율(0~1)을 1바이트로 양자화한다. 클라는 양자화한 값으로 직접 이동을 계산하고 같은 값을 서버에 보내므로
	// 서버 재시뮬레이션과 비트 단위로 같은 속도가 나온다(반올림 차이로 인한 미세 보정이 생기지 않는다).
	VALOR_API uint8 QuantizeSpeedMultiplier(float Multiplier);
	VALOR_API float DequantizeSpeedMultiplier(uint8 Quantized);

	// 지면 최고 속도 = 칼 기준 달리기 × 장비 배율 × (앉기 비율 | 걷기 비율 | 1). 앉기가 걷기보다 우선한다.
	VALOR_API float ComputeMaxGroundSpeed(float BaseRunSpeed, float EquipMultiplier, bool bWantsToWalk, bool bIsCrouching, float WalkSpeedRatio, float CrouchSpeedRatio);

	// 서버: 클라가 이번 이동에 쓴 장비 배율 주장을 검증한다.
	//  - 서버가 아는 현재 배율 이하(더 느리게 움직인다는 주장)는 항상 받아들인다 — 느리게 움직여서 얻는 이득은 없다.
	//  - 서버 배율이 최근(GraceSeconds 안에) 바뀌었다면 바뀌기 전 배율까지 허용한다. 교체·조준 RPC가 먼저 도착하고,
	//    그 전에 클라가 옛 속도로 시뮬레이션한 이동이 뒤늦게 도착하는 정상 상황에서 보정이 생기지 않게 하기 위함이다.
	//  - 그보다 빠른 주장(칼을 든 척하며 라이플을 쏘는 조작)은 허용 최대치로 잘린다 → 서버 위치가 달라져 클라가 보정된다.
	VALOR_API uint8 ResolveClaimedSpeedMultiplier(uint8 Claimed, uint8 ServerCurrent, uint8 ServerPrevious, double SecondsSinceServerChange, float GraceSeconds);
}

/** 저장된 이동 1개(소유 클라). 걷기 의도와 그 순간의 장비 속도 배율을 같이 저장해 보정 후 재시뮬레이션에서 그대로 재현한다. */
class FValorSavedMove : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual bool IsImportantMove(const FSavedMovePtr& LastAckedMove) const override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* C) override;

	bool bSavedWantsToWalk = false;
	uint8 SavedEquipSpeedQuantized = 255;
};

class FValorNetworkPredictionData_Client : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;

	explicit FValorNetworkPredictionData_Client(const UCharacterMovementComponent& ClientMovement);

	virtual FSavedMovePtr AllocateNewMove() override;
};

/** 클라 → 서버 이동 RPC 데이터(패킹된 이동 RPC). 기본 이동 데이터 + 장비 속도 배율 1바이트. */
struct FValorCharacterNetworkMoveData : public FCharacterNetworkMoveData
{
	typedef FCharacterNetworkMoveData Super;

	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType) override;

	uint8 EquipSpeedQuantized = 255;
};

struct FValorCharacterNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
	FValorCharacterNetworkMoveDataContainer();

	FValorCharacterNetworkMoveData ValorMoveData[3];
};

/**
 * 발로란트식 이동 속도: 칼 6.75m/s 기준으로 든 무기(라이플 0.8 → 5.4m/s), 조준(밴달 ×0.76 → 4.1m/s),
 * 걷기(Riot 측정 61%), 앉기 비율을 곱한다.
 *
 * 왜 이동 컴포넌트에서 하는가(예측 안전성): 이동은 소유 클라가 먼저 시뮬레이션하고 서버가 같은 입력으로 다시 시뮬레이션해
 *   위치를 비교한다. 속도에 영향을 주는 상태(걷기, 장비 배율)가 "이동 하나하나"에 실려 있지 않으면, 교체 RPC가 도착한 순간
 *   서버는 새 속도로, 그보다 먼저 클라가 시뮬레이션한 이동은 옛 속도로 계산돼 위치가 어긋나고 보정(러버밴딩)이 생긴다.
 *   그래서 걷기는 압축 플래그(FLAG_Custom_0), 장비 배율은 커스텀 이동 데이터 1바이트로 매 이동과 함께 보낸다.
 *   예전처럼 걷기를 별도 RPC로 MaxWalkSpeed에 넣는 방식은 이 문제가 있어 대체했다.
 * 네트워크:
 *   - 소유 클라: 매 틱 전투 컴포넌트의 예측 상태(교체·조준)로 배율을 정하고 양자화해 이동에 저장한다. 보정 후 재시뮬레이션은
 *     저장된 값을 그대로 쓴다.
 *   - 서버: 클라가 주장한 배율을 서버 권위 상태로 검증(ValorMovement::ResolveClaimedSpeedMultiplier)한 뒤 그 값으로 시뮬레이션한다.
 *   - 리슨 호스트 자신과 다른 클라에서의 원격 캐릭터: 전투 컴포넌트 상태로 직접 계산한다.
 * 트레이드오프:
 *   - 클라가 숫자(배율)를 보내지만 서버가 아는 최대치를 넘지 못하므로 "더 빨리 달리기" 조작은 막힌다. 대신 서버 상태가 바뀐 직후
 *     GraceSeconds 동안은 바뀌기 전 속도를 허용한다(교체 직후 최대 0.3초, 그 사이에도 장착 시간 때문에 새 총은 못 쏜다).
 *   - 핑이 GraceSeconds보다 큰 클라는 서버가 먼저 바꾼 상태(총 줍기로 더 느린 총을 든 경우 등)에서 한 번 보정될 수 있다.
 *   - 앉기 이동 속도 비율은 공개 수치가 없어 추정치다.
 */
UCLASS()
class VALOR_API UValorCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UValorCharacterMovementComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual float GetMaxSpeed() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;

	// 걷기(Shift 홀드) 의도. 앉기(bWantsToCrouch)와 같은 방식으로 이동마다 실려 서버로 간다.
	void SetWantsToWalk(bool bNewWantsToWalk) { bWantsToWalk = bNewWantsToWalk; }
	bool WantsToWalk() const { return bWantsToWalk; }

	// 이번 이동에 쓰는 장비 속도 배율(양자화된 값).
	uint8 GetEquipSpeedQuantized() const { return EquipSpeedQuantized; }

	// 정확도 계산(걷기/달리기 판정)과 HUD가 읽는 기준값.
	float GetBaseRunSpeed() const { return BaseRunSpeed; }
	float GetWalkSpeedRatio() const { return WalkSpeedRatio; }
	float GetCrouchSpeedRatio() const { return CrouchSpeedRatio; }

protected:
	// 칼을 든 달리기 속도(cm/s). 위키 Melee "Run Speed 6.75 m/s". 총은 이 값에 무기 배율을 곱한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Movement", meta=(ClampMin="1.0"))
	float BaseRunSpeed = 675.0f;

	// 걷기 속도 비율. Riot 디자이너 측정(2020, 팬텀): "Run -> Walk (61%)".
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Movement", meta=(ClampMin="0.05", ClampMax="1.0"))
	float WalkSpeedRatio = 0.61f;

	// 앉아서 이동하는 속도 비율. 공개 수치가 없어 이전 프로젝트 값(180/500)을 유지한 추정치다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Movement", meta=(ClampMin="0.05", ClampMax="1.0"))
	float CrouchSpeedRatio = 0.36f;

	// 서버 장비 배율이 바뀐 뒤 바뀌기 전 배율을 계속 허용하는 시간(초). 교체 직전에 시뮬레이션된 이동이 늦게 도착하는 만큼만 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Movement|Validation", meta=(ClampMin="0.0"))
	float SpeedChangeGraceSeconds = 0.3f;

private:
	friend class FValorSavedMove;

	UValorCombatComponent* GetCombatComponent() const;

	// 전투 상태(이 머신의 시점)로 장비 배율을 다시 계산해 양자화 값으로 둔다.
	void RefreshEquipSpeedFromCombatState();

	// 서버: 서버 권위 배율의 변화 이력(직전 값과 바뀐 시각)을 갱신한다.
	void UpdateServerSpeedHistory();

	bool bWantsToWalk = false;

	// 이번 이동의 장비 속도 배율(양자화). 255 = 배율 1(칼).
	uint8 EquipSpeedQuantized = 255;

	// 서버 검증 이력.
	uint8 ServerAuthoritativeSpeed = 255;
	uint8 ServerPreviousSpeed = 255;
	double ServerSpeedChangeTime = -1000.0;

	FValorCharacterNetworkMoveDataContainer ValorMoveDataContainer;
};

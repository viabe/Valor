#pragma once

#include "CoreMinimal.h"
#include "ValorTaggingTypes.generated.h"

/**
 * 피격 감속(태깅): 총에 맞으면 잠깐 느려진다.
 *
 * 공개 수치(이 프로젝트 데이터의 근거):
 *  - 표준 72.5% 감속(3.0 패치, 75% → 72.5%). 같은 패치에서 이동 정확도 기준(데드존)도 27.5%가 됐다 = 맞으면 정확도 기준 속도까지 느려진다.
 *  - 벽 관통 탄 25%(1.02 패치, 35% → 25%. 그 뒤 변경은 확인하지 못함).
 *  - 산탄 펠릿은 10m 밖에서 맞히면 30%만(위키 Weapons).
 *  - 1.02: 감속이 한 번에 걸리지 않고 서서히 걸린다(목표 감속까지 걸리는 시간 2배). 핑이 높을 때 덜 튀게 하려는 목적.
 * 추정(공개 수치 없음): 감속이 다 걸리는 시간 0.1초, 0.3초까지 유지, 0.5초에 회복("짧은 시간").
 *
 * 모양: 감속이 시작된 뒤 RampInSeconds 동안 0 → 1로 걸리고, FullSlowUntilSeconds까지 유지, EndSeconds에 0으로 풀린다.
 *   이동 속도 배율 = 1 - 세기 × 모양.
 *
 * 네트워크(이동 예측과 맞추는 방법):
 *  - 서버는 맞은 순간 감속 상태(FValorTagState: 서버 시작 시각, 세기, 순번)를 정해 본인에게 복제한다.
 *  - 소유 클라는 새 순번을 받은 첫 이동에서 감속을 시작하고, 그 이동에 "받은 순번"을 실어 보낸다. 시작 시각은 그 이동의
 *    클라 이동 시각이다(FValorTagMoveClock).
 *  - 서버도 그 순번이 실린 이동의 클라 이동 시각으로 같은 시계를 시작한다. 두 쪽이 같은 입력(이동 시각, 순번, 세기)으로 같은 식을
 *    계산하므로 정상 클라는 위치 보정이 생기지 않는다(서버 시간 추정 오차나 핑과 무관).
 *  - 클라가 TagAckMaxDelaySeconds 안에 받았다고 하지 않으면(감속을 무시하는 조작, 아주 큰 지연) 서버가 그때 강제로 시작한다.
 *    조작은 감속을 피하지 못하고 그만큼 늦출 뿐이다.
 *  - 리슨 호스트 자신과 봇(서버가 직접 움직임)은 서버 시간 기준 상태를 그대로 쓴다.
 */
USTRUCT(BlueprintType)
struct FValorTaggingConfig
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Tagging", meta=(ClampMin="0.0", ClampMax="1.0"))
	float StandardStrength = 0.725f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Tagging", meta=(ClampMin="0.0", ClampMax="1.0"))
	float WallPenetrationStrength = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Tagging", meta=(ClampMin="0.0", ClampMax="1.0"))
	float FarPelletStrength = 0.30f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Tagging", meta=(ClampMin="0.0"))
	float FarPelletDistanceCm = 1000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Tagging", meta=(ClampMin="0.0"))
	float RampInSeconds = 0.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Tagging", meta=(ClampMin="0.0"))
	float FullSlowUntilSeconds = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Tagging", meta=(ClampMin="0.0"))
	float EndSeconds = 0.5f;
};

// 서버가 정한 감속 상태(본인에게만 복제). 시작 시각은 서버 월드 시간, 세기는 1바이트(0~255 = 0~1), 순번은 맞을 때마다 1씩 오른다.
USTRUCT()
struct FValorTagState
{
	GENERATED_BODY()

	UPROPERTY()
	float StartServerTime = -1000.0f;

	UPROPERTY()
	uint8 StrengthQuantized = 0;

	UPROPERTY()
	uint8 Sequence = 0;

	float GetStrength() const { return static_cast<float>(StrengthQuantized) / 255.0f; }
};

// 소유 클라의 이동 시각으로 잰 감속 시계. 클라와 서버(그 클라의 이동을 처리할 때)가 각자 같은 값을 갖는다.
struct FValorTagMoveClock
{
	float StartMoveTime = -1000.0f;
	uint8 StrengthQuantized = 0;
	uint8 Sequence = 0;
};

// 감속 규칙(순수 함수). 서버 판정·소유 클라 예측·서버 검증·테스트가 같은 식을 쓴다.
namespace ValorTagging
{
	// 모양 0~1(시작 뒤 Elapsed초).
	VALOR_API float ComputeEnvelope(float ElapsedSeconds, const FValorTaggingConfig& Config);

	// 서버 시간 기준 이동 속도 배율(1 = 감속 없음). 서버가 직접 움직이는 캐릭터(리슨 호스트, 봇)와 표시용.
	VALOR_API float ComputeSpeedMultiplier(const FValorTagState& State, double ServerTime, const FValorTaggingConfig& Config);

	// 서버: 새로 맞았을 때의 상태. 순번을 올리고, 세기는 아직 걸려 있는 감속과 비교해 센 쪽을 쓴다.
	// 이미 걸린 감속이 있으면 지금 감속 정도에서 이어서 다시 끝까지 걸린다(속도가 튀지 않게).
	VALOR_API FValorTagState AddHit(const FValorTagState& Current, double ServerNow, float Strength, const FValorTaggingConfig& Config);

	// 이동 시계로 새 감속 상태를 받는다(소유 클라: 새 순번을 본 첫 이동, 서버: 그 순번이 실린 이동). 지금 감속 정도에서 이어서 시작한다.
	VALOR_API FValorTagMoveClock AcknowledgeOnMoveClock(const FValorTagMoveClock& Current, const FValorTagState& State, float MoveTime, const FValorTaggingConfig& Config);

	// 이동 시계 기준 배율.
	VALOR_API float ComputeMoveClockMultiplier(const FValorTagMoveClock& Clock, float MoveTime, const FValorTaggingConfig& Config);

	// 서버 검증: 이 이동에 허용하는 가장 빠른 배율. 클라가 다음 이동 시각을 미리 어림해 쓰므로(프레임 경계의 작은 차이) 아주 작은 여유를 둔다.
	VALOR_API float ComputeAllowedMoveClockMultiplier(const FValorTagMoveClock& Clock, float MoveTime, float ToleranceSeconds, const FValorTaggingConfig& Config);

	// 한 발이 한 대상에게 준 감속 세기: 산탄 펠릿이 10m 밖이면 30%, 모든 펠릿이 벽을 뚫었으면 25%, 아니면 72.5%(약한 쪽을 쓴다).
	VALOR_API float SelectStrength(bool bPellets, float NearestHitDistanceCm, bool bAllPenetrated, const FValorTaggingConfig& Config);
}

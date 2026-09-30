#pragma once

#include "CoreMinimal.h"
#include "ValorWeaponTypes.generated.h"

// =====================================================================================================
// 사격 파이프라인 공용 타입
// 발로란트 넷코드 원칙(Riot 기술 블로그 "Peeking into VALORANT's Netcode"):
//  - 서버가 권위를 갖고, 클라는 자기 입력의 결과를 즉시 "예측"해서 보여 준다.
//  - 클라는 발사 시점(시뮬레이션 시각)을 서버에 보내고, 서버는 그 시각으로 되감아 판정한다.
//  - "the client's prediction almost always agrees with the server about where a shot landed"
// 이를 위해 반동/탄퍼짐은 (설정 데이터 + 발사 시각 + 시드 + 자세)만으로 결정되는 순수 계산으로 만들고,
// 서버와 소유 클라가 같은 함수를 각자 실행한다. 아래 타입들은 그 입력/출력이다.
// =====================================================================================================

/**
 * 사격 시점의 사수 자세 스냅샷.
 * 왜: 반동/탄퍼짐 배율(앉기·이동·점프·ADS)의 입력을 한곳에 모아, 서버(권위)와 소유 클라(예측)가
 *     각자 자기 캐릭터 상태로 같은 구조체를 채운 뒤 같은 함수에 넣게 한다 → 계산식 중복/불일치 방지.
 * 네트워크: 복제하지 않는다. 서버는 서버가 아는 이동 상태로, 클라는 로컬 상태로 채운다.
 * 트레이드오프: 원격 클라의 이동 상태는 서버가 수 ms 늦게 알 수 있어 탄퍼짐 "반경"이 미세하게 다를 수 있다.
 *             반동(수직/수평 패턴)은 발사 시각·순서·시드만으로 정해지므로 이 차이의 영향을 받지 않는다.
 */
struct FValorShooterStance
{
	float HorizontalSpeed = 0.0f;
	float WalkSpeed = 250.0f;
	float RunSpeed = 500.0f;
	float CrouchSpeed = 180.0f;
	float TimeSinceLanded = 1000.0f;
	bool bIsADS = false;
	bool bIsCrouched = false;
	bool bIsAirborne = false;
};

/**
 * 무기 1개의 스프레이(연사) 진행 상태.
 * - 서버: 권위 상태(실제 탄도 계산에 사용).
 * - 소유 클라: 예측 상태(즉시 트레이서/ADS 카메라/크로스헤어 표시에 사용).
 * 네트워크: 복제하지 않는다. 양쪽이 같은 발사 요청(시각)을 같은 순서로 처리하면 같은 값이 된다.
 * 회복이 끝나면(Gun Recovery Time 경과) 다음 발에서 통째로 초기화되므로, 어긋나도 한 스프레이 안에서만 영향이 있다.
 */
struct FValorSprayState
{
	// 마지막 발의 발사 시각(서버 동기화 시간축).
	double LastShotTime = -1000.0;

	// 마지막 발이 사용한 스프레이 진행도(발 단위, 소수). 커브 X축 값이다.
	float LastShotSprayIndex = 0.0f;

	// 마지막 발이 사용한 수평 반동(도, 배율 적용 전).
	float LastShotYaw = 0.0f;

	// 수평 반동이 향하는 쪽(-1 왼쪽 / +1 오른쪽 / 0 아직 미정 = 보호 탄 구간).
	// 수평 반동은 이 쪽의 진폭(커브 값)을 목표로 "Yaw Switch Time에 한쪽 끝에서 반대쪽 끝까지" 가는 속도로 이동한다.
	int32 YawSide = 0;

	// 현재 스프레이에서 쏜 발 수(디버그/연출용 정수 카운트).
	int32 ShotsInSpray = 0;

	bool bHasFired = false;
};

/** 상태를 바꾸지 않는 "지금 쏜다면" 평가 결과. ADS 카메라와 크로스헤어가 매 프레임 읽는다. */
struct FValorSprayEvaluation
{
	// 다음 발이 사용할 스프레이 진행도.
	float SprayIndex = 0.0f;

	// 다음 발에 적용될 반동(도, 자세/모드 배율 적용 후). +Pitch = 위, +Yaw = 오른쪽.
	float RecoilPitchDegrees = 0.0f;
	float RecoilYawDegrees = 0.0f;

	// 연사로 인한 탄퍼짐(첫 발 오차 포함, 앉기 배율 적용 후).
	float SprayErrorDegrees = 0.0f;

	// 완전히 회복된 상태의 탄퍼짐(= 첫 발 오차 × 앉기 배율). 크로스헤어 "Firing Error" 표시의 기준선.
	float RestErrorDegrees = 0.0f;

	// 이동/점프/착지로 인한 추가 탄퍼짐.
	float MovementErrorDegrees = 0.0f;

	// 총 탄퍼짐 반경 = SprayErrorDegrees + MovementErrorDegrees.
	float FiringErrorDegrees = 0.0f;
};

/**
 * 한 발의 결정적 계산 결과. 방향 계산(ComputeShotDirection)에 필요한 값을 모두 담는다.
 * 무작위 값도 "시드에서 뽑은 결과"로 저장해 두어, 같은 결과를 두 번 계산하지 않고 재사용한다.
 */
USTRUCT()
struct FValorComputedShotData
{
	GENERATED_BODY()

	// 스프레이 안에서 몇 번째 발인지(0부터).
	UPROPERTY()
	int32 ShotIndexInSpray = 0;

	// 커브 평가에 쓴 스프레이 진행도(탭 사격이면 소수).
	UPROPERTY()
	float SprayIndex = 0.0f;

	// 이번 발에 적용된 반동(도, 배율 적용 후). 조준 방향 기준 로컬 공간에서 적용된다.
	UPROPERTY()
	float RecoilPitchDegrees = 0.0f;

	UPROPERTY()
	float RecoilYawDegrees = 0.0f;

	// 이번 발의 총 탄퍼짐 반경(도)과 중심 편향(Error Power).
	UPROPERTY()
	float FiringErrorDegrees = 0.0f;

	UPROPERTY()
	float ErrorPower = 1.0f;

	// 시드에서 뽑은 탄퍼짐 난수(반경 비율 0~1, 방향 비율 0~1).
	UPROPERTY()
	float ErrorRadiusRandom = 0.0f;

	UPROPERTY()
	float ErrorAngleRandom = 0.0f;
};

/**
 * 클라 → 서버 발사 요청(RPC 파라미터).
 * 왜 이 두 값뿐인가: 프로젝트 원칙상 클라는 "입력"만 보낸다. 발사 시각(방아쇠를 당긴 순간)과 조준 방향(마우스 입력의 결과)은
 *   입력이고, 반동/탄퍼짐/피격 결과는 서버가 스스로 계산한다. 클라가 계산한 값은 절대 보내지 않는다.
 * 네트워크: NetSerialize로 12바이트 → 8바이트(시각 float 4 + Pitch/Yaw 각 2바이트)로 압축한다.
 *   발사는 초당 10회 가까이 오가므로 작게 유지할 가치가 있다.
 * 결정성: 클라는 보내기 전에 Quantize()로 서버가 받게 될 값과 똑같이 만든 뒤 그 값으로 예측한다.
 */
USTRUCT()
struct FValorShotRequest
{
	GENERATED_BODY()

	// 발사 시각(서버 동기화 시간축, 초). 서버 되감기(랙 보상)와 반동 회복 계산의 공통 기준.
	UPROPERTY()
	float ClientShotTime = 0.0f;

	// 발사 순간의 조준 방향(컨트롤 회전). 카메라 연출(뷰 펀치)이 섞이지 않은 순수 조준이다.
	UPROPERTY()
	FRotator AimRotation = FRotator::ZeroRotator;

	// NetSerialize와 동일한 양자화를 로컬에서 미리 적용한다(서버/클라가 비트 단위로 같은 입력을 쓰도록).
	void Quantize();

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <>
struct TStructOpsTypeTraits<FValorShotRequest> : public TStructOpsTypeTraitsBase2<FValorShotRequest>
{
	enum
	{
		WithNetSerializer = true
	};
};

#pragma once

#include "CoreMinimal.h"
#include "Weapons/ValorWeaponTypes.h"

struct FValorWeaponConfig;

/**
 * 발로란트식 스프레이(반동 + 탄퍼짐) 결정적 시뮬레이션.
 *
 * 책임(SRP): "설정 데이터 + 스프레이 상태 + 발사 시각 + 자세 + 시드 → 이번 발의 반동/탄퍼짐/방향" 계산만 한다.
 *   UObject·월드·네트워크를 전혀 모르는 순수 함수라서 서버와 클라가 똑같이 실행할 수 있고, 자동화 테스트로 검증할 수 있다.
 *
 * 모델(Riot 공개 자료 기반):
 *   1) 스프레이 진행도(SprayIndex, 발 단위 실수)가 반동/탄퍼짐 커브의 X축이다.
 *   2) 풀오토 간격이면 발마다 +1, 사격을 멈추면 Gun Recovery Time에 걸쳐 0으로 회복된다.
 *      회복 도중 다시 쏘면 Tap Efficiency만큼 누적 속도가 줄어든다(탭/버스트가 유리해지는 이유).
 *   3) 수직 반동 = VerticalRecoilCurve(진행도). 보호 탄 구간은 완전 결정적이다.
 *   4) 수평 반동 = 보호 탄 이후 시드로 방향(좌/우)을 고르고 그쪽 진폭(HorizontalRecoilAmplitudeCurve)을 목표로 이동,
 *      매 발 YawSwitchChance 확률로 목표가 반대편으로 바뀌며, 한쪽 끝→반대쪽 끝을 YawSwitchTime에 가는 속도로 넘어간다
 *      (11.08 패치노트의 세 파라미터를 그대로 구현, 실제 게임 영상의 카메라 좌우 궤적과 비교해 확인).
 *   5) 탄퍼짐 = 첫 발/최대 오차 사이를 FiringErrorCurve로 보간 + 이동 오차, 반경 분포는 Error Power(중심 편향)를 따른다.
 *   6) 발사 간격 = 발마다 "다음 발까지 최소 간격"을 계산해 상태에 남긴다(점사: 점사 간격/점사 사이 대기, 오딘: 가속).
 *      서버는 이 값으로 연사 속도를 검증하고, 클라는 이 값으로 다음 발을 예약한다 → 둘이 같은 리듬으로 쏜다.
 *   7) 산탄: 반동은 한 번, 펠릿마다 발 시드 + 펠릿 번호로 퍼짐을 따로 뽑는다(서버/클라가 같은 산탄 모양).
 *
 * 네트워크: 이 모듈은 네트워크를 모른다. 서버는 권위 상태로, 소유 클라는 예측 상태로 같은 함수를 호출한다.
 * 트레이드오프: 시각을 입력으로 쓰므로 서버는 클라가 주장한 발사 시각을 검증(범위/간격/속도 제한)한 뒤 사용해야 한다.
 */
namespace ValorSpray
{
	// 현재 발사 모드(힙/ADS)의 기본 발사 간격(초, 1/FireRate). 점사 모드는 공식 표기대로 "평균" 간격이다.
	// 실제 다음 발까지의 간격(점사·가속 반영)은 AdvanceShot이 상태의 NextShotCooldown에 남긴다.
	VALOR_API float GetFireInterval(const FValorWeaponConfig& Config, bool bIsADS);

	// 이 무기가 낼 수 있는 가장 빠른 순간 발사 속도(발/초): 점사 안 속도, 가속 최고 속도까지 포함. 서버 수신 속도 제한에 쓴다.
	VALOR_API float GetMaxFireRate(const FValorWeaponConfig& Config);

	// 이 시각에 쏘면 새 스프레이(완전 회복 후 첫 발)가 시작되는지.
	VALOR_API bool IsNewSpray(const FValorWeaponConfig& Config, bool bIsADS, const FValorSprayState& State, double ShotTime);

	// 상태를 바꾸지 않고 "지금 쏜다면"의 반동/탄퍼짐을 평가한다(ADS 카메라, 크로스헤어, 디버그 표시용).
	VALOR_API FValorSprayEvaluation Evaluate(const FValorWeaponConfig& Config, const FValorShooterStance& Stance, const FValorSprayState& State, double Now);

	// 한 발을 계산하고 스프레이 상태를 한 발 전진시킨다. 같은 입력이면 항상 같은 결과를 낸다.
	VALOR_API FValorComputedShotData AdvanceShot(const FValorWeaponConfig& Config, const FValorShooterStance& Stance, FValorSprayState& State, double ShotTime, int32 ShotSeed);

	// 조준 방향에 반동(로컬 공간 회전)과 탄퍼짐 샘플을 적용한 최종 탄 방향.
	VALOR_API FVector ComputeShotDirection(const FRotator& AimRotation, const FValorComputedShotData& Shot);

	// 산탄 펠릿 하나의 방향. 0번 펠릿은 ComputeShotDirection과 같고(단발 무기와 동일), 나머지는 펠릿마다 독립 난수로 퍼진다.
	VALOR_API FVector ComputePelletDirection(const FRotator& AimRotation, const FValorComputedShotData& Shot, int32 PelletIndex);

	// 탄퍼짐 없이 반동만 적용한 방향(버키 캐니스터처럼 "중심선"이 필요한 경우).
	VALOR_API FVector ComputeRecoilDirection(const FRotator& AimRotation, const FValorComputedShotData& Shot);

	// 무기 시드 + 누적 발사 번호로 한 발의 시드를 만든다(플랫폼과 무관하게 같은 값).
	VALOR_API int32 MakeShotSeed(int32 WeaponSeed, int32 ShotCounter);
}

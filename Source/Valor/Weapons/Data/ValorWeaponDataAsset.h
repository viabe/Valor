#pragma once

#include "Animation/ValorAnimTypes.h"
#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "ValorWeaponDataAsset.generated.h"

class UAnimMontage;
class UMaterialInterface;
class UNiagaraSystem;
class USoundBase;

// =====================================================================================================
// 발로란트식 무기 데이터 설계
// - 왜: 발로란트는 총을 "Equippable" 단위의 PrimaryDataAsset(밴달 = ShooterGame/.../AK/AKPrimaryAsset)으로
//   관리하고, 반동/탄퍼짐을 디자이너 용어(Gun Recovery Time, Tap Efficiency, Firing Error 커브, Error Power,
//   Protected bullet count, Yaw switch time/chance, Vertical(Pitch) recoil curve)로 튜닝한다. 이 이름들은
//   공식 패치노트(0.50, 2.02, 6.11, 9.10, 11.08)에 그대로 등장하므로, 같은 이름·같은 단위(도/초/발)로 필드를
//   두면 패치노트 수치를 그대로 옮겨 적고 비교할 수 있다.
// - 네트워크: 이 데이터는 복제하지 않는다. 서버와 클라가 같은 에셋을 로드하므로 "같은 입력 → 같은 결과"라는
//   결정적 시뮬레이션의 기반이 되고, 런타임 상태(스프레이 진행도 등)만 무기 액터가 따로 들고 있다.
// - 트레이드오프: 수치를 에셋에 두면 코드 수정 없이 튜닝할 수 있지만, 서버/클라가 서로 다른 에셋 버전을 쓰면
//   예측이 어긋난다(배포 파이프라인에서 버전을 맞춰야 함).
// =====================================================================================================

UENUM(BlueprintType)
enum class EValorWallPenetrationTier : uint8
{
	Low,
	Medium,
	High
};

// 상점/무기 분류. valorant-api의 EEquippableCategory(Sidearm/SMG/Shotgun/Rifle/Sniper/Heavy)와 같은 구분이다.
// 무기군마다 이동 오차·이동 속도 같은 공통 규칙이 묶여 있어, 나중에 상점 UI와 무기군 규칙 분기에 쓴다.
UENUM(BlueprintType)
enum class EValorWeaponCategory : uint8
{
	Sidearm,
	SMG,
	Shotgun,
	Rifle,
	Sniper,
	Heavy
};

// 우클릭(Alternate Fire) 동작 종류. valorant-api의 EWeaponAltFireDisplayType과 같은 구분이다.
UENUM(BlueprintType)
enum class EValorAltFireType : uint8
{
	// 우클릭 없음: 프렌지, 고스트, 셰리프, 쇼티, 저지, 밴딧.
	None,

	// 정조준(줌): 대부분의 주무기. 불독·스팅어는 ADS에서 점사(BurstCount)로 바뀐다.
	ADS,

	// 클래식 우클릭: 3펠릿 산탄 점사(라이엇 데이터 분류가 "Shotgun"이다).
	Shotgun,

	// 버키 우클릭: 일정 거리(AirBurstDistanceCm)에서 터지며 산탄을 뿌리는 캐니스터.
	AirBurst
};

UENUM(BlueprintType)
enum class EValorHitZone : uint8
{
	None,
	Body,
	Head,
	Leg
};

USTRUCT(BlueprintType)
struct FValorDamageRangeStep
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float MaxDistanceCm = 1500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float HeadDamage = 160.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float BodyDamage = 40.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float LegDamage = 34.0f;
};

// 발사할 때마다 화면이 위로 톡 튀었다가 곧바로 가라앉는 "카메라 킥"(패턴 추종 위에 더해지는 톱니 모양 성분).
// 근거: 실제 발로란트 밴달 무보정 연사 영상의 프레임별 카메라 회전량 측정(2026-09-29, Docs/VandalRecoil.md) —
//   패턴을 따라 올라간 높이(약 4.2°) 위에서 매 발 약 0.8~0.9° 튀고, 다음 발(0.1초) 전에 거의 다 가라앉는 톱니가 반복됐다.
// 네트워크: 소유 클라의 카메라에만 적용되는 순수 연출이다. 컨트롤 회전(조준)과 서버 탄도는 건드리지 않는다.
USTRUCT(BlueprintType)
struct FValorCameraKickConfig
{
	GENERATED_BODY()

	// 한 발당 위로 튀는 크기(도, 최고점 기준). 실제 값은 [값×(1-PitchVariance), 값] 사이에서 무작위.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|CameraKick", meta=(ClampMin="0.0"))
	float PitchDegrees = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|CameraKick", meta=(ClampMin="0.0", ClampMax="1.0"))
	float PitchVariance = 0.2f;

	// 좌우 무작위 흔들림 최대(±도). 영상에서는 발마다의 좌우 튐이 거의 보이지 않아 작게 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|CameraKick", meta=(ClampMin="0.0"))
	float YawDegrees = 0.15f;

	// 화면 기울어짐(롤) 무작위 최대(±도). 발로란트 영상에서는 롤 흔들림이 두드러지지 않아 기본 0.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|CameraKick", meta=(ClampMin="0.0"))
	float RollDegrees = 0.0f;

	// 킥이 최고점에 도달하는 시간(초). 임계 감쇠 스프링의 반응 속도로, 작을수록 날카롭게 튀고 빨리 돌아온다.
	// 0.025초면 한 프레임 안에 튀고, 다음 발(0.1초) 직전에는 약 20%만 남는다 — 영상에서 측정한 톱니 모양과 같다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|CameraKick", meta=(ClampMin="0.005"))
	float PeakTimeSeconds = 0.025f;

	// 연사 중 누적될 수 있는 킥의 최대치(도).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|CameraKick", meta=(ClampMin="0.0"))
	float MaxKickDegrees = 3.0f;
};

// 이동 중 추가 탄퍼짐(도) 네 가지. 발사 모드 하나만 무기 공통 값과 다를 때(클래식 우클릭) 덮어쓰기용으로 쓴다.
USTRUCT(BlueprintType)
struct FValorMovementErrorValues
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="0.0"))
	float CrouchMovingError = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="0.0"))
	float WalkingError = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="0.0"))
	float RunningError = 6.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="0.0"))
	float AirborneError = 10.0f;
};

// 발사 모드 하나(힙파이어 = Primary Fire / 정조준 = Alternate Fire)의 수치.
// 발로란트 무기 데이터가 weaponStats(힙)와 adsStats(정조준)를 나눠 두는 구조를 그대로 따른다.
// 기본값은 밴달 힙파이어 값이며, ADS 값은 FValorWeaponConfig 생성자에서 채운다.
// "[구현 예정]" 필드는 공식 수치를 미리 담아 두는 데이터 전용 필드다. 현재 사격 코드는 읽지 않으며(산탄·점사·가속),
// 해당 총을 구현할 때 이 값을 그대로 쓰면 된다.
USTRUCT(BlueprintType)
struct FValorFireModeStats
{
	GENERATED_BODY()

	// 초당 발사 수. 밴달: 힙 9.75, ADS 8.775(90%). 서버는 이 값으로 발사 간격(연사 속도)을 검증한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode", meta=(ClampMin="0.1"))
	float FireRate = 9.75f;

	// 첫 발 탄퍼짐 반경(도). 밴달: 힙 0.25, ADS 0.1575 ("1st Shot Spread").
	// 반동이 아니라 "무작위 오차"이므로 첫 발도 크로스헤어에서 이만큼 벗어날 수 있다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode", meta=(ClampMin="0.0"))
	float FirstShotError = 0.25f;

	// 연사로 도달하는 최대 탄퍼짐 반경(도). 밴달: 힙 1.0, ADS 1.02 ("Max Spread").
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode", meta=(ClampMin="0.0"))
	float MaxFiringError = 1.0f;

	// 반동(수직·수평) 전체 배율. Riot: "ADS will also reduce recoil (this is multiplicative with crouch)".
	// 위키 표기는 "Slight spread and recoil reduction"뿐이라 ADS 값(0.9)은 추정치다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode", meta=(ClampMin="0.0"))
	float RecoilMultiplier = 1.0f;

	// 카메라(= 화면 중앙 크로스헤어)가 "스프레이 패턴"을 따라가는 비율(0~1).
	// 발로란트 힙파이어는 약 0.5: 실제 무보정 연사 영상 2개를 프레임 단위로 측정하니 탄은 조준점 위 약 8.5°까지 가는데
	//   카메라는 약 4.2~4.9°만 올라갔다 → 화면이 반쯤 따라 올라가고 탄은 크로스헤어보다 더 위에 맞는다
	//   ("크로스헤어가 스프레이를 그대로 따라가지는 않는다"는 커뮤니티 설명과도 맞다).
	// ADS는 1: 위키 표기 "Crosshair follows recoil" — 조준경이 반동을 따라 올라가 탄이 조준점에 맺힌다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode", meta=(ClampMin="0.0", ClampMax="1.0"))
	float CameraRecoilFollowRatio = 0.5f;

	// 매 발 화면이 튀었다 돌아오는 카메라 킥(연출). 패턴 추종(CameraRecoilFollowRatio)과 더해져 최종 카메라 회전이 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode")
	FValorCameraKickConfig CameraKick;

	// 앉아서 멈춰 있을 때 이 모드의 탄퍼짐 배율. 위키 "Crouch primary/alt fire spread multiplier", 밴달 0.85.
	// 모드마다 다를 수 있어(스팅어·불독: 힙 0.85 / ADS 0.75, 클래식: 좌클릭 0.75 / 우클릭 0.9) 무기 공통 값이 아니라 여기에 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode", meta=(ClampMin="0.0"))
	float CrouchErrorMultiplier = 0.85f;

	// 이 모드로 들고 있을 때 이동 속도 배율. valorant-api runSpeedMultiplier.
	// 힙 = 칼(6.75m/s) 대비(밴달 0.8 → 5.4m/s), ADS = 그 무기 힙 이동 속도 대비(밴달 0.76 → 5.4 × 0.76 = 4.1m/s).
	// ADS가 "힙 대비"라는 근거: 위키 표기 "Move Speed 76% (4.104 m/sec)", 2.03 패치 "마샬 줌 이동 속도: 줌 해제 속도의 76% → 90%".
	// [구현 예정] 캐릭터 이동 속도에는 아직 반영하지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode", meta=(ClampMin="0.1", ClampMax="1.0"))
	float MoveSpeedMultiplier = 0.8f;

	// 한 발(방아쇠 1회)에 나가는 산탄 수. 1 = 일반 탄. 저지 12, 버키 15, 쇼티 15, 클래식 우클릭 3, 버키 우클릭 5.
	// 피해량 구간(DamageRanges)은 산탄 한 알 기준이다. [구현 예정]
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode", meta=(ClampMin="1"))
	int32 PelletCount = 1;

	// 한 발에 소모하는 탄약 수. 클래식 우클릭 = 3(탄창 12발 = 우클릭 4번). [구현 예정]
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode", meta=(ClampMin="1"))
	int32 AmmoPerShot = 1;

	// 점사 발 수(1 = 점사 아님). 불독 ADS 3, 스팅어 ADS 4. [구현 예정]
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode", meta=(ClampMin="1"))
	int32 BurstCount = 1;

	// 점사 안에서의 발사 속도(발/초). 불독 13.333, 스팅어 18. 0이면 점사가 아니다. [구현 예정]
	// 점사 모드의 FireRate는 공식 표기대로 "평균" 속도다(불독 6.316, 스팅어 8.471).
	// 점사 사이 대기 = BurstCount / FireRate - BurstCount / BurstFireRate → 불독 0.475 - 0.225, 스팅어 0.472 - 0.222, 둘 다 0.25초.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode", meta=(ClampMin="0.0"))
	float BurstFireRate = 0.0f;

	// 계속 쏠수록 발사 속도가 오르는 무기(오딘 힙파이어 12 → 15.6). 0이면 가속 없음. FireRate가 시작 속도다. [구현 예정]
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode", meta=(ClampMin="0.0"))
	float SpinUpMaxFireRate = 0.0f;

	// FireRate → SpinUpMaxFireRate까지 오르는 데 걸리는 연사 시간(초). 공개 수치가 없어 오딘 값은 추정치다. [구현 예정]
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode", meta=(ClampMin="0.0"))
	float SpinUpTimeSeconds = 0.0f;

	// 이 모드만 이동 오차가 무기 공통 값(FValorMovementAccuracyProfile)과 다른 경우에 켠다.
	// 공식 수치가 모드별로 다른 무기는 클래식뿐이다(좌클릭 0.5/1.1/2.3/7, 우클릭 0/0.6/1.5/2.25).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode", meta=(InlineEditConditionToggle))
	bool bOverrideMovementError = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FireMode", meta=(EditCondition="bOverrideMovementError"))
	FValorMovementErrorValues MovementErrorOverride;
};

// 총기 하나의 반동/스프레이 정의(힙·ADS 공용). 발로란트의 "하이브리드" 반동 모델을 데이터로 표현한다.
// 수치 근거: Riot 패치노트(구조 파라미터) + 실제 게임 영상의 프레임 단위 측정(크기) — Docs/VandalRecoil.md 참고.
// Riot 개발자(2020): "The first several bullets have fully deterministic recoil, but deeper into the spray,
//                    your weapon will make pseudo-random deviations."
// → (1) 보호 탄 구간: 수직 커브만 따르는 완전 결정적 반동
//   (2) 그 이후: 수평 드리프트가 무작위로 한쪽을 고르고, 매 발 확률적으로 반대편으로 넘어간다(시드 기반 의사난수).
USTRUCT(BlueprintType)
struct VALOR_API FValorRecoilProfile
{
	GENERATED_BODY()

	// 생성자에서 밴달 기본 커브 키를 채운다(에셋을 비워 둬도 밴달이 동작하도록).
	FValorRecoilProfile();

	// 수직(Pitch) 반동 커브. X = 스프레이 진행도(발 단위, 소수 허용), Y = 누적 수직 반동(도).
	// 11.08 패치노트의 "Vertical (Pitch) recoil curve ... (total recoil unchanged)" 개념: 커브 모양과 총량으로 정의한다.
	// 밴달 기본값(실제 게임 영상 측정): 약 8발 만에 약 7.9°까지 거의 직선으로 오르고, 이후 약 8.7~8.9°에서 멈춘다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil")
	FRuntimeFloatCurve VerticalRecoilCurve;

	// 수평(Yaw) 반동 진폭 커브. X = 스프레이 진행도, Y = 그 시점에 총구가 한쪽으로 벌어질 수 있는 최대 폭(도).
	// 보호 탄 이후 무작위로 고른 쪽(YawSide)의 이 폭을 목표로, "Yaw Switch Time에 한쪽 끝에서 반대쪽 끝까지 가는 속도"로 이동한다.
	// 밴달 기본값(영상 측정): 6발째부터 벌어지기 시작해 약 12발째 ±2.4°, 이후 ±2.8°까지.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil")
	FRuntimeFloatCurve HorizontalRecoilAmplitudeCurve;

	// 탄퍼짐 증가 커브. X = 스프레이 진행도, Y = 0~1 (FirstShotError → MaxFiringError 보간 비율).
	// 0.50 패치노트: "Firing Error (this value is a curve that has intermediate values between each bullet)".
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil")
	FRuntimeFloatCurve FiringErrorCurve;

	// 보호 탄 수: 이 발 수까지는 수평 방향 전환(Yaw switch)이 일어나지 않는다. 11.08(PC): 밴달 4 → 6.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil", meta=(ClampMin="0"))
	int32 ProtectedBulletCount = 6;

	// 보호 탄 이후 매 발마다 수평 반동이 반대편으로 넘어갈 확률. 11.08(PC): 6% → 10%.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil", meta=(ClampMin="0.0", ClampMax="1.0"))
	float YawSwitchChance = 0.10f;

	// 수평 반동이 한쪽에서 반대쪽으로 완전히 넘어가는 데 걸리는 시간(초). 11.08(PC): 0.37s → 0.6s.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil", meta=(ClampMin="0.01"))
	float YawSwitchTime = 0.6f;

	// 수평 반동 절대 상한(도). 진폭 커브를 잘못 입력해도 탄이 비현실적으로 벌어지지 않게 막는 안전장치.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil", meta=(ClampMin="0.0"))
	float MaxHorizontalRecoil = 3.5f;

	// 사격을 멈춘 뒤 반동/탄퍼짐이 첫 발 상태로 완전히 돌아오는 시간(초). 0.50: 밴달 0.4 → 0.375.
	// 0.50 패치노트: "Inaccuracy is accrued any time the weapon is re-fired prior to a complete duration of Gun Recovery Time."
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil", meta=(ClampMin="0.01"))
	float GunRecoveryTime = 0.375f;

	// 회복이 끝나기 전에 다시 쏠 때 부정확도(스프레이 진행도)가 쌓이는 속도를 낮추는 값. 0.50: 밴달 4 → 6.
	// "The higher the Tap Efficiency, the lower the rate of inaccuracy accrual."
	// 구현: 풀오토 간격이면 1발씩, 회복 직전에 다시 쏘면 1/TapEfficiency발만큼만 진행도가 오른다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil", meta=(ClampMin="1.0"))
	float TapEfficiency = 6.0f;

	// 연사 판정 여유 배율: 마지막 발 후 (발사 간격 × 이 값)까지는 회복을 시작하지 않는다.
	// 왜: 발사 시각을 이상적인 연사 간격으로 보정하더라도 네트워크/프레임 오차 몇 ms에 풀오토가 "중간에 회복"되지 않게 하기 위함.
	// 영상에서 카메라는 마지막 발 직후부터 곧바로 내려오기 시작하므로 여유는 짧게(10%) 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil", meta=(ClampMin="1.0"))
	float RecoveryGraceMultiplier = 1.1f;
};

// 자세/이동에 따른 정확도(무기군 공통). 발로란트는 이동 페널티를 무기군(라이플 등)끼리 공유한다.
// 모든 기본값은 라이플 기준 공식 패치노트/위키 값이다.
USTRUCT(BlueprintType)
struct FValorMovementAccuracyProfile
{
	GENERATED_BODY()

	// 이 속도 비율(달리기 최고 속도 대비) 이하이면 "멈춘 것"으로 보고 이동 페널티를 주지 않는다(카운터 스트레이프 허용).
	// 0.50: 25% → 30%, 이후 27.5%.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DeadzoneSpeedRatio = 0.275f;

	// 앉아서 이동 중 추가 탄퍼짐(도). 2.02: 0.3 → 0.8.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="0.0"))
	float CrouchMovingError = 0.8f;

	// 걷는 중 추가 탄퍼짐(도). 9.10: 2 → 3.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="0.0"))
	float WalkingError = 3.0f;

	// 달리는 중 추가 탄퍼짐(도). 9.10: 5 → 6.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="0.0"))
	float RunningError = 6.0f;

	// 공중 추가 탄퍼짐(도). 위키 라이플 Airborne +10.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="0.0"))
	float AirborneError = 10.0f;

	// 착지 직후 추가 탄퍼짐(도)과 지속 시간(초). 1.09: 5.0 → 7.0, 0.2s → 0.225s(점진 → 즉시 해제 방식).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="0.0"))
	float JumpLandError = 7.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="0.0"))
	float JumpLandErrorDuration = 0.225f;

	// 앉아서 멈춰 있을 때의 탄퍼짐 배율은 발사 모드마다 다를 수 있어 FValorFireModeStats::CrouchErrorMultiplier에 둔다.

	// 앉아서 멈춰 있을 때 반동 배율. 0.50: "Horizontal (Yaw) Recoil reduced by 15% while crouched and stationary".
	// Riot: "Crouching while firing weapons will reduce recoil" → 수직에도 같은 배율을 적용한다(수직 값은 추정).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="0.0"))
	float CrouchRecoilMultiplier = 0.85f;

	// 달리며 쏠 때 수직 반동 배율. 6.11: 밴달 1.5 → 1.8. 걷기 속도부터 달리기 최고 속도까지 선형으로 커진다(공중도 최대값).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="1.0"))
	float RunningVerticalRecoilMultiplier = 1.8f;

	// 달리며(점프·로프 포함) 쏠 때 수평 반동 배율. 4.0: 스펙터 "pitch and yaw recoil multipliers when running/jumping/on ascender
	// 1.25 → 1.5" 이후 6.11에서 수직만 1.8로 올렸으므로 스펙터 수평은 1.5다. 밴달은 공개 수치가 없어 1(배율 없음)로 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="1.0"))
	float RunningHorizontalRecoilMultiplier = 1.0f;

	// 탄퍼짐 중심 편향("Error Power", Riot 내부 명칭 Center Biasing). 반경 = 오차 × U^ErrorPower.
	// 0.5면 원 안에 균일, 클수록 중앙에 몰린다. 6.11: 이동 중 편향을 크게 줄여 "거의 균일"하게 바꿨다.
	// 정지 값(1.0)은 공개 수치가 없어 추정치다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="0.1"))
	float StandingErrorPower = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Accuracy", meta=(ClampMin="0.1"))
	float MovingErrorPower = 0.5f;
};

// 발사 연출(로컬 전용). 게임플레이 판정과 무관하므로 서버(데디케이티드)에서는 전혀 쓰지 않는다.
// 에셋을 비워 두면 개발 빌드에서 디버그 선/점으로 대체 표시한다(Valor.Debug.DrawShots).
USTRUCT(BlueprintType)
struct FValorWeaponFXConfig
{
	GENERATED_BODY()

	// 트레이서/총구 화염이 나가는 소켓(또는 본) 이름. Araxys 밴달 메시는 "Muzzle" 본을 가진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FX")
	FName MuzzleSocketName = TEXT("Muzzle");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FX")
	TObjectPtr<UNiagaraSystem> MuzzleFlashFX = nullptr;

	// 총구 → 탄착점으로 이어지는 트레이서. 끝점은 아래 User 파라미터(Vector)로 넘긴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FX")
	TObjectPtr<UNiagaraSystem> TracerFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FX")
	FName TracerEndParameterName = TEXT("BeamEnd");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FX")
	TObjectPtr<UNiagaraSystem> ImpactFX = nullptr;

	// 벽 탄흔 데칼. 발로란트처럼 스프레이 모양을 벽에서 읽을 수 있게 해 주는 핵심 피드백이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FX")
	TObjectPtr<UMaterialInterface> ImpactDecalMaterial = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FX")
	FVector ImpactDecalSize = FVector(4.0f, 5.0f, 5.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FX", meta=(ClampMin="0.0"))
	float ImpactDecalLifeSpan = 6.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FX")
	TObjectPtr<USoundBase> FireSound = nullptr;
};

// 무기 1종의 전체 정의. 구조체 기본값 = 밴달이므로, 데이터 자산이 없거나 비어 있어도 밴달로 동작한다.
USTRUCT(BlueprintType)
struct VALOR_API FValorWeaponConfig
{
	GENERATED_BODY()

	// ADS 수치와 피해량 구간처럼 한 줄 기본값으로 표현하기 어려운 밴달 값을 채운다.
	FValorWeaponConfig();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	FName WeaponId = TEXT("Vandal");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	FText DisplayName;

	// 상점/무기군 분류(valorant-api category).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	EValorWeaponCategory Category = EValorWeaponCategory::Rifle;

	// 상점 가격(크레딧). valorant-api shopData.cost. [구현 예정] 상점/경제 시스템에서 사용.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon", meta=(ClampMin="0"))
	int32 Cost = 2900;

	// 무기를 꺼내 쏠 수 있을 때까지의 시간(초, "Normal" 장착 속도). valorant-api equipTimeSeconds. [구현 예정]
	// 위키에는 Fast/Instant 장착 속도도 있으나 적용 조건이 공개돼 있지 않아 담지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon", meta=(ClampMin="0.0"))
	float EquipTimeSeconds = 1.0f;

	// 애니메이션 레이어가 어떤 무기군 포즈를 써야 하는지 분류한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	EValorWeaponAnimationType AnimationType = EValorWeaponAnimationType::Rifle;

	// 발사 애니메이션은 무기별로 다를 수 있으므로 데이터 자산에서 상체 몽타주를 갈아끼울 수 있게 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|Animation")
	TObjectPtr<UAnimMontage> FireMontage = nullptr;

	// 총기 메시 안의 그립 소켓을 손 소켓과 정렬해, 메시 피벗이 달라도 같은 장착 규칙을 유지한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|Attachment")
	FName GripSocketName = TEXT("GripSocket");

	// 사격 템포와 애니메이션 감각을 맞추기 위해 무기별 재생 속도도 함께 노출한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|Animation", meta=(ClampMin="0.1"))
	float FireMontagePlayRate = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	bool bAutomatic = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon", meta=(ClampMin="1"))
	int32 MagazineSize = 25;

	// 밴달 예비 탄약: 6.11에서 75 → 50.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon", meta=(ClampMin="0"))
	int32 MaxReserveAmmo = 50;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon", meta=(ClampMin="0.0"))
	float ReloadDuration = 2.5f;

	// 0보다 크면 탄창 교체가 아니라 한 발씩 장전한다(버키 0.5초/셸, 마샬 0.5초/발). ReloadDuration은 빈 탄창에서 가득 채우는 시간. [구현 예정]
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon", meta=(ClampMin="0.0"))
	float ReloadPerRoundSeconds = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon", meta=(ClampMin="100.0"))
	float TraceDistanceCm = 50000.0f;

	// 우클릭 동작 종류. ADS가 아닌 무기(None/Shotgun/AirBurst)는 줌이 없으므로 ADSZoomMultiplier = 1로 둔다.
	// [구현 예정] 현재 입력은 우클릭을 모두 정조준으로 처리하므로, ADS가 없는 무기는 AltFire에 힙 수치를 복사해 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	EValorAltFireType AltFireType = EValorAltFireType::ADS;

	// ADS 배율. 밴달 1.25배 줌 → 힙 FOV에서 계산한다(고정 FOV 대신 배율을 저장해 FOV 설정이 바뀌어도 줌 비율 유지).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon", meta=(ClampMin="1.0"))
	float ADSZoomMultiplier = 1.25f;

	// 2단 줌 배율(오퍼레이터 "Dual Zoom toggle between 2.5x and 5x"의 5배). 0이면 2단 줌 없음. [구현 예정]
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon", meta=(ClampMin="0.0"))
	float SecondaryADSZoomMultiplier = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon", meta=(ClampMin="0.1"))
	float ADSInterpSpeed = 18.0f;

	// 소음기(팬텀·고스트·스펙터). 위키: "Tracers not visible to enemies, Firing sound can't be heard at 40m+ except in direction of fire".
	// [구현 예정] 적 시점 트레이서 숨김 + 원거리 발사음 감쇠.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	bool bSilenced = false;

	// 버키 우클릭 캐니스터가 터지는 거리(cm). 공식 7.5m. 그 전에 맞으면 터지지 않고 펠릿 1알 피해만 준다. [구현 예정]
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon", meta=(ClampMin="0.0"))
	float AirBurstDistanceCm = 0.0f;

	// Primary Fire(힙파이어) 수치.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|Accuracy")
	FValorFireModeStats HipFire;

	// Alternate Fire(ADS) 수치.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|Accuracy")
	FValorFireModeStats AltFire;

	// 반동 패턴 + 회복 규칙. 기본은 힙·ADS 공용이며, ADS는 AltFire.RecoilMultiplier만 곱한다(밴달·팬텀 방식).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|Accuracy")
	FValorRecoilProfile RecoilProfile;

	// 우클릭이 힙파이어와 전혀 다른 발사 방식이라 반동/탄퍼짐 증가/회복 규칙을 따로 쓰는 무기만 켠다.
	// 예) 클래식 우클릭: 연속 점사마다 탄퍼짐이 1.9 → 2.5 → 6.0으로 뛴다(2.0 패치). 스팅어 점사: 회복 0.4초(2.03 패치).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|Accuracy", meta=(InlineEditConditionToggle))
	bool bUseSeparateAltFireRecoil = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|Accuracy", meta=(EditCondition="bUseSeparateAltFireRecoil"))
	FValorRecoilProfile AltFireRecoilProfile;

	// 자세/이동 정확도(무기군 공통 값).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|Accuracy")
	FValorMovementAccuracyProfile MovementAccuracy;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon|FX")
	FValorWeaponFXConfig Effects;

	// 관통 가능한 벽 두께(cm)와 관통 후 피해 배율. 발로란트는 등급(Low/Medium/High)만 공개하므로 cm·배율은 프로젝트 규칙이다
	// (Low 20cm ×0.5 / Medium 45cm ×0.7 / High 90cm ×0.8). 현재 판정은 Low 등급이면 관통하지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float PenetrationDepthCm = 45.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float PenetrationDamageMultiplier = 0.7f;

	// 공식 벽 관통 등급(valorant-api wallPenetration).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	EValorWallPenetrationTier PenetrationTier = EValorWallPenetrationTier::Medium;

	// 거리별 피해량(가까운 구간부터). 마지막 구간은 그 너머 전체에 적용된다(발로란트 표기의 "50m"는 표시 상한일 뿐 더 멀어도 같다).
	// 밴달은 거리 감쇠가 없다(전 구간 머리 160 / 몸 40 / 다리 34). 생성자에서 한 구간으로 채운다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	TArray<FValorDamageRangeStep> DamageRanges;
};

UCLASS(BlueprintType)
class VALOR_API UValorWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	FValorWeaponConfig WeaponConfig;

#if WITH_EDITORONLY_DATA
	// 이 에셋 수치의 출처(공식/추정)와 구현 상태 메모. 에디터 전용이라 쿠킹된 게임/서버에는 들어가지 않는다.
	// 왜: 영상 실측·패치노트·API에서 온 값과 추정값이 섞여 있으므로, 나중에 튜닝할 때 무엇을 믿어도 되는지 바로 보이게 한다.
	UPROPERTY(EditAnywhere, Category="Valor|Weapon|Source", meta=(MultiLine="true"))
	FString SourceNotes;
#endif
};

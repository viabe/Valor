#pragma once

#include "Animation/ValorAnimTypes.h"
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ValorWeaponDataAsset.generated.h"

class UAnimMontage;

UENUM(BlueprintType)
enum class EValorWallPenetrationTier : uint8
{
	Low,
	Medium,
	High
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

// 스프레이 패턴의 "한 발"을 나타낸다. 발로란트처럼 총기별 반동을 데이터로 정의하기 위한 최소 단위다.
// PitchKick(+)는 화면을 위로, YawKick(+)는 화면을 오른쪽으로 밀어내는 카메라 킥(도 단위)이다.
USTRUCT(BlueprintType)
struct FValorRecoilStep
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float PitchKick = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float YawKick = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float AdditionalSpread = 0.0f;
};

// 총기 하나의 "스프레이/반동" 전체를 정의하는 프로파일이다.
// 설계 의도: 발로란트는 (1) 앞쪽 탄들은 정해진 결정적 패턴을 따르고, (2) 패턴을 다 쓰면 랜덤 구간으로 넘어가며,
//           (3) 사격을 멈추면 카메라가 원래 조준점으로 되돌아오는 "회복(Recovery)"을 가진다.
// 이 세 가지를 모두 데이터로 분리해 두면, 나중에 팬텀/스펙터/오딘 등 다른 총을 추가할 때
// 이 구조체만 채워 넣으면 되므로 확장이 쉬워진다(총기 코드 수정 불필요).
USTRUCT(BlueprintType)
struct FValorRecoilProfile
{
	GENERATED_BODY()

	// 결정적 스프레이 패턴이다. 인덱스 = 발사 순서(0부터), 값 = 그 탄에서 화면에 더해지는 카메라 킥이다.
	// 비워두면(0개) 코드에 내장된 기본 밴달 패턴을 사용하므로, 데이터 자산을 따로 채우지 않아도 동작한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil")
	TArray<FValorRecoilStep> Pattern;

	// 결정적 패턴을 모두 소진한 뒤(탄창이 길어 연사가 계속될 때) 사용할 랜덤 반동 범위다.
	// 발로란트도 패턴 후반부는 좌우로 랜덤하게 흔들린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil")
	float SustainedPitchMin = 0.10f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil")
	float SustainedPitchMax = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil")
	float SustainedYawMin = -0.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil")
	float SustainedYawMax = 0.6f;

	// === 시각적 반동(뷰 펀치) — 연출 전용 ===
	// 발로란트의 화면 반동 재현: 연사 중에는 화면이 반동 패턴을 따라 '누적'으로 밀려 올라가고(발사 사이
	// 감쇠 없음), 사격을 멈추면 짧은 지연 후 원래 조준점으로 부드럽게 복귀한다(리코일 회복).
	// 탄도(위 Pattern)와 연출이 같은 패턴을 공유하므로, 스케일 1.0이면 연사 중 탄이 대략 크로스헤어
	// 위치에 맺힌다 → "밀려 올라가는 크로스헤어를 마우스로 끌어내려 타겟에 붙잡아 두는" 발로란트식
	// 반동 컨트롤이 성립한다. FollowCamera의 상대 회전에만 적용되므로 조준(컨트롤 회전)과 서버 탄도에는
	// 전혀 영향이 없다.

	// 이번 발 패턴 증분 × 이 배율 = 화면 킥 증가량. 1.0 = 화면이 패턴을 그대로 따라감(권장).
	// 낮추면 화면 상승이 탄착보다 작아져 탄이 크로스헤어 위쪽에 맺히고(CS 느낌), 0이면 화면 고정.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil|ViewPunch", meta=(ClampMin="0.0"))
	float ViewPunchScale = 1.0f;

	// 마지막 발사 후 복귀를 시작하기까지의 대기 시간(초). 연사 간격(TimeBetweenShots)보다 길어야
	// 연사 도중에 화면이 도로 내려가지 않고 패턴 위치를 유지한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil|ViewPunch", meta=(ClampMin="0.0"))
	float ViewPunchRecoveryDelaySeconds = 0.15f;

	// 원위치 복귀 속도. 클수록 빠릿하게, 작을수록 부드럽게 되돌아온다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil|ViewPunch", meta=(ClampMin="0.1"))
	float ViewPunchRecoverySpeed = 10.0f;

	// 화면 킥 누적 상한(도). 밴달 패턴 총 상승(약 10.5도)을 온전히 담도록 여유 있게 잡는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Recoil|ViewPunch", meta=(ClampMin="0.0"))
	float ViewPunchMaxDegrees = 12.0f;
};

USTRUCT(BlueprintType)
struct FValorWeaponConfig
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	FName WeaponId = TEXT("Rifle_Default");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	FText DisplayName;

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	int32 MagazineSize = 25;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	int32 MaxReserveAmmo = 75;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float TimeBetweenShots = 0.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float ReloadDuration = 1.9f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float TraceDistanceCm = 50000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float ADSFieldOfView = 82.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float ADSInterpSpeed = 18.0f;

	// 발로란트처럼 서서 쏘는 첫 발은 조준점에 정확히 맞도록 0으로 둔다(랜덤 스프레드 없음).
	// 스프레이의 모양은 결정적 반동 패턴(RecoilProfile)이 그리고, 아래 랜덤 스프레드는 후반부에 아주 약간의
	// 흔들림만 더한다. 값을 키우면 패턴이 흐려지고, 줄이면 더 결정적(학습 가능)으로 또렷해진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float BaseFirstShotSpreadDegrees = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float AdditionalSpreadPerShotDegrees = 0.06f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float MaxSpreadDegrees = 4.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float SpreadRecoveryDelay = 0.18f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float MovementSpreadDegrees = 2.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float WalkingSpreadDegrees = 0.85f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float CrouchSpreadMultiplier = 0.75f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float ADSSpreadMultiplier = 0.65f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float CrouchRecoilMultiplier = 0.85f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float ADSRecoilMultiplier = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float PenetrationDepthCm = 45.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	float PenetrationDamageMultiplier = 0.7f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	EValorWallPenetrationTier PenetrationTier = EValorWallPenetrationTier::Medium;

	// 총기별 스프레이/반동 전체 정의다. 발로란트식 결정적 패턴 + 회복을 한곳에 모아 확장성을 확보한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Weapon")
	FValorRecoilProfile RecoilProfile;

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
};

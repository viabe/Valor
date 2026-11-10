#pragma once

#include "CoreMinimal.h"
#include "ValorViewmodelTypes.generated.h"

class AActor;
class USkinnedMeshComponent;

// =====================================================================================================
// 1인칭 뷰모델(내 화면의 총) 공용 타입
//
// 발로란트처럼 내 화면과 남의 화면의 총이 다르다.
//   - 내 화면(로컬 플레이어): 총이 카메라 기준 위치에 붙는다. 구도, ADS 조준선 정렬, 사격 킥, 시선 흔들림을 1인칭 전용으로 정한다.
//   - 남의 화면: 지금처럼 3인칭 몸의 손 소켓에 붙는다.
// 판정(탄도)은 원래 카메라 기준이라 총 위치는 순수 연출이다. 그래서 1인칭 배치는 복제도 서버 비용도 없다.
//
// 좌표 규칙
//   - 뷰 공간: 카메라 기준. X = 앞, Y = 오른쪽, Z = 위. 단위 cm는 실제 크기 기준이다(축소 배율 적용 전).
//   - 총 리그: 가져온 총 14종 모두 Root 뼈가 손잡이 위치에 있고, 축이 Z = 총구 방향, Y = 총의 아래, X = 총의 왼쪽이다.
//     무기 메시는 이 뼈(또는 GripSocket)가 액터 원점과 겹치도록 정렬되므로, 액터 회전 = 리그 축 → 원하는 방향 변환이다.
// =====================================================================================================

/**
 * 무기별 1인칭 배치. 값은 모두 뷰 공간(카메라 기준, 실제 크기 cm·도)이다.
 * 데이터 에셋에 없던 필드라 기존 DA_* 에셋은 이 기본값을 그대로 쓴다. 총마다 다르게 하려면 DA에서 덮어쓴다.
 */
USTRUCT(BlueprintType)
struct FValorViewmodelConfig
{
	GENERATED_BODY()

	// 힙(비조준) 자세에서 손잡이(그립) 위치. 손잡이가 화면 오른쪽 아래 구석 근처에 오고, 총구가 크로스헤어 오른쪽 아래에 오도록 정했다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Viewmodel")
	FVector HipLocation = FVector(45.0f, 27.0f, -22.0f);

	// 힙 자세의 추가 회전(뷰 공간). Pitch 위 +, Yaw 오른쪽 +, Roll 시계 방향 +.
	// 총구를 살짝 숙여 총열 윗면이 보이게 한다(가늠쇠가 손잡이보다 높아 회전이 없으면 총구가 떠 보인다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Viewmodel")
	FRotator HipRotation = FRotator(-3.5f, 2.0f, 0.0f);

	// 조준 자세에서 카메라부터 손잡이까지 앞쪽 거리(cm).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Viewmodel", meta=(ClampMin="0.0"))
	float ADSForwardCm = 30.0f;

	// 조준선 높이(cm): 손잡이 축에서 눈높이까지. 조준 시 총을 이만큼 내려 조준기가 화면 중앙에 오게 한다.
	// 0이면 메시 크기로 자동 계산한다(총 윗면 높이 - AutoSightBelowTopCm). 가져온 총에는 조준기 뼈가 없어서 쓰는 근사다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Viewmodel", meta=(ClampMin="0.0"))
	float SightHeightCm = 0.0f;

	// 조준 자세 좌우 보정(cm, 오른쪽 +). 조준기가 총 중심선에서 벗어난 총에 쓴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Viewmodel")
	float ADSRightCm = 0.0f;

	// 한 발마다 총이 뒤로 밀리는 거리(cm, 최고점 기준). 조준 중에는 ADSKickScale을 곱한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Viewmodel|Kick", meta=(ClampMin="0.0"))
	float KickBackCm = 2.5f;

	// 한 발마다 총구가 들리는 각도(도, 최고점 기준).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Viewmodel|Kick", meta=(ClampMin="0.0"))
	float KickPitchDegrees = 3.0f;

	// 좌우 무작위 흔들림 최대(±도).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Viewmodel|Kick", meta=(ClampMin="0.0"))
	float KickYawDegrees = 0.6f;

	// 킥이 최고점에 닿는 시간(초). 카메라 킥과 같은 임계 감쇠 스프링이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Viewmodel|Kick", meta=(ClampMin="0.005"))
	float KickPeakTimeSeconds = 0.035f;

	// 조준 중 킥 배율(조준기가 화면 중앙에서 크게 벗어나지 않게 줄인다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Viewmodel|Kick", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ADSKickScale = 0.35f;
};

/** 한 프레임의 뷰모델 자세(뷰 공간, 실제 크기). 순수 계산 결과라 테스트로 검증한다. */
struct FValorViewmodelPose
{
	FVector Location = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
};

namespace ValorViewmodel
{
	// 무기 메시의 손잡이 기준점 이름. 데이터 에셋의 GripSocket이 있으면 그것을, 없으면 리그 공통 Root 뼈를 쓴다.
	// 소켓 조회 함수는 뼈 이름도 받으므로 Root 뼈를 소켓처럼 쓸 수 있다. 둘 다 없으면 NAME_None(메시 원점 그대로).
	// 왜: 가져온 총 14종 중 GripSocket은 밴달에만 있다. Root 뼈는 14종 모두 손잡이에 있고 축 규칙도 같다.
	VALOR_API FName ResolveGripReferenceName(const USkinnedMeshComponent& Mesh, FName PreferredSocketName);

	// 총 리그 축(Z 총구, Y 아래, X 왼쪽) → 뷰 공간(X 앞, Z 위, Y 오른쪽) 회전.
	VALOR_API FQuat GripToViewRotation();

	// 총구(리그 Z)가 MuzzleDirection을 향하고 총의 위(리그 -Y)가 UpHint 쪽인(좌우 기울임 없는) 총 리그의 월드 회전.
	// 3인칭 조준 정렬에서 쓴다. UpHint는 MuzzleDirection과 평행하지 않아야 한다(시선의 위 방향을 넘긴다).
	VALOR_API FQuat MakeLevelRigRotation(const FVector& MuzzleDirection, const FVector& UpHint);

	// 1인칭 팔(First Person 템플릿 SK_Mannequin_Arms)의 오른손 GripPoint 소켓 축 규칙으로 바꾸는 회전(총 리그 공간 기준).
	// GripPoint 축(템플릿 소총 대기 자세에서 잼): X = 총의 왼쪽, Y = 총열(앞), Z = 위. 총 리그는 X = 왼쪽, Y = 아래, Z = 총구.
	// GripPoint 프레임(월드) = 이 회전 x 총 손잡이 프레임(월드).
	VALOR_API FQuat RigToArmsGripPointRotation();

	// 조준선 높이: 설정값이 있으면 그 값, 없으면 메시 윗면 높이 - AutoSightBelowTopCm(최소 0).
	VALOR_API float ResolveSightHeightCm(const FValorViewmodelConfig& Config, float MeshTopAboveGripCm);

	// 자동 조준선이 총 윗면보다 얼마나 아래인가(cm). 0이면 총 윗면이 화면 중앙선에 정확히 닿고, 양수면 가늠쇠 끝이 중앙선 위로 살짝 올라온다.
	inline constexpr float AutoSightBelowTopCm = 1.0f;

	// 힙 ↔ 조준 자세를 ADSAlpha(0 = 힙, 1 = 조준)로 섞는다. 조준 자세는 총구가 시선과 평행하고 조준선이 화면 중앙이다.
	VALOR_API FValorViewmodelPose BlendHipToADS(const FValorViewmodelConfig& Config, float SightHeightCm, float ADSAlpha);

	// 장착 중 총이 아래에서 올라오는 비율: 진행도 0 → 0(완전히 내림), 1 → 1(제자리). 처음에 빨리 올라오고 끝에서 감속한다.
	VALOR_API float EquipRaiseAlpha(float EquipProgress);

	// 메시 경계 상자(컴포넌트 공간)에서 총 윗면이 손잡이 축보다 몇 cm 위인가. 자동 조준선 높이의 입력이다.
	// GripComponentTransform의 스케일은 무시한다(가져온 리그의 Root 뼈는 스케일 100을 가진다 — 단위 변환 흔적).
	VALOR_API float ComputeTopAboveGripCm(const FBox& MeshBoundsComponentSpace, const FTransform& GripComponentTransform);

	// 손에 드는 무기 액터를 소유자 시점에 맞는 곳에 붙인다(모든 머신에서 각자 호출).
	//   - 소유자가 이 머신의 로컬 플레이어면 1인칭 뷰모델 피벗(카메라 자식)에 붙이고, 그림자를 끈다.
	//     1인칭 총은 카메라 가까이에 축소돼 있어 그림자가 바닥에 엉뚱한 크기로 떨어지기 때문이다(발로란트도 1인칭 그림자는 자기 손·총에만 진다).
	//   - 아니면 3인칭 손 소켓에 붙이고 그림자를 켠다.
	// 이미 같은 곳에 붙어 있으면 다시 붙이지 않는다. 1인칭이면 true.
	VALOR_API bool AttachHeldItemToOwnerView(AActor& HeldItem, AActor* OwnerActor);
}

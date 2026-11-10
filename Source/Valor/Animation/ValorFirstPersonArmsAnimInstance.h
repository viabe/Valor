#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "ValorFirstPersonArmsAnimInstance.generated.h"

class UAnimSequence;

/** 게임 스레드(뷰모델 컴포넌트)가 매 프레임 넘기는 손 목표. 모두 월드 공간이다. */
struct FValorArmsHandTargets
{
	// 총 손잡이 프레임을 팔 리그의 GripPoint 축 규칙으로 바꾼 것(ValorViewmodel::RigToArmsGripPointRotation).
	// 오른손은 기본 자세의 "손 ↔ GripPoint" 관계를 그대로 유지한 채 여기에 붙는다(손가락이 쥔 모양 그대로 총이 손에 들어온다).
	FTransform GripPointWorld = FTransform::Identity;

	// 왼손(손목 뼈) 위치. 회전은 기본 자세의 "왼손 ↔ GripPoint" 관계를 유지한다.
	FVector LeftHandWorld = FVector::ZeroVector;

	bool bRightHand = false;
	bool bLeftHand = false;

	// true = 소총 대기 자세(양손으로 총을 쥔 손가락), false = 맨손 대기 자세(칼처럼 한 손만 쓸 때 왼팔).
	bool bUseRiflePose = true;
};

/**
 * 1인칭 팔 애니메이션 프록시(워커 스레드에서 자세 계산).
 *
 * 자세 = 기본 시퀀스(소총/맨손 대기, 손가락·쇄골·팔꿈치 방향) + 양팔 투본 IK(손을 총에 붙임).
 * 왜 애님 블루프린트가 아니라 C++인가: 이 프로젝트는 C++ 위주이고, 손 목표가 매 프레임 총(뷰모델)에서 계산되므로
 *   그래프 노드 두 개(IK)를 위해 에셋을 만들고 변수를 연결하는 것보다 코드 한 곳이 추적하기 쉽다.
 *   그래프가 없으므로 Evaluate를 직접 구현한다(엔진이 그래프 대신 이 함수를 부른다).
 * 네트워크: 없음. 로컬 플레이어 화면에만 존재하는 연출이다.
 */
struct FValorFirstPersonArmsProxy : public FAnimInstanceProxy
{
	FValorFirstPersonArmsProxy() = default;
	explicit FValorFirstPersonArmsProxy(UAnimInstance* InAnimInstance);

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	struct FArmBones
	{
		int32 Upper = INDEX_NONE;
		int32 Lower = INDEX_NONE;
		int32 Hand = INDEX_NONE;
	};

	void CacheBoneIndices(const USkeletalMesh* SkeletalMesh);
	static void SolveArm(FCSPose<FCompactPose>& CSPose, const FArmBones& Arm, const FTransform& HandTargetCS);

	// 기본 시퀀스(소유 애님 인스턴스의 UPROPERTY가 GC에서 지킨다).
	const UAnimSequence* RiflePose = nullptr;
	const UAnimSequence* IdlePose = nullptr;
	float PoseTime = 0.0f;

	FValorArmsHandTargets Targets;
	FTransform GripPointCS = FTransform::Identity;
	FVector LeftHandCS = FVector::ZeroVector;

	FArmBones RightArm;
	FArmBones LeftArm;

	// 오른손 GripPoint 소켓(템플릿에서는 middle_01_r 뼈에 달려 있다).
	int32 GripSocketBone = INDEX_NONE;
	FTransform GripSocketLocal = FTransform::Identity;
};

/**
 * 1인칭 팔 애님 인스턴스. 뷰모델 컴포넌트가 기본 시퀀스와 매 프레임 손 목표를 넣어 준다.
 */
UCLASS(Transient, NotBlueprintable)
class VALOR_API UValorFirstPersonArmsAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	void SetPoses(UAnimSequence* InRiflePose, UAnimSequence* InIdlePose);
	void SetHandTargets(const FValorArmsHandTargets& InTargets) { HandTargets = InTargets; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	friend struct FValorFirstPersonArmsProxy;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> RiflePose;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdlePose;

	FValorArmsHandTargets HandTargets;
};

#include "Animation/ValorFirstPersonArmsAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "TwoBoneIK.h"

namespace
{
	// First Person 템플릿 팔(SK_Mannequin_Arms, UE5 마네킹 뼈 이름) 기준.
	const FName RightUpperArmName(TEXT("upperarm_r"));
	const FName RightLowerArmName(TEXT("lowerarm_r"));
	const FName RightHandName(TEXT("hand_r"));
	const FName LeftUpperArmName(TEXT("upperarm_l"));
	const FName LeftLowerArmName(TEXT("lowerarm_l"));
	const FName LeftHandName(TEXT("hand_l"));
	const FName GripPointSocketName(TEXT("GripPoint"));

	// 손이 목표에 조금 못 미치면 팔을 이 비율까지 늘려 붙인다(총에서 손이 떨어져 보이는 것보다 덜 눈에 띈다).
	constexpr double MaxArmStretch = 1.15;

	// 팔꿈치 방향 힌트를 기본 자세의 팔꿈치에서 굽은 쪽으로 이만큼(cm) 더 밀어, 손이 크게 움직여도 팔꿈치가 뒤집히지 않게 한다.
	constexpr double ElbowHintPushCm = 30.0;
}

FValorFirstPersonArmsProxy::FValorFirstPersonArmsProxy(UAnimInstance* InAnimInstance)
	: FAnimInstanceProxy(InAnimInstance)
{
}

void FValorFirstPersonArmsProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);

	const USkeletalMeshComponent* MeshComponent = InAnimInstance ? InAnimInstance->GetSkelMeshComponent() : nullptr;
	CacheBoneIndices(MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr);
}

void FValorFirstPersonArmsProxy::CacheBoneIndices(const USkeletalMesh* SkeletalMesh)
{
	RightArm = FArmBones();
	LeftArm = FArmBones();
	GripSocketBone = INDEX_NONE;
	if (!SkeletalMesh)
	{
		return;
	}

	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	RightArm = {RefSkeleton.FindBoneIndex(RightUpperArmName), RefSkeleton.FindBoneIndex(RightLowerArmName), RefSkeleton.FindBoneIndex(RightHandName)};
	LeftArm = {RefSkeleton.FindBoneIndex(LeftUpperArmName), RefSkeleton.FindBoneIndex(LeftLowerArmName), RefSkeleton.FindBoneIndex(LeftHandName)};

	if (const USkeletalMeshSocket* GripSocket = SkeletalMesh->FindSocket(GripPointSocketName))
	{
		GripSocketBone = RefSkeleton.FindBoneIndex(GripSocket->BoneName);
		GripSocketLocal = FTransform(GripSocket->RelativeRotation, GripSocket->RelativeLocation, GripSocket->RelativeScale);
	}
}

void FValorFirstPersonArmsProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	// 게임 스레드: 목표를 복사하고 팔 컴포넌트 공간으로 바꿔 둔다(워커 스레드의 Evaluate는 게임 오브젝트를 읽지 않는다).
	if (const UValorFirstPersonArmsAnimInstance* ArmsInstance = Cast<UValorFirstPersonArmsAnimInstance>(InAnimInstance))
	{
		RiflePose = ArmsInstance->RiflePose;
		IdlePose = ArmsInstance->IdlePose;
		Targets = ArmsInstance->HandTargets;
	}

	const FTransform& ArmsComponentTransform = GetComponentTransform();
	GripPointCS = Targets.GripPointWorld.GetRelativeTransform(ArmsComponentTransform);
	GripPointCS.SetScale3D(FVector::OneVector);
	LeftHandCS = ArmsComponentTransform.InverseTransformPosition(Targets.LeftHandWorld);
}

void FValorFirstPersonArmsProxy::Update(float DeltaSeconds)
{
	PoseTime += DeltaSeconds;
}

bool FValorFirstPersonArmsProxy::Evaluate(FPoseContext& Output)
{
	// 1) 기본 자세: 대기 시퀀스를 반복 재생한다(숨쉬기 흔들림은 IK로 손이 총에 고정되므로 어깨·팔꿈치에만 남는다).
	const UAnimSequence* BasePose = Targets.bUseRiflePose ? RiflePose : IdlePose;
	if (BasePose && BasePose->GetPlayLength() > UE_KINDA_SMALL_NUMBER)
	{
		FAnimationPoseData PoseData(Output);
		const double SampleTime = FMath::Fmod(static_cast<double>(PoseTime), static_cast<double>(BasePose->GetPlayLength()));
		BasePose->GetAnimationPose(PoseData, FAnimExtractContext(SampleTime, false));
	}
	else
	{
		Output.ResetToRefPose();
	}

	const FBoneContainer& BoneContainer = Output.Pose.GetBoneContainer();
	auto ToCompact = [&BoneContainer](int32 MeshIndex)
	{
		return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
	};

	const FCompactPoseBoneIndex GripBone = ToCompact(GripSocketBone);
	const FCompactPoseBoneIndex RightHand = ToCompact(RightArm.Hand);
	const FCompactPoseBoneIndex LeftHand = ToCompact(LeftArm.Hand);
	if ((!Targets.bRightHand && !Targets.bLeftHand) || !GripBone.IsValid() || !RightHand.IsValid() || !LeftHand.IsValid())
	{
		return true;
	}

	FCSPose<FCompactPose> CSPose;
	CSPose.InitPose(Output.Pose);

	// 2) 기본 자세에서 두 손이 GripPoint(템플릿 총 손잡이)에 대해 어디 있는지 먼저 잰다(팔을 고치기 전 값이어야 한다).
	const FTransform BaseGripCS = GripSocketLocal * CSPose.GetComponentSpaceTransform(GripBone);
	const FTransform RightHandInGrip = CSPose.GetComponentSpaceTransform(RightHand).GetRelativeTransform(BaseGripCS);
	const FTransform LeftHandInGrip = CSPose.GetComponentSpaceTransform(LeftHand).GetRelativeTransform(BaseGripCS);

	// 3) 같은 관계를 지금 총의 손잡이에 적용해 손 목표를 만들고, 투본 IK로 팔을 맞춘다.
	if (Targets.bRightHand)
	{
		SolveArm(CSPose, RightArm, RightHandInGrip * GripPointCS);
	}

	if (Targets.bLeftHand)
	{
		FTransform LeftTarget = LeftHandInGrip * GripPointCS;
		LeftTarget.SetLocation(LeftHandCS);
		SolveArm(CSPose, LeftArm, LeftTarget);
	}

	// 팔 일부만 컴포넌트 공간으로 바꿨으므로 안전 변환(부모가 로컬 공간이어도 올바르게 계산)을 쓴다.
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(CSPose, Output.Pose);
	return true;
}

void FValorFirstPersonArmsProxy::SolveArm(FCSPose<FCompactPose>& CSPose, const FArmBones& Arm, const FTransform& HandTargetCS)
{
	const FBoneContainer& BoneContainer = CSPose.GetPose().GetBoneContainer();
	const FCompactPoseBoneIndex Upper = Arm.Upper == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(Arm.Upper));
	const FCompactPoseBoneIndex Lower = Arm.Lower == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(Arm.Lower));
	const FCompactPoseBoneIndex Hand = Arm.Hand == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(Arm.Hand));
	if (!Upper.IsValid() || !Lower.IsValid() || !Hand.IsValid())
	{
		return;
	}

	FTransform UpperCS = CSPose.GetComponentSpaceTransform(Upper);
	FTransform LowerCS = CSPose.GetComponentSpaceTransform(Lower);
	FTransform HandCS = CSPose.GetComponentSpaceTransform(Hand);

	// 팔꿈치 힌트: 기본 자세의 팔꿈치를 어깨-손 중간점에서 멀어지는 쪽으로 더 민다(굽는 방향 유지).
	const FVector Elbow = LowerCS.GetLocation();
	const FVector BendDirection = (Elbow - ((UpperCS.GetLocation() + HandCS.GetLocation()) * 0.5)).GetSafeNormal();
	const FVector ElbowHint = Elbow + (BendDirection * ElbowHintPushCm);

	AnimationCore::SolveTwoBoneIK(UpperCS, LowerCS, HandCS, ElbowHint, HandTargetCS.GetLocation(), true, 1.0, MaxArmStretch);
	HandCS.SetRotation(HandTargetCS.GetRotation());

	// 부모 → 자식 순서(뼈 번호 오름차순)여야 한다. 자식(손가락, 비틀림 뼈)은 새 손·팔을 따라간다.
	const FBoneTransform ArmTransforms[] = {FBoneTransform(Upper, UpperCS), FBoneTransform(Lower, LowerCS), FBoneTransform(Hand, HandCS)};
	CSPose.SafeSetCSBoneTransforms(ArmTransforms);
}

void UValorFirstPersonArmsAnimInstance::SetPoses(UAnimSequence* InRiflePose, UAnimSequence* InIdlePose)
{
	RiflePose = InRiflePose;
	IdlePose = InIdlePose;
}

FAnimInstanceProxy* UValorFirstPersonArmsAnimInstance::CreateAnimInstanceProxy()
{
	return new FValorFirstPersonArmsProxy(this);
}

#include "Weapons/ValorViewmodelTypes.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Interfaces/ValorWeaponOwnerInterface.h"

namespace ValorViewmodel
{
	namespace
	{
		const FName RigRootBoneName(TEXT("Root"));
	}

	FName ResolveGripReferenceName(const USkinnedMeshComponent& Mesh, FName PreferredSocketName)
	{
		if (!PreferredSocketName.IsNone() && Mesh.DoesSocketExist(PreferredSocketName))
		{
			return PreferredSocketName;
		}

		return Mesh.DoesSocketExist(RigRootBoneName) ? RigRootBoneName : NAME_None;
	}

	FQuat GripToViewRotation()
	{
		// 행 = 리그 축이 뷰 공간에서 가리키는 방향. 리그 X(총의 왼쪽) → 뷰 -Y, 리그 Y(총의 아래) → 뷰 -Z, 리그 Z(총구) → 뷰 +X.
		// 세 축이 오른손/왼손 규칙을 바꾸지 않는 순수 회전(행렬식 +1)이라 FQuat로 바로 바꿀 수 있다.
		static const FQuat Rotation(FMatrix(
			FVector(0.0f, -1.0f, 0.0f),
			FVector(0.0f, 0.0f, -1.0f),
			FVector(1.0f, 0.0f, 0.0f),
			FVector::ZeroVector));
		return Rotation;
	}

	FQuat MakeLevelRigRotation(const FVector& MuzzleDirection, const FVector& UpHint)
	{
		// 시선처럼 X = 앞, Z = 위인 프레임을 만들고, 1인칭과 같은 리그 -> 시선 축 변환을 얹는다.
		return FRotationMatrix::MakeFromXZ(MuzzleDirection, UpHint).ToQuat() * GripToViewRotation();
	}

	FQuat RigToArmsGripPointRotation()
	{
		// 행 = GripPoint 축이 총 리그 공간에서 가리키는 방향. GripPoint X(왼쪽) = 리그 X, GripPoint Y(총열) = 리그 Z, GripPoint Z(위) = 리그 -Y.
		static const FQuat Rotation(FMatrix(
			FVector(1.0f, 0.0f, 0.0f),
			FVector(0.0f, 0.0f, 1.0f),
			FVector(0.0f, -1.0f, 0.0f),
			FVector::ZeroVector));
		return Rotation;
	}

	float ResolveSightHeightCm(const FValorViewmodelConfig& Config, float MeshTopAboveGripCm)
	{
		if (Config.SightHeightCm > 0.0f)
		{
			return Config.SightHeightCm;
		}

		return FMath::Max(MeshTopAboveGripCm - AutoSightBelowTopCm, 0.0f);
	}

	FValorViewmodelPose BlendHipToADS(const FValorViewmodelConfig& Config, float SightHeightCm, float ADSAlpha)
	{
		const float Alpha = FMath::Clamp(ADSAlpha, 0.0f, 1.0f);
		const FVector ADSLocation(Config.ADSForwardCm, Config.ADSRightCm, -SightHeightCm);

		FValorViewmodelPose Pose;
		Pose.Location = FMath::Lerp(Config.HipLocation, ADSLocation, Alpha);
		// 조준 자세는 추가 회전 0(총구가 시선과 평행). 힙 회전은 수 도 수준이라 성분별 보간으로 충분하다.
		Pose.ViewRotation = FMath::Lerp(Config.HipRotation, FRotator::ZeroRotator, Alpha);
		return Pose;
	}

	float EquipRaiseAlpha(float EquipProgress)
	{
		const float Remaining = 1.0f - FMath::Clamp(EquipProgress, 0.0f, 1.0f);
		return 1.0f - (Remaining * Remaining * Remaining);
	}

	float ComputeTopAboveGripCm(const FBox& MeshBoundsComponentSpace, const FTransform& GripComponentTransform)
	{
		if (!MeshBoundsComponentSpace.IsValid)
		{
			return 0.0f;
		}

		const FTransform GripWithoutScale(GripComponentTransform.GetRotation(), GripComponentTransform.GetLocation());
		float MinDownAxis = TNumericLimits<float>::Max();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector CornerPoint(
				(Corner & 1) ? MeshBoundsComponentSpace.Max.X : MeshBoundsComponentSpace.Min.X,
				(Corner & 2) ? MeshBoundsComponentSpace.Max.Y : MeshBoundsComponentSpace.Min.Y,
				(Corner & 4) ? MeshBoundsComponentSpace.Max.Z : MeshBoundsComponentSpace.Min.Z);
			// 리그 Y = 총의 아래쪽이므로, 가장 작은 Y가 가장 높은 점이다.
			MinDownAxis = FMath::Min(MinDownAxis, static_cast<float>(GripWithoutScale.InverseTransformPosition(CornerPoint).Y));
		}

		return FMath::Max(-MinDownAxis, 0.0f);
	}

	bool AttachHeldItemToOwnerView(AActor& HeldItem, AActor* OwnerActor)
	{
		IValorWeaponOwnerInterface* WeaponOwner = Cast<IValorWeaponOwnerInterface>(OwnerActor);
		USceneComponent* ItemRoot = HeldItem.GetRootComponent();
		if (!WeaponOwner || !ItemRoot)
		{
			return false;
		}

		USceneComponent* FirstPersonParent = WeaponOwner->GetFirstPersonWeaponAttachComponent();
		USceneComponent* AttachParent = FirstPersonParent ? FirstPersonParent : WeaponOwner->GetWeaponAttachComponent();
		if (!AttachParent)
		{
			return false;
		}

		const bool bFirstPerson = FirstPersonParent != nullptr;
		const FName SocketName = bFirstPerson ? NAME_None : WeaponOwner->GetWeaponAttachSocketName();
		if (ItemRoot->GetAttachParent() != AttachParent || ItemRoot->GetAttachSocketName() != SocketName)
		{
			// 위치·회전은 부착점에 맞추고, 스케일은 아래에서 따로 정한다.
			HeldItem.AttachToComponent(AttachParent, FAttachmentTransformRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepRelative, false), SocketName);
		}

		if (bFirstPerson)
		{
			// 1인칭 피벗은 뷰모델 축소 배율을 가진다. 상대 스케일 1이면 그 배율을 그대로 받는다.
			ItemRoot->SetRelativeScale3D(FVector::OneVector);
		}
		else
		{
			// 손 뼈 축 → 총 리그 축 보정. 소켓에 그대로 붙이면 총구(리그 Z)가 손 뼈 Z(소총 자세에서 위쪽)를 따라 하늘을 향한다.
			ItemRoot->SetRelativeRotation(WeaponOwner->GetThirdPersonWeaponRotation());
			// 3인칭은 손 소켓 스케일과 상관없이 원래 크기(예전 SnapToTargetNotIncludingScale과 같은 결과).
			ItemRoot->SetWorldScale3D(FVector::OneVector);
		}

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(&HeldItem);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			PrimitiveComponent->SetCastShadow(!bFirstPerson);
		}

		return bFirstPerson;
	}
}

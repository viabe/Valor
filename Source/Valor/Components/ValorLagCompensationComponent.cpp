#include "ValorLagCompensationComponent.h"

namespace
{
	bool IntersectSegmentAABB(const FVector& Start, const FVector& End, const FVector& Min, const FVector& Max, float& OutHitT)
	{
		const FVector Direction = End - Start;
		float TMin = 0.0f;
		float TMax = 1.0f;

		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			const float AxisStart = Start[AxisIndex];
			const float AxisDirection = Direction[AxisIndex];
			const float AxisMin = Min[AxisIndex];
			const float AxisMax = Max[AxisIndex];

			if (FMath::IsNearlyZero(AxisDirection))
			{
				if (AxisStart < AxisMin || AxisStart > AxisMax)
				{
					return false;
				}

				continue;
			}

			const float InvDirection = 1.0f / AxisDirection;
			float T1 = (AxisMin - AxisStart) * InvDirection;
			float T2 = (AxisMax - AxisStart) * InvDirection;

			if (T1 > T2)
			{
				Swap(T1, T2);
			}

			TMin = FMath::Max(TMin, T1);
			TMax = FMath::Min(TMax, T2);
			if (TMin > TMax)
			{
				return false;
			}
		}

		OutHitT = TMin;
		return true;
	}
}

UValorLagCompensationComponent::UValorLagCompensationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(false);

	HitBoxes =
	{
		{TEXT("Head"), FVector(0.0f, 0.0f, 64.0f), FVector(14.0f, 14.0f, 14.0f), EValorHitZone::Head},
		{TEXT("Body"), FVector(0.0f, 0.0f, 30.0f), FVector(22.0f, 18.0f, 28.0f), EValorHitZone::Body},
		{TEXT("Leg"), FVector(0.0f, 0.0f, 4.0f), FVector(18.0f, 18.0f, 22.0f), EValorHitZone::Leg}
	};
}

void UValorLagCompensationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		RecordSnapshot();
	}
}

bool UValorLagCompensationComponent::ConfirmHitAtTime(const FVector& TraceStart, const FVector& ShotDirection, float MaxDistance, float RequestedWorldTimeSeconds, FValorLagCompHitResult& OutHitResult) const
{
	const FValorLagCompSnapshot* Snapshot = FindSnapshotForTime(RequestedWorldTimeSeconds);
	if (!Snapshot)
	{
		return false;
	}

	const FVector SegmentEnd = TraceStart + (ShotDirection.GetSafeNormal() * MaxDistance);
	float BestHitT = TNumericLimits<float>::Max();
	EValorHitZone BestZone = EValorHitZone::None;
	FVector BestImpactPoint = FVector::ZeroVector;

	for (const FValorLagCompHitBox& HitBox : HitBoxes)
	{
		float CurrentT = 0.0f;
		FVector CurrentImpactPoint = FVector::ZeroVector;
		if (!IntersectSegmentWithHitBox(Snapshot->ActorTransform, HitBox, TraceStart, SegmentEnd, CurrentT, CurrentImpactPoint))
		{
			continue;
		}

		if (CurrentT < BestHitT)
		{
			BestHitT = CurrentT;
			BestZone = HitBox.HitZone;
			BestImpactPoint = CurrentImpactPoint;
		}
	}

	if (BestZone == EValorHitZone::None)
	{
		return false;
	}

	OutHitResult.bHitConfirmed = true;
	OutHitResult.HitZone = BestZone;
	OutHitResult.ImpactPoint = BestImpactPoint;
	OutHitResult.HitDistance = FVector::Distance(TraceStart, BestImpactPoint);
	return true;
}

void UValorLagCompensationComponent::RecordSnapshot()
{
	if (!GetOwner())
	{
		return;
	}

	FValorLagCompSnapshot Snapshot;
	Snapshot.WorldTimeSeconds = GetWorld()->GetTimeSeconds();
	Snapshot.ActorTransform = GetOwner()->GetActorTransform();
	SnapshotHistory.Insert(Snapshot, 0);

	for (int32 SnapshotIndex = SnapshotHistory.Num() - 1; SnapshotIndex >= 0; --SnapshotIndex)
	{
		if ((Snapshot.WorldTimeSeconds - SnapshotHistory[SnapshotIndex].WorldTimeSeconds) <= MaxRecordTimeSeconds)
		{
			break;
		}

		SnapshotHistory.RemoveAt(SnapshotIndex);
	}
}

const FValorLagCompSnapshot* UValorLagCompensationComponent::FindSnapshotForTime(float RequestedWorldTimeSeconds) const
{
	if (SnapshotHistory.Num() == 0)
	{
		return nullptr;
	}

	const FValorLagCompSnapshot* BestSnapshot = &SnapshotHistory[0];
	float BestDistance = FMath::Abs(SnapshotHistory[0].WorldTimeSeconds - RequestedWorldTimeSeconds);

	for (const FValorLagCompSnapshot& Snapshot : SnapshotHistory)
	{
		const float CandidateDistance = FMath::Abs(Snapshot.WorldTimeSeconds - RequestedWorldTimeSeconds);
		if (CandidateDistance < BestDistance)
		{
			BestDistance = CandidateDistance;
			BestSnapshot = &Snapshot;
		}
	}

	return BestSnapshot;
}

bool UValorLagCompensationComponent::IntersectSegmentWithHitBox(const FTransform& SnapshotTransform, const FValorLagCompHitBox& HitBox, const FVector& SegmentStart, const FVector& SegmentEnd, float& OutT, FVector& OutImpactPoint) const
{
	const FTransform HitBoxTransform(SnapshotTransform.GetRotation(), SnapshotTransform.TransformPosition(HitBox.LocalOffset), SnapshotTransform.GetScale3D());
	const FVector LocalStart = HitBoxTransform.InverseTransformPosition(SegmentStart);
	const FVector LocalEnd = HitBoxTransform.InverseTransformPosition(SegmentEnd);

	const FVector LocalMin = -HitBox.Extent;
	const FVector LocalMax = HitBox.Extent;
	if (!IntersectSegmentAABB(LocalStart, LocalEnd, LocalMin, LocalMax, OutT))
	{
		return false;
	}

	const FVector LocalImpactPoint = FMath::Lerp(LocalStart, LocalEnd, OutT);
	OutImpactPoint = HitBoxTransform.TransformPosition(LocalImpactPoint);
	return true;
}

#include "ValorLagCompensationComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"

namespace ValorLagComp
{
#if ENABLE_DRAW_DEBUG
	// 1: 서버(권한)는 가장 최근 기록 자세를, 클라는 지금 보이는 자세를 그린다. PIE에서 판정 도형이 메시를 따라가는지 볼 때 쓴다.
	TAutoConsoleVariable<int32> CVarDrawHitShapes(
		TEXT("valor.DrawHitboxes"),
		0,
		TEXT("1이면 캐릭터 판정 도형을 그린다(머리 빨강, 몸 노랑, 다리 파랑)."),
		ECVF_Cheat);
#endif

	// 아래 교차 함수는 모두 도형 로컬 공간에서 선분 Start + t * Delta(t는 0~1)가 도형에 처음 들어가는 t를 구한다.
	// 시작점이 이미 안에 있으면 t = 0이다(초근접 사격).

	bool IntersectSegmentSphere(const FVector& Start, const FVector& Delta, const FVector& Center, float Radius, float& OutT)
	{
		const FVector Offset = Start - Center;
		const double C = Offset.SizeSquared() - FMath::Square(Radius);
		if (C <= 0.0)
		{
			OutT = 0.0f;
			return true;
		}

		const double B = Offset | Delta;
		if (B >= 0.0)
		{
			return false;
		}

		const double A = Delta.SizeSquared();
		const double Discriminant = (B * B) - (A * C);
		if (A <= UE_SMALL_NUMBER || Discriminant < 0.0)
		{
			return false;
		}

		const double T = (-B - FMath::Sqrt(Discriminant)) / A;
		if (T > 1.0)
		{
			return false;
		}

		OutT = static_cast<float>(T);
		return true;
	}

	// Z축 캡슐(원통 반길이 HalfLength, 반지름 Radius) = 원통 옆면 + 위아래 구. 시작점이 밖이면 첫 진입은 세 도형 진입 중 가장 이른 것이다
	// (원통 위아래 원판은 구 안쪽이라 원판으로 먼저 들어가는 경우는 없다).
	bool IntersectSegmentCapsule(const FVector& Start, const FVector& Delta, float HalfLength, float Radius, float& OutT)
	{
		const FVector Top(0.0f, 0.0f, HalfLength);
		const FVector ClosestOnAxis(0.0f, 0.0f, FMath::Clamp(Start.Z, -HalfLength, HalfLength));
		if (FVector::DistSquared(Start, ClosestOnAxis) <= FMath::Square(Radius))
		{
			OutT = 0.0f;
			return true;
		}

		float BestT = TNumericLimits<float>::Max();

		const double A = FMath::Square(Delta.X) + FMath::Square(Delta.Y);
		const double B = (Start.X * Delta.X) + (Start.Y * Delta.Y);
		const double C = FMath::Square(Start.X) + FMath::Square(Start.Y) - FMath::Square(Radius);
		if (A > UE_SMALL_NUMBER && C > 0.0 && B < 0.0)
		{
			const double Discriminant = (B * B) - (A * C);
			if (Discriminant >= 0.0)
			{
				const double T = (-B - FMath::Sqrt(Discriminant)) / A;
				const double Z = Start.Z + (T * Delta.Z);
				if (T <= 1.0 && FMath::Abs(Z) <= HalfLength)
				{
					BestT = static_cast<float>(T);
				}
			}
		}

		float CapT = 0.0f;
		if (IntersectSegmentSphere(Start, Delta, Top, Radius, CapT))
		{
			BestT = FMath::Min(BestT, CapT);
		}
		if (IntersectSegmentSphere(Start, Delta, -Top, Radius, CapT))
		{
			BestT = FMath::Min(BestT, CapT);
		}

		if (BestT > 1.0f)
		{
			return false;
		}

		OutT = BestT;
		return true;
	}

	bool IntersectSegmentBox(const FVector& Start, const FVector& Delta, const FVector& HalfExtent, float& OutT)
	{
		double TMin = 0.0;
		double TMax = 1.0;

		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			const double AxisStart = Start[AxisIndex];
			const double AxisDelta = Delta[AxisIndex];
			const double AxisExtent = HalfExtent[AxisIndex];

			if (FMath::IsNearlyZero(AxisDelta))
			{
				if (AxisStart < -AxisExtent || AxisStart > AxisExtent)
				{
					return false;
				}
				continue;
			}

			double T1 = (-AxisExtent - AxisStart) / AxisDelta;
			double T2 = (AxisExtent - AxisStart) / AxisDelta;
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

		OutT = static_cast<float>(TMin);
		return true;
	}

	bool IntersectSegmentShape(const FValorLagCompHitShape& Shape, const FTransform& ShapeTransform, const FVector& SegmentStart, const FVector& SegmentEnd, float& OutT)
	{
		// 선분을 도형 공간으로 옮기면 t는 그대로다(아핀 변환). 크기(스케일)도 같이 풀린다.
		const FVector LocalStart = ShapeTransform.InverseTransformPosition(SegmentStart);
		const FVector LocalDelta = ShapeTransform.InverseTransformPosition(SegmentEnd) - LocalStart;

		switch (Shape.ShapeType)
		{
		case EValorHitShapeType::Sphere:
			return IntersectSegmentSphere(LocalStart, LocalDelta, FVector::ZeroVector, Shape.Radius, OutT);
		case EValorHitShapeType::Capsule:
			return IntersectSegmentCapsule(LocalStart, LocalDelta, Shape.HalfLength, Shape.Radius, OutT);
		case EValorHitShapeType::Box:
			return IntersectSegmentBox(LocalStart, LocalDelta, Shape.HalfExtent, OutT);
		default:
			return false;
		}
	}
}

UValorLagCompensationComponent::UValorLagCompensationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(false);

	HeadBoneNames = {TEXT("head")};
	LegBoneKeywords = {TEXT("thigh"), TEXT("calf"), TEXT("foot"), TEXT("ball")};
}

void UValorLagCompensationComponent::BeginPlay()
{
	Super::BeginPlay();

	RebuildHitShapes();

	if (USkeletalMeshComponent* Mesh = GetPoseMesh())
	{
		// ACharacter 기본값(AlwaysTickPose)은 화면에 그려질 때만 뼈를 갱신한다. 서버는 데디케이티드면 아무것도 그리지 않고,
		// 리슨 호스트도 자기 몸(OwnerNoSee)이나 시야 밖 캐릭터는 그리지 않는다. 그러면 판정 도형이 마지막으로 그려진 자세에 멈추므로
		// 서버(권한)에서는 항상 뼈를 갱신한다. 클라는 사수 화면에 보이는 캐릭터만 예측 판정하므로 기본값 그대로 둔다.
		if (GetOwner() && GetOwner()->HasAuthority())
		{
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}

		// 이번 프레임의 애니메이션 평가(병렬 작업 포함, 메시 틱이 끝나야 완료)가 끝난 뒤 기록한다.
		AddTickPrerequisiteComponent(Mesh);
	}
}

void UValorLagCompensationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const bool bHasAuthority = GetOwner() && GetOwner()->HasAuthority();
	if (bHasAuthority)
	{
		RecordSnapshot();
	}

#if ENABLE_DRAW_DEBUG
	// 내 캐릭터는 1인칭 카메라를 감싸므로 그리지 않는다(화면이 선으로 덮인다).
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (ValorLagComp::CVarDrawHitShapes.GetValueOnGameThread() > 0 && !(OwnerPawn && OwnerPawn->IsLocallyControlled()))
	{
		TArray<FTransform> Pose;
		if (!bHasAuthority || !GetLatestRecordedPose(Pose))
		{
			GetCurrentPose(Pose);
		}
		DrawHitShapes(Pose, -1.0f);
	}
#endif
}

USkeletalMeshComponent* UValorLagCompensationComponent::GetPoseMesh() const
{
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	return OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
}

EValorHitZone UValorLagCompensationComponent::ClassifyBone(FName BoneName) const
{
	if (HeadBoneNames.Contains(BoneName))
	{
		return EValorHitZone::Head;
	}

	const FString BoneString = BoneName.ToString();
	for (const FString& Keyword : LegBoneKeywords)
	{
		if (BoneString.Contains(Keyword, ESearchCase::IgnoreCase))
		{
			return EValorHitZone::Leg;
		}
	}

	return EValorHitZone::Body;
}

void UValorLagCompensationComponent::AddShape(const FValorLagCompHitShape& Shape)
{
	FValorLagCompHitShape PaddedShape = Shape;
	const float Padding = Shape.HitZone == EValorHitZone::Head ? HeadShapePadding : (Shape.HitZone == EValorHitZone::Leg ? LegShapePadding : BodyShapePadding);
	PaddedShape.Radius += Padding;
	PaddedShape.HalfExtent += FVector(Padding);
	HitShapes.Add(PaddedShape);
}

void UValorLagCompensationComponent::RebuildHitShapes()
{
	HitShapes.Reset();
	SnapshotHistory.Reset();

	const USkeletalMeshComponent* Mesh = GetPoseMesh();
	const UPhysicsAsset* PhysicsAsset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (PhysicsAsset)
	{
		for (const USkeletalBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
		{
			const int32 BoneIndex = BodySetup ? Mesh->GetBoneIndex(BodySetup->BoneName) : INDEX_NONE;
			if (BoneIndex == INDEX_NONE)
			{
				continue;
			}

			FValorLagCompHitShape Shape;
			Shape.BoneName = BodySetup->BoneName;
			Shape.BoneIndex = BoneIndex;
			Shape.HitZone = ClassifyBone(BodySetup->BoneName);

			const FKAggregateGeom& Geometry = BodySetup->AggGeom;
			for (const FKSphereElem& Sphere : Geometry.SphereElems)
			{
				Shape.ShapeType = EValorHitShapeType::Sphere;
				Shape.LocalTransform = Sphere.GetTransform();
				Shape.Radius = Sphere.Radius;
				AddShape(Shape);
			}
			for (const FKSphylElem& Sphyl : Geometry.SphylElems)
			{
				Shape.ShapeType = EValorHitShapeType::Capsule;
				Shape.LocalTransform = Sphyl.GetTransform();
				Shape.Radius = Sphyl.Radius;
				Shape.HalfLength = Sphyl.Length * 0.5f;
				AddShape(Shape);
			}
			for (const FKTaperedCapsuleElem& TaperedCapsule : Geometry.TaperedCapsuleElems)
			{
				// 양 끝 반지름이 다른 캡슐은 큰 쪽 반지름의 일반 캡슐로 근사한다(판정이 작아지는 쪽보다 낫다).
				Shape.ShapeType = EValorHitShapeType::Capsule;
				Shape.LocalTransform = TaperedCapsule.GetTransform();
				Shape.Radius = FMath::Max(TaperedCapsule.Radius0, TaperedCapsule.Radius1);
				Shape.HalfLength = TaperedCapsule.Length * 0.5f;
				AddShape(Shape);
			}
			for (const FKBoxElem& Box : Geometry.BoxElems)
			{
				Shape.ShapeType = EValorHitShapeType::Box;
				Shape.LocalTransform = Box.GetTransform();
				Shape.HalfExtent = FVector(Box.X, Box.Y, Box.Z) * 0.5f;
				AddShape(Shape);
			}
		}
	}

	if (HitShapes.Num() > 0)
	{
		return;
	}

	// 물리 에셋이 없으면 캡슐 콜리전 크기의 몸 판정 하나로 대신한다(헤드샷은 없다). 메시 설정이 빠졌다는 신호라 경고를 남긴다.
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (const UCapsuleComponent* Capsule = OwnerCharacter ? OwnerCharacter->GetCapsuleComponent() : nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: 캐릭터 메시에 물리 에셋이 없어 캡슐 하나로 판정합니다(헤드샷 불가)."), *GetNameSafe(GetOwner()));
		FValorLagCompHitShape Shape;
		Shape.ShapeType = EValorHitShapeType::Capsule;
		Shape.Radius = Capsule->GetUnscaledCapsuleRadius();
		Shape.HalfLength = FMath::Max(0.0f, Capsule->GetUnscaledCapsuleHalfHeight() - Shape.Radius);
		Shape.HitZone = EValorHitZone::Body;
		AddShape(Shape);
	}
}

FTransform UValorLagCompensationComponent::GetAuthoritativeMeshTransform() const
{
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* Mesh = GetPoseMesh();
	if (!OwnerCharacter || !Mesh)
	{
		return GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;
	}

	// 기본 오프셋은 앉기에 맞춰 엔진이 갱신한다(캡슐이 줄면 메시 위치도 옮겨진다).
	const FTransform BaseMeshOffset(OwnerCharacter->GetBaseRotationOffset(), OwnerCharacter->GetBaseTranslationOffset(), Mesh->GetRelativeScale3D());
	return BaseMeshOffset * OwnerCharacter->GetActorTransform();
}

void UValorLagCompensationComponent::BuildPose(const FTransform& MeshToWorld, TArray<FTransform>& OutPose) const
{
	OutPose.Reset(HitShapes.Num());

	const USkeletalMeshComponent* Mesh = GetPoseMesh();
	const bool bHasBones = Mesh && Mesh->GetNumComponentSpaceTransforms() > 0;
	const FTransform ActorTransform = GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;

	for (const FValorLagCompHitShape& Shape : HitShapes)
	{
		if (Shape.BoneIndex == INDEX_NONE || !bHasBones)
		{
			OutPose.Add(Shape.LocalTransform * ActorTransform);
			continue;
		}

		OutPose.Add(Shape.LocalTransform * Mesh->GetBoneTransform(Shape.BoneIndex, MeshToWorld));
	}
}

void UValorLagCompensationComponent::GetCurrentPose(TArray<FTransform>& OutPose) const
{
	const USkeletalMeshComponent* Mesh = GetPoseMesh();
	BuildPose(Mesh ? Mesh->GetComponentTransform() : GetAuthoritativeMeshTransform(), OutPose);
}

bool UValorLagCompensationComponent::GetLatestRecordedPose(TArray<FTransform>& OutPose) const
{
	if (SnapshotHistory.Num() == 0)
	{
		return false;
	}

	OutPose = SnapshotHistory[0].ShapeTransforms;
	return true;
}

bool UValorLagCompensationComponent::ConfirmHitAtTime(const FVector& TraceStart, const FVector& ShotDirection, float MaxDistance, float RequestedWorldTimeSeconds, FValorLagCompHitResult& OutHitResult) const
{
	TArray<FTransform> Pose;
	if (!GetPoseAtTime(RequestedWorldTimeSeconds, Pose))
	{
		return false;
	}

	return ConfirmHitAgainstPose(Pose, TraceStart, ShotDirection, MaxDistance, OutHitResult);
}

bool UValorLagCompensationComponent::ConfirmHitAtCurrentPose(const FVector& TraceStart, const FVector& ShotDirection, float MaxDistance, FValorLagCompHitResult& OutHitResult) const
{
	TArray<FTransform> Pose;
	GetCurrentPose(Pose);
	return ConfirmHitAgainstPose(Pose, TraceStart, ShotDirection, MaxDistance, OutHitResult);
}

bool UValorLagCompensationComponent::ConfirmHitAgainstPose(const TArray<FTransform>& Pose, const FVector& TraceStart, const FVector& ShotDirection, float MaxDistance, FValorLagCompHitResult& OutHitResult) const
{
	if (Pose.Num() != HitShapes.Num())
	{
		return false;
	}

	const FVector SegmentEnd = TraceStart + (ShotDirection.GetSafeNormal() * MaxDistance);
	float BestT = TNumericLimits<float>::Max();
	EValorHitZone BestZone = EValorHitZone::None;

	// 부위는 탄이 가장 먼저 들어간 도형으로 정한다. 목(몸)을 지나 머리로 가는 탄은 몸이고, 머리 앞을 가린 팔은 몸이다.
	for (int32 ShapeIndex = 0; ShapeIndex < HitShapes.Num(); ++ShapeIndex)
	{
		float CurrentT = 0.0f;
		if (ValorLagComp::IntersectSegmentShape(HitShapes[ShapeIndex], Pose[ShapeIndex], TraceStart, SegmentEnd, CurrentT) && CurrentT < BestT)
		{
			BestT = CurrentT;
			BestZone = HitShapes[ShapeIndex].HitZone;
		}
	}

	if (BestZone == EValorHitZone::None)
	{
		return false;
	}

	OutHitResult.bHitConfirmed = true;
	OutHitResult.HitZone = BestZone;
	OutHitResult.ImpactPoint = FMath::Lerp(TraceStart, SegmentEnd, BestT);
	OutHitResult.HitDistance = BestT * MaxDistance;
	return true;
}

void UValorLagCompensationComponent::RecordSnapshot()
{
	if (!GetOwner() || HitShapes.Num() == 0)
	{
		return;
	}

	FValorLagCompSnapshot Snapshot;
	Snapshot.WorldTimeSeconds = GetWorld()->GetTimeSeconds();
	BuildPose(GetAuthoritativeMeshTransform(), Snapshot.ShapeTransforms);
	SnapshotHistory.Insert(MoveTemp(Snapshot), 0);

	const float NewestTime = SnapshotHistory[0].WorldTimeSeconds;
	while (SnapshotHistory.Num() > 2 && (NewestTime - SnapshotHistory.Last().WorldTimeSeconds) > MaxRecordTimeSeconds)
	{
		SnapshotHistory.Pop(EAllowShrinking::No);
	}
}

bool UValorLagCompensationComponent::GetPoseAtTime(float RequestedWorldTimeSeconds, TArray<FTransform>& OutPose) const
{
	if (SnapshotHistory.Num() == 0)
	{
		return false;
	}

	// 기록 범위 밖이면 가까운 끝을 쓴다(너무 오래된 발사 시각은 최대 되감기 시간에서 멈춘다).
	if (RequestedWorldTimeSeconds >= SnapshotHistory[0].WorldTimeSeconds)
	{
		OutPose = SnapshotHistory[0].ShapeTransforms;
		return true;
	}

	for (int32 OlderIndex = 1; OlderIndex < SnapshotHistory.Num(); ++OlderIndex)
	{
		const FValorLagCompSnapshot& Older = SnapshotHistory[OlderIndex];
		if (Older.WorldTimeSeconds > RequestedWorldTimeSeconds)
		{
			continue;
		}

		// 앞뒤 두 기록 사이를 보간한다. 서버 틱 간격(60Hz면 16ms)만큼 생기던 위치 오차(달리기 속도로 최대 약 9cm)를 없앤다.
		const FValorLagCompSnapshot& Newer = SnapshotHistory[OlderIndex - 1];
		const float Span = Newer.WorldTimeSeconds - Older.WorldTimeSeconds;
		const float Alpha = Span > UE_KINDA_SMALL_NUMBER ? (RequestedWorldTimeSeconds - Older.WorldTimeSeconds) / Span : 1.0f;

		OutPose.SetNum(Newer.ShapeTransforms.Num());
		for (int32 ShapeIndex = 0; ShapeIndex < OutPose.Num(); ++ShapeIndex)
		{
			OutPose[ShapeIndex].Blend(Older.ShapeTransforms[ShapeIndex], Newer.ShapeTransforms[ShapeIndex], Alpha);
		}
		return true;
	}

	OutPose = SnapshotHistory.Last().ShapeTransforms;
	return true;
}

void UValorLagCompensationComponent::DrawHitShapes(const TArray<FTransform>& Pose, float LifeTimeSeconds) const
{
#if ENABLE_DRAW_DEBUG
	const UWorld* World = GetWorld();
	if (!World || Pose.Num() != HitShapes.Num())
	{
		return;
	}

	for (int32 ShapeIndex = 0; ShapeIndex < HitShapes.Num(); ++ShapeIndex)
	{
		const FValorLagCompHitShape& Shape = HitShapes[ShapeIndex];
		const FTransform& ShapeTransform = Pose[ShapeIndex];
		const FColor Color = Shape.HitZone == EValorHitZone::Head ? FColor::Red : (Shape.HitZone == EValorHitZone::Leg ? FColor::Blue : FColor::Yellow);
		const float Scale = static_cast<float>(ShapeTransform.GetMaximumAxisScale());
		const FVector Center = ShapeTransform.GetLocation();
		const FQuat Rotation = ShapeTransform.GetRotation();

		switch (Shape.ShapeType)
		{
		case EValorHitShapeType::Sphere:
			DrawDebugSphere(World, Center, Shape.Radius * Scale, 16, Color, false, LifeTimeSeconds, SDPG_Foreground);
			break;
		case EValorHitShapeType::Capsule:
			DrawDebugCapsule(World, Center, (Shape.HalfLength + Shape.Radius) * Scale, Shape.Radius * Scale, Rotation, Color, false, LifeTimeSeconds, SDPG_Foreground);
			break;
		case EValorHitShapeType::Box:
			DrawDebugBox(World, Center, Shape.HalfExtent * ShapeTransform.GetScale3D(), Rotation, Color, false, LifeTimeSeconds, SDPG_Foreground);
			break;
		default:
			break;
		}
	}
#endif
}

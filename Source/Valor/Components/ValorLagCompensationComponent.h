#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"
#include "ValorLagCompensationComponent.generated.h"

class USkeletalMeshComponent;

enum class EValorHitShapeType : uint8
{
	Sphere,
	Capsule,
	Box
};

// 판정 도형 하나. 캐릭터 메시의 물리 에셋 바디 도형을 그대로 옮겨 와 뼈에 붙인다.
// 그래서 애니메이션(조준 자세, 앉기, 달리기)을 따라가고, 메시를 바꾸면(요원별 체형) 판정도 같이 바뀐다.
// 도형의 뼈 기준 변환은 고정이고, 서버 기록(스냅샷)에는 도형별 월드 변환만 남긴다.
struct FValorLagCompHitShape
{
	FName BoneName = NAME_None;

	// INDEX_NONE이면 뼈 대신 캐릭터(액터) 기준이다(물리 에셋이 없을 때의 대체 캡슐).
	int32 BoneIndex = INDEX_NONE;

	// 뼈(또는 액터) 기준 도형 변환. 캡슐은 이 변환의 Z축 방향으로 놓인다(물리 에셋 Sphyl과 같은 규약).
	FTransform LocalTransform = FTransform::Identity;

	EValorHitShapeType ShapeType = EValorHitShapeType::Capsule;
	float Radius = 0.0f;
	float HalfLength = 0.0f;
	FVector HalfExtent = FVector::ZeroVector;
	EValorHitZone HitZone = EValorHitZone::Body;
};

namespace ValorLagComp
{
	// 선분 SegmentStart→SegmentEnd가 도형(월드 변환 ShapeTransform)에 처음 들어가는 지점의 비율 t(0~1). 시작점이 안이면 0.
	VALOR_API bool IntersectSegmentShape(const FValorLagCompHitShape& Shape, const FTransform& ShapeTransform, const FVector& SegmentStart, const FVector& SegmentEnd, float& OutT);
}

// 한 시점의 판정 자세. ShapeTransforms는 HitShapes와 같은 순서의 월드 변환이다.
struct FValorLagCompSnapshot
{
	float WorldTimeSeconds = 0.0f;
	TArray<FTransform> ShapeTransforms;
};

USTRUCT()
struct FValorLagCompHitResult
{
	GENERATED_BODY()

	UPROPERTY()
	bool bHitConfirmed = false;

	UPROPERTY()
	float HitDistance = 0.0f;

	UPROPERTY()
	FVector ImpactPoint = FVector::ZeroVector;

	UPROPERTY()
	EValorHitZone HitZone = EValorHitZone::None;
};

UCLASS(ClassGroup=(Valor), meta=(BlueprintSpawnableComponent))
class VALOR_API UValorLagCompensationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UValorLagCompensationComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 서버 전용: 발사 시각의 자세(앞뒤 기록을 보간)로 되감아 판정한다.
	bool ConfirmHitAtTime(const FVector& TraceStart, const FVector& ShotDirection, float MaxDistance, float RequestedWorldTimeSeconds, FValorLagCompHitResult& OutHitResult) const;

	// 되감기 없이 지금 자세로 판정한다(모든 머신). 사수 클라의 사격 예측(탄착·명중 연출)이 서버와 같은 판정 도형을 쓰게 한다.
	// 클라에는 기록(SnapshotHistory)이 없으므로 이 함수만 쓸 수 있다. 화면에 보이는 메시(네트워크 보정 위치 포함) 그대로 본다.
	bool ConfirmHitAtCurrentPose(const FVector& TraceStart, const FVector& ShotDirection, float MaxDistance, FValorLagCompHitResult& OutHitResult) const;

	// 메시나 물리 에셋을 바꾼 뒤(요원 선택 등) 다시 부른다. 기록은 도형 구성이 바뀌므로 비운다.
	void RebuildHitShapes();

	const TArray<FValorLagCompHitShape>& GetHitShapes() const { return HitShapes; }

	// 화면에 보이는 메시 기준 지금 자세(ConfirmHitAtCurrentPose가 쓰는 것과 같다).
	void GetCurrentPose(TArray<FTransform>& OutPose) const;

	// 서버 기록 중 가장 최근 자세. 기록이 없으면 false.
	bool GetLatestRecordedPose(TArray<FTransform>& OutPose) const;

	// 개발용: 도형을 부위별 색(머리 빨강, 몸 노랑, 다리 파랑)으로 그린다. 출시 빌드에서는 아무것도 하지 않는다.
	void DrawHitShapes(const TArray<FTransform>& Pose, float LifeTimeSeconds) const;

protected:
	UPROPERTY(EditDefaultsOnly, Category="Valor|LagComp")
	float MaxRecordTimeSeconds = 0.25f;

	// 이 이름의 뼈에 붙은 바디는 머리 판정이다(정확히 일치). 목은 발로란트처럼 몸으로 친다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|LagComp")
	TArray<FName> HeadBoneNames;

	// 뼈 이름에 이 단어가 들어가면 다리 판정이다(대소문자 무시, 트위스트 뼈 포함). 나머지(골반·척추·목·팔·손)는 몸이다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|LagComp")
	TArray<FString> LegBoneKeywords;

	// 부위별로 도형을 키우는 양(cm). 물리 에셋은 래그돌용이라 메시 겉면보다 약간 안쪽에 있을 수 있어 여기서 맞춘다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|LagComp")
	float HeadShapePadding = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|LagComp")
	float BodyShapePadding = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|LagComp")
	float LegShapePadding = 0.0f;

private:
	USkeletalMeshComponent* GetPoseMesh() const;
	EValorHitZone ClassifyBone(FName BoneName) const;
	void AddShape(const FValorLagCompHitShape& Shape);

	// 서버 기록용 메시 변환: 캐릭터(캡슐) 변환 x 메시 기본 오프셋. 리슨 서버가 원격 클라 메시에 거는 화면용 보정(스무딩)을 뺀다.
	FTransform GetAuthoritativeMeshTransform() const;
	void BuildPose(const FTransform& MeshToWorld, TArray<FTransform>& OutPose) const;

	void RecordSnapshot();
	bool GetPoseAtTime(float RequestedWorldTimeSeconds, TArray<FTransform>& OutPose) const;
	bool ConfirmHitAgainstPose(const TArray<FTransform>& Pose, const FVector& TraceStart, const FVector& ShotDirection, float MaxDistance, FValorLagCompHitResult& OutHitResult) const;

	TArray<FValorLagCompHitShape> HitShapes;

	// 최신이 앞(0번)이다.
	TArray<FValorLagCompSnapshot> SnapshotHistory;
};

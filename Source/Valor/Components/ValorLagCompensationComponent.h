#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"
#include "ValorLagCompensationComponent.generated.h"

USTRUCT()
struct FValorLagCompHitBox
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category="Valor|LagComp")
	FName Name = NAME_None;

	UPROPERTY(EditDefaultsOnly, Category="Valor|LagComp")
	FVector LocalOffset = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, Category="Valor|LagComp")
	FVector Extent = FVector(20.0f, 20.0f, 20.0f);

	UPROPERTY(EditDefaultsOnly, Category="Valor|LagComp")
	EValorHitZone HitZone = EValorHitZone::Body;
};

USTRUCT()
struct FValorLagCompSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	float WorldTimeSeconds = 0.0f;

	UPROPERTY()
	FTransform ActorTransform;
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

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	bool ConfirmHitAtTime(const FVector& TraceStart, const FVector& ShotDirection, float MaxDistance, float RequestedWorldTimeSeconds, FValorLagCompHitResult& OutHitResult) const;

protected:
	UPROPERTY(EditDefaultsOnly, Category="Valor|LagComp")
	float MaxRecordTimeSeconds = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|LagComp")
	TArray<FValorLagCompHitBox> HitBoxes;

private:
	void RecordSnapshot();
	const FValorLagCompSnapshot* FindSnapshotForTime(float RequestedWorldTimeSeconds) const;
	bool IntersectSegmentWithHitBox(const FTransform& SnapshotTransform, const FValorLagCompHitBox& HitBox, const FVector& SegmentStart, const FVector& SegmentEnd, float& OutT, FVector& OutImpactPoint) const;

	UPROPERTY()
	TArray<FValorLagCompSnapshot> SnapshotHistory;
};

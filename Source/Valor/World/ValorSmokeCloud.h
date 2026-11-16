#pragma once

#include "CoreMinimal.h"
#include "World/ValorAbilityActor.h"
#include "ValorSmokeCloud.generated.h"

class UStaticMeshComponent;

/**
 * 연막(시야 차단 구). 탄은 막지 않는다(장르 공통: 연막은 시야만 가린다). 수명이 끝나면 사라진다.
 * 바깥 면 + 안쪽을 향한 면(음수 스케일로 뒤집은 구) 두 겹이라 연막 밖에서도 안에서도 너머가 보이지 않는다.
 * 모양은 엔진 기본 구(지름 9m). 머티리얼·안개 연출은 디자인 때 BP에서 바꾼다.
 */
UCLASS()
class VALOR_API AValorSmokeCloud : public AValorAbilityActor
{
	GENERATED_BODY()

public:
	AValorSmokeCloud();

	float GetRadius() const { return RadiusCm; }

	// 이 위치가 연막 안인가(시야 판정·테스트용).
	bool ContainsLocation(const FVector& Location) const { return FVector::DistSquared(Location, GetActorLocation()) <= FMath::Square(RadiusCm); }

protected:
	UPROPERTY(EditDefaultsOnly, Category="Valor|Smoke", meta=(ClampMin="50.0"))
	float RadiusCm = 450.0f;

	UPROPERTY(VisibleAnywhere, Category="Valor|Smoke")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// 안쪽 면(안에서 볼 때 보이는 겹).
	UPROPERTY(VisibleAnywhere, Category="Valor|Smoke")
	TObjectPtr<UStaticMeshComponent> InnerMesh;
};

// 연막 통이 터뜨리는 연막(15초).
UCLASS()
class VALOR_API AValorSmokeCloud_Canister : public AValorSmokeCloud
{
	GENERATED_BODY()

public:
	AValorSmokeCloud_Canister() { LifetimeSeconds = 15.0f; }
};

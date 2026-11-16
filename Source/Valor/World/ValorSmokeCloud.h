#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValorSmokeCloud.generated.h"

class UStaticMeshComponent;

/**
 * 연막(시야 차단 구). 서버가 스폰하고 수명(SetLifeSpan)이 끝나면 사라진다. 탄은 막지 않는다(장르 공통: 연막은 시야만 가린다).
 * 지금 모양은 엔진 기본 구(지름 9m)라 바깥에서만 가려 보인다(안에서는 면이 안 보인다). 시야 차단 머티리얼·안쪽 안개는 5-2(스킬 부품)에서 다듬는다.
 */
UCLASS()
class VALOR_API AValorSmokeCloud : public AActor
{
	GENERATED_BODY()

public:
	AValorSmokeCloud();

	float GetRadius() const { return RadiusCm; }

protected:
	UPROPERTY(EditDefaultsOnly, Category="Valor|Smoke", meta=(ClampMin="50.0"))
	float RadiusCm = 450.0f;

	UPROPERTY(VisibleAnywhere, Category="Valor|Smoke")
	TObjectPtr<UStaticMeshComponent> Mesh;
};

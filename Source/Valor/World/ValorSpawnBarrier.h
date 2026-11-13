#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValorSpawnBarrier.generated.h"

class UStaticMeshComponent;

/**
 * 스폰 장벽(발로란트 구매 단계 장벽). 맵의 스폰 출구에 놓고 크기(스케일)를 출구에 맞춘다.
 *
 * 구매 단계 동안만 캐릭터(Pawn)를 막고 보인다. 탄·시야는 막지 않는다. 교전이 시작되면 사라진다.
 * 네트워크: 장벽 상태는 복제하지 않는다. 각 머신이 복제된 라운드 단계(GameState)를 보고 콜리전을 켜고 끈다.
 *   서버와 소유 클라가 같은 단계를 보므로 이동 예측과 서버 결과가 같다(단계가 바뀌는 순간 몇 프레임 차이는 보정으로 끝난다).
 * 트레이드오프: 모양은 엔진 기본 정육면체(100cm)다. 디자인이 생기면 BP에서 메시·머티리얼을 바꾼다.
 */
UCLASS()
class VALOR_API AValorSpawnBarrier : public AActor
{
	GENERATED_BODY()

public:
	AValorSpawnBarrier();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	bool IsBarrierActive() const { return bActive; }

protected:
	UPROPERTY(VisibleAnywhere, Category="Valor|Spawn")
	TObjectPtr<UStaticMeshComponent> Wall;

private:
	void ApplyActive(bool bNewActive);

	bool bActive = true;
	bool bInitialized = false;
};

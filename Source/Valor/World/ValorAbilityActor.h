#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValorMatchTypes.h"
#include "ValorAbilityActor.generated.h"

/**
 * 스킬로 생기는 모든 물체(투척물, 연막, 섬광, 정찰, 벽, 지속 회복)의 부모.
 * 누가(Instigator) 어느 팀으로 만들었는지 들고 있고, 라운드가 시작되면 경기 모드가 모두 지운다(DestroyAll).
 * 네트워크: 기본 복제(서버가 스폰). 서버 전용 물체는 하위 클래스가 bReplicates를 끈다.
 */
UCLASS(Abstract)
class VALOR_API AValorAbilityActor : public AActor
{
	GENERATED_BODY()

public:
	AValorAbilityActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;

	// 만든 사람의 팀(스폰할 때 Instigator에서 정해진다).
	EValorTeam GetOwnerTeam() const { return OwnerTeam; }

	// 수명(초). 0이면 스스로 사라지지 않는다. 스폰할 때 기본값으로 SetLifeSpan을 건다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Ability", meta=(ClampMin="0.0"))
	float LifetimeSeconds = 0.0f;

	// 서버: 월드의 스킬 물체를 모두 지운다(라운드 시작).
	static void DestroyAll(UWorld* World);

protected:
	UPROPERTY(Replicated)
	EValorTeam OwnerTeam = EValorTeam::None;
};

#pragma once

#include "CoreMinimal.h"
#include "World/ValorAbilityActor.h"
#include "ValorAbilityProjectile.generated.h"

class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * 던지는 스킬 부품. 날아가서(튕기거나 벽에 붙고) 정해진 때에 "효과"(PayloadClass: 연막·섬광·정찰 등)를 그 자리에 만든다.
 *
 * 터지는 때: 퓨즈 시간이 지났을 때(FuseSeconds > 0), 멈췄을 때(bDetonateOnStop), 붙었을 때(bStickOnHit + bDetonateOnStick).
 * 캐릭터는 통과한다(벽·바닥에만 부딪힌다). 효과는 서버만 만든다.
 * 네트워크: 서버가 날리고 이동을 복제한다(클라 예측 없음 — 손에서 나가는 연출은 이후 애니메이션과 함께).
 * 수치를 바꾼 하위 클래스(C++ 기본값 또는 BP)로 스킬마다 다른 투척물을 만든다.
 */
UCLASS()
class VALOR_API AValorAbilityProjectile : public AValorAbilityActor
{
	GENERATED_BODY()

public:
	AValorAbilityProjectile();

	virtual void BeginPlay() override;

	// 서버: 이 방향으로 던진다(속도는 InitialSpeed).
	void Launch(const FVector& Direction);

	// 서버: 지금 자리에 효과를 만들고 사라진다.
	void Detonate();

	UPROPERTY(EditDefaultsOnly, Category="Valor|Projectile")
	TSubclassOf<AValorAbilityActor> PayloadClass;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Projectile", meta=(ClampMin="0.0"))
	float InitialSpeed = 1800.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Projectile", meta=(ClampMin="0.0"))
	float GravityScale = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Projectile")
	bool bBounce = true;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Projectile", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Bounciness = 0.35f;

	// 처음 부딪힌 면에 붙는다(정찰 다트 등).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Projectile")
	bool bStickOnHit = false;

	// 0보다 크면 던진 뒤 이 시간에 터진다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Projectile", meta=(ClampMin="0.0"))
	float FuseSeconds = 0.0f;

	// 굴러가다 멈추면 터진다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Projectile")
	bool bDetonateOnStop = true;

	// 효과를 면에서 이만큼 띄워 만든다(연막이 바닥에 반쯤 묻히지 않게).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Projectile")
	float PayloadHeightOffset = 0.0f;

protected:
	UPROPERTY(VisibleAnywhere, Category="Valor|Projectile")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category="Valor|Projectile")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category="Valor|Projectile")
	TObjectPtr<UProjectileMovementComponent> Movement;

private:
	UFUNCTION()
	void HandleStop(const FHitResult& ImpactResult);

	UFUNCTION()
	void HandleBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity);

	bool bDetonated = false;
	FTimerHandle FuseTimerHandle;
};

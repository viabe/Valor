#pragma once

#include "CoreMinimal.h"
#include "World/ValorAbilityActor.h"
#include "World/ValorAbilityProjectile.h"
#include "ValorAbilityEffects.generated.h"

class AValorCharacter;
class UStaticMeshComponent;

/**
 * 섬광 효과(투척물이 터뜨림). 터지는 순간 서버가 반경 안의 플레이어마다 "가림 없이 보이는가 + 시선과의 각도"로 눈부심 시간을 정하고
 * 그 플레이어 컨트롤러에게만 보낸다(ClientFlashed). 아군·자기 자신도 눈이 부시다(장르 공통).
 * 눈부심은 화면 연출이다(서버 판정은 바꾸지 않는다). 규칙: ValorAbilityRules::ComputeFlashSeconds.
 */
UCLASS()
class VALOR_API AValorFlashBurst : public AValorAbilityActor
{
	GENERATED_BODY()

public:
	AValorFlashBurst();

	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Flash", meta=(ClampMin="0.0"))
	float RadiusCm = 2500.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Flash", meta=(ClampMin="0.0"))
	float FullBlindSeconds = 1.75f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Flash", meta=(ClampMin="0.0"))
	float PartialBlindSeconds = 0.6f;
};

/**
 * 정찰 효과: 반경 안, 가림 없이 보이는 적을 만든 사람의 팀 화면에 RevealSeconds 동안 표시한다(벽 너머로 보이는 표시).
 * 표시는 캐릭터의 복제 값(RevealedToTeam, 끝나는 서버 시각)이라 그 팀 모든 사람의 HUD가 같은 것을 본다.
 */
UCLASS()
class VALOR_API AValorReconPulse : public AValorAbilityActor
{
	GENERATED_BODY()

public:
	AValorReconPulse();

	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Recon", meta=(ClampMin="0.0"))
	float RadiusCm = 1500.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Recon", meta=(ClampMin="0.0"))
	float RevealSeconds = 3.0f;
};

/**
 * 벽: 캐릭터와 탄(Visibility 트레이스)을 막고, 체력이 다 깎이면 부서진다. 수명이 끝나도 사라진다.
 * 탄 피해: 서버 히트스캔이 벽에 막히면 그 무기의 몸통 피해를 벽에 준다(UValorCombatComponent).
 */
UCLASS()
class VALOR_API AValorAbilityWall : public AValorAbilityActor
{
	GENERATED_BODY()

public:
	AValorAbilityWall();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 서버 전용.
	void ApplyWallDamage(float Damage);
	float GetHealth() const { return Health; }

	UPROPERTY(EditDefaultsOnly, Category="Valor|Wall", meta=(ClampMin="1.0"))
	float MaxHealth = 800.0f;

	// 벽 크기(cm): 너비, 두께, 높이.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Wall")
	FVector Size = FVector(30.0f, 500.0f, 300.0f);

protected:
	virtual void PostInitializeComponents() override;

	UPROPERTY(VisibleAnywhere, Category="Valor|Wall")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(Replicated)
	float Health = 0.0f;
};

/**
 * 지속 회복(서버 전용, 대상에 붙음): DurationSeconds 동안 TotalHeal을 고르게 나눠 준다. 대상이 죽으면 멈춘다.
 */
UCLASS()
class VALOR_API AValorHealOverTime : public AValorAbilityActor
{
	GENERATED_BODY()

public:
	AValorHealOverTime();

	virtual void Tick(float DeltaSeconds) override;

	void SetTarget(AValorCharacter* InTarget) { Target = InTarget; }

	UPROPERTY(EditDefaultsOnly, Category="Valor|Heal", meta=(ClampMin="0.0"))
	float TotalHeal = 60.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Heal", meta=(ClampMin="0.1"))
	float DurationSeconds = 4.0f;

private:
	TWeakObjectPtr<AValorCharacter> Target;
	float Elapsed = 0.0f;
};

// === 투척물 프리셋(수치만 다른 하위 클래스) ===

// 연막 통: 튕기다 멈추면 연막.
UCLASS()
class VALOR_API AValorProjectile_Smoke : public AValorAbilityProjectile
{
	GENERATED_BODY()

public:
	AValorProjectile_Smoke();
};

// 섬광탄: 0.6초 뒤 공중·바닥 어디서든 터진다.
UCLASS()
class VALOR_API AValorProjectile_Flash : public AValorAbilityProjectile
{
	GENERATED_BODY()

public:
	AValorProjectile_Flash();
};

// 정찰 다트: 처음 닿은 면에 붙어 정찰 신호를 낸다.
UCLASS()
class VALOR_API AValorProjectile_Recon : public AValorAbilityProjectile
{
	GENERATED_BODY()

public:
	AValorProjectile_Recon();
};

/**
 * 화염 지대: 반경 안 살아 있는 캐릭터(팀 구분 없음, 자신 포함)에게 TickSeconds마다 초당 DamagePerSecond 피해를 준다.
 * 피해는 기존 피해 GE로 보낸다(만든 사람이 처치자, 킬피드 무기 칸은 비어 있음). 수명이 끝나면 꺼진다.
 */
UCLASS()
class VALOR_API AValorDamageZone : public AValorAbilityActor
{
	GENERATED_BODY()

public:
	AValorDamageZone();

	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditDefaultsOnly, Category="Valor|DamageZone", meta=(ClampMin="10.0"))
	float RadiusCm = 350.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|DamageZone", meta=(ClampMin="0.0"))
	float DamagePerSecond = 30.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|DamageZone", meta=(ClampMin="0.05"))
	float TickSeconds = 0.25f;

protected:
	virtual void PostInitializeComponents() override;

	UPROPERTY(VisibleAnywhere, Category="Valor|DamageZone")
	TObjectPtr<UStaticMeshComponent> Mesh;

private:
	void ApplyZoneDamage(AValorCharacter& Victim, float Damage);

	float Accumulated = 0.0f;
};

// 대형 화염(궁극기용): 반경 6m, 7초, 초당 40.
UCLASS()
class VALOR_API AValorDamageZone_Inferno : public AValorDamageZone
{
	GENERATED_BODY()

public:
	AValorDamageZone_Inferno();
};

// 넓은 정찰(궁극기용): 반경 50m, 4초 표시.
UCLASS()
class VALOR_API AValorReconPulse_Wide : public AValorReconPulse
{
	GENERATED_BODY()

public:
	AValorReconPulse_Wide();
};

// 소형 벽(시그니처용): 너비 2.5m, 높이 2m, 체력 400, 15초.
UCLASS()
class VALOR_API AValorAbilityWall_Small : public AValorAbilityWall
{
	GENERATED_BODY()

public:
	AValorAbilityWall_Small();
};

// 화염탄: 튕기다 멈추면 화염 지대.
UCLASS()
class VALOR_API AValorProjectile_Incendiary : public AValorAbilityProjectile
{
	GENERATED_BODY()

public:
	AValorProjectile_Incendiary();
};

// 대형 화염탄(궁극기): 멀리 날아가 멈추면 대형 화염.
UCLASS()
class VALOR_API AValorProjectile_Inferno : public AValorAbilityProjectile
{
	GENERATED_BODY()

public:
	AValorProjectile_Inferno();
};

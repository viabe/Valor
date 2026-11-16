#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/ValorAgentAbility.h"
#include "ValorSampleAbilities.generated.h"

class AValorAbilityProjectile;
class AValorAbilityWall;
class AValorHealOverTime;

// =====================================================================================================
// 스킬 부품(5-2)으로 만든 범용 스킬과 샘플 요원.
// 범용 스킬은 수치·부품 클래스만 바꿔 여러 요원에 쓴다(던지기, 이동, 지속 회복, 벽 세우기, 궁극기 재충전).
// =====================================================================================================

// 던지기: 시선 방향으로 투척물(ProjectileClass)을 던진다. 투척물이 정해진 때에 효과(연막·섬광·정찰)를 만든다.
UCLASS()
class VALOR_API UValorAbility_Throw : public UValorAgentAbility
{
	GENERATED_BODY()

protected:
	virtual void ActivateAgentAbility() override;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Ability")
	TSubclassOf<AValorAbilityProjectile> ProjectileClass;
};

// 이동: 보는 수평 방향 ForwardSpeed + 위로 UpSpeed로 튀어 오른다(돌진·도약). 소유 클라가 바로 움직이도록 예측 실행한다.
UCLASS()
class VALOR_API UValorAbility_Launch : public UValorAgentAbility
{
	GENERATED_BODY()

public:
	UValorAbility_Launch();

protected:
	virtual void ActivateAgentAbility() override;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Ability")
	float ForwardSpeed = 1600.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Ability")
	float UpSpeed = 150.0f;
};

// 지속 회복: 자신에게 지속 회복(EffectClass: 4초 동안 60)을 붙인다.
UCLASS()
class VALOR_API UValorAbility_HealOverTime : public UValorAgentAbility
{
	GENERATED_BODY()

public:
	UValorAbility_HealOverTime();

protected:
	virtual void ActivateAgentAbility() override;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Ability")
	TSubclassOf<AValorHealOverTime> EffectClass;
};

// 벽 세우기: 조준한 바닥(최대 MaxRangeCm)에 시선과 수직으로 벽을 세운다.
UCLASS()
class VALOR_API UValorAbility_DeployWall : public UValorAgentAbility
{
	GENERATED_BODY()

public:
	UValorAbility_DeployWall();

protected:
	virtual void ActivateAgentAbility() override;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Ability")
	TSubclassOf<AValorAbilityWall> WallClass;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Ability", meta=(ClampMin="100.0"))
	float MaxRangeCm = 1500.0f;
};

// 자기 자리에서 효과(PayloadClass: 정찰 펄스 등)를 바로 만든다.
UCLASS()
class VALOR_API UValorAbility_PulseAtSelf : public UValorAgentAbility
{
	GENERATED_BODY()

protected:
	virtual void ActivateAgentAbility() override;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Ability")
	TSubclassOf<class AValorAbilityActor> PayloadClass;
};

// 과충전(궁극기): 체력과 실드(중형 실드 50)를 가득 채운다.
UCLASS()
class VALOR_API UValorAbility_Overdrive : public UValorAgentAbility
{
	GENERATED_BODY()

protected:
	virtual void ActivateAgentAbility() override;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Ability", meta=(ClampMin="0.0"))
	float ShieldAmount = 50.0f;
};

// === 수치 프리셋(요원 칸 설정이 스킬 클래스로 고르므로 하위 클래스로 둔다. BP로도 만들 수 있다) ===

UCLASS()
class VALOR_API UValorAbility_ThrowSmoke : public UValorAbility_Throw
{
	GENERATED_BODY()

public:
	UValorAbility_ThrowSmoke();
};

UCLASS()
class VALOR_API UValorAbility_ThrowFlash : public UValorAbility_Throw
{
	GENERATED_BODY()

public:
	UValorAbility_ThrowFlash();
};

UCLASS()
class VALOR_API UValorAbility_ThrowRecon : public UValorAbility_Throw
{
	GENERATED_BODY()

public:
	UValorAbility_ThrowRecon();
};

UCLASS()
class VALOR_API UValorAbility_ThrowIncendiary : public UValorAbility_Throw
{
	GENERATED_BODY()

public:
	UValorAbility_ThrowIncendiary();
};

UCLASS()
class VALOR_API UValorAbility_ThrowInferno : public UValorAbility_Throw
{
	GENERATED_BODY()

public:
	UValorAbility_ThrowInferno();
};

// 넓은 정찰(자기 자리에서 반경 50m).
UCLASS()
class VALOR_API UValorAbility_Watchtower : public UValorAbility_PulseAtSelf
{
	GENERATED_BODY()

public:
	UValorAbility_Watchtower();
};

// 소형 벽 세우기.
UCLASS()
class VALOR_API UValorAbility_DeployWallSmall : public UValorAbility_DeployWall
{
	GENERATED_BODY()

public:
	UValorAbility_DeployWallSmall();
};

// 짧은 돌진: 수평 1100.
UCLASS()
class VALOR_API UValorAbility_DashShort : public UValorAbility_Launch
{
	GENERATED_BODY()

public:
	UValorAbility_DashShort() { ForwardSpeed = 1100.0f; UpSpeed = 100.0f; }
};

// 돌진: 수평 1600.
UCLASS()
class VALOR_API UValorAbility_Dash : public UValorAbility_Launch
{
	GENERATED_BODY()
};

// 도약: 위로 1100.
UCLASS()
class VALOR_API UValorAbility_Updraft : public UValorAbility_Launch
{
	GENERATED_BODY()

public:
	UValorAbility_Updraft();
};

// === 샘플 요원(이 프로젝트에서 만든 요원) ===

// Drift: C 지속 회복(200, 1회) / Q 연막 통(100, 2회) / E 돌진(매 라운드 무료) / X 과충전(6포인트).
UCLASS()
class VALOR_API UValorAgent_Drift : public UValorAgentDefinition
{
	GENERATED_BODY()

public:
	UValorAgent_Drift();
};

// Flare: C 도약(150, 2회) / Q 섬광탄(200, 2회) / E 정찰 다트(매 라운드 무료) / X 방벽(6포인트).
UCLASS()
class VALOR_API UValorAgent_Flare : public UValorAgentDefinition
{
	GENERATED_BODY()

public:
	UValorAgent_Flare();
};

// Cinder: C 섬광탄(150, 1회) / Q 화염탄(250, 1회) / E 짧은 돌진(매 라운드 무료) / X 대형 화염(7포인트).
UCLASS()
class VALOR_API UValorAgent_Cinder : public UValorAgentDefinition
{
	GENERATED_BODY()

public:
	UValorAgent_Cinder();
};

// Rook: C 정찰 다트(200, 1회) / Q 지속 회복(150, 1회) / E 소형 벽(매 라운드 무료) / X 넓은 정찰(6포인트).
UCLASS()
class VALOR_API UValorAgent_Rook : public UValorAgentDefinition
{
	GENERATED_BODY()

public:
	UValorAgent_Rook();
};

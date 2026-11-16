#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/ValorAgentAbility.h"
#include "ValorSampleAbilities.generated.h"

// 스킬 틀(5-1) 확인용 샘플 스킬. 장르 공통 메커닉(회복·연막·돌진·재충전)을 가장 단순한 형태로 만든 것이다.
// 5-2(스킬 부품)에서 투척물·연막 시야 차단·섬광 등으로 다듬는다.

// 회복: 체력을 즉시 HealAmount만큼 채운다(최대 체력까지).
UCLASS()
class VALOR_API UValorAbility_Mend : public UValorAgentAbility
{
	GENERATED_BODY()

protected:
	virtual void ActivateAgentAbility() override;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Ability", meta=(ClampMin="0.0"))
	float HealAmount = 40.0f;
};

// 연막 통: 조준한 곳(최대 MaxRangeCm)에 시야를 가리는 연막을 SmokeDurationSeconds 동안 만든다.
UCLASS()
class VALOR_API UValorAbility_SmokeCanister : public UValorAgentAbility
{
	GENERATED_BODY()

protected:
	virtual void ActivateAgentAbility() override;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Ability", meta=(ClampMin="100.0"))
	float MaxRangeCm = 2500.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Ability", meta=(ClampMin="1.0"))
	float SmokeDurationSeconds = 15.0f;
};

// 돌진: 보고 있는 수평 방향으로 짧게 튀어 나간다. 소유 클라가 바로 움직이도록 예측 실행한다.
UCLASS()
class VALOR_API UValorAbility_Dash : public UValorAgentAbility
{
	GENERATED_BODY()

public:
	UValorAbility_Dash();

protected:
	virtual void ActivateAgentAbility() override;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Ability", meta=(ClampMin="0.0"))
	float DashSpeed = 1600.0f;
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

// 샘플 요원 "Drift": C 회복(200, 1회) / Q 연막 통(100, 2회) / E 돌진(매 라운드 무료 1회) / X 과충전(궁극기 6포인트).
UCLASS()
class VALOR_API UValorAgent_Drift : public UValorAgentDefinition
{
	GENERATED_BODY()

public:
	UValorAgent_Drift();
};

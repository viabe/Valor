#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/ValorAgentTypes.h"
#include "ValorAgentAbility.generated.h"

class AValorCharacter;
class AValorPlayerState;

/**
 * 요원 스킬의 공통 부모. 스킬 칸(C/Q/E/X)은 부여할 때 스펙의 InputID로 정한다(같은 스킬 클래스를 다른 칸에 쓸 수 있다).
 *
 * 비용: 일반 칸은 PlayerState 충전 1개, 궁극기 칸은 궁극기 포인트 전부(필요 포인트가 모여야 쓴다).
 *   충전·포인트는 PlayerState에 있어 죽고 다시 스폰돼도 남는다. 서버만 줄이고 복제로 클라에 보인다.
 * 네트워크: 기본은 서버 실행(클라 입력 → ServerTryActivateAbility). 이동처럼 즉시 반응이 필요한 스킬은 하위 클래스가 LocalPredicted로 바꾼다.
 * 하위 클래스는 ActivateAgentAbility만 구현한다(비용 확인·차감은 여기서 끝난다).
 */
UCLASS(Abstract)
class VALOR_API UValorAgentAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UValorAgentAbility();

	// 부여할 때 스펙 InputID = 이 값 + 칸 번호. 무기 어빌리티(입력 없음)와 겹치지 않게 띄운다.
	static constexpr int32 InputIdBase = 10;
	static int32 InputIdForSlot(EValorAbilitySlot Slot) { return InputIdBase + static_cast<int32>(Slot); }

	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:
	// 비용을 낸 뒤 실제 효과. 서버 실행 스킬은 서버에서만, LocalPredicted 스킬은 소유 클라와 서버 둘 다에서 불린다.
	virtual void ActivateAgentAbility() {}

	EValorAbilitySlot GetSlotFromSpec(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const;
	AValorCharacter* GetValorCharacter() const;
	AValorPlayerState* GetValorPlayerState(const FGameplayAbilityActorInfo* ActorInfo) const;
};

#include "UGA_WeaponFire.h"

#include "AbilitySystem/ValorGameplayTags.h"
#include "Components/ValorCombatComponent.h"
#include "ValorCharacter.h"

UUGA_WeaponFire::UUGA_WeaponFire()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	// 죽은 캐릭터는 이 어빌리티를 활성화할 수 없다(서버가 사망 시 State.Dead 태그를 붙인다).
	// 입력 쪽에서도 막지만, 서버 권위 차단은 여기 한 줄이 맡는다 → 사망 직후 도착한 발사/재장전 RPC가 무시된다.
	ActivationBlockedTags.AddTag(ValorGameplayTags::State_Dead);
}

void UUGA_WeaponFire::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (AValorCharacter* Character = Cast<AValorCharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UValorCombatComponent* CombatComponent = Character->GetCombatComponent())
		{
			CombatComponent->ExecuteServerFireAbility();
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

#include "AbilitySystem/ValorGameplayTags.h"

namespace ValorGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dead, "State.Dead", "사망 상태. 사격/재장전/조준 어빌리티를 막는다.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Damage, "SetByCaller.Damage", "피해 GameplayEffect의 피해량 SetByCaller 키.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Zone_Head, "Damage.Zone.Head", "머리 명중.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Zone_Body, "Damage.Zone.Body", "몸통 명중.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Zone_Leg, "Damage.Zone.Leg", "다리 명중.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Penetrated, "Damage.Penetrated", "벽을 관통한 피해(월뱅).");
}

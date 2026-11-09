#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Valor 네이티브 게임플레이 태그.
 *
 * 왜 네이티브 태그인가: 태그를 ini/에셋이 아니라 C++에 선언하면 오타가 컴파일 에러가 되고, 에셋 없이도(C++ 전용 규칙)
 *   모듈이 로드되는 순간 태그가 등록된다. 어빌리티 CDO 생성자(ActivationBlockedTags)에서도 바로 쓸 수 있다.
 * 네트워크: 태그 자체는 FName 기반 식별자라 복제 비용이 없다. 실제로 복제되는 것은 ASC의 태그 카운트(복제 루즈 태그)뿐이다.
 */
namespace ValorGameplayTags
{
	// 사망 상태. 서버가 붙이고(로컬 + 복제 루즈 태그), 사격/재장전/조준 어빌리티의 ActivationBlockedTags가 이 태그로 막힌다.
	VALOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);

	// 피해 GameplayEffect의 SetByCaller 키(피해량).
	VALOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Damage);

	// 피해 스펙에 붙는 동적 에셋 태그: 맞은 부위(산탄은 가장 치명적인 부위)와 벽 관통 여부. 킬피드/히트마커가 읽는다.
	VALOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Zone_Head);
	VALOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Zone_Body);
	VALOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Zone_Leg);
	VALOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Penetrated);
}

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "ValorDamageGameplayEffect.generated.h"

/**
 * 총/스킬 피해를 전달하는 즉발 GameplayEffect(C++ 정의, 에셋 불필요).
 *
 * 왜 GameplayEffect인가: 예전처럼 ApplyModToAttribute로 숫자만 빼면 "누가, 무엇으로, 어느 부위를" 맞혔는지가 사라져
 *   킬 크레딧·킬피드·헤드샷 표시를 만들 수 없다. GE 스펙은 EffectContext(사수, 무기, 발사 위치)와 동적 태그(부위, 관통)를
 *   싣고 가므로 AttributeSet이 피해를 적용하는 그 자리에서 사건 전체를 알 수 있다. 스킬 피해도 같은 GE를 쓰면 된다.
 * 동작: IncomingDamage(메타 속성)에 SetByCaller.Damage만큼 더한다 → AttributeSet이 실드 → 체력 순서로 나눠 적용한다.
 * 네트워크: 서버에서만 적용된다(사격 판정이 서버 전용). 결과는 Health/Shield 속성 복제로만 클라에 전달되고 GE 자체는 복제되지 않는다(즉발).
 * 트레이드오프: 클래스 기본값으로 정의해 BP 없이 쓰지만, 피해 공식을 디자이너가 에셋에서 바꾸려면 BP 서브클래스를 만들어 교체해야 한다.
 */
UCLASS()
class VALOR_API UValorDamageGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UValorDamageGameplayEffect();
};

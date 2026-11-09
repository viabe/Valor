#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"

/**
 * 피해 계산·전달에 쓰는 순수 타입과 함수(월드/액터 없이 테스트 가능).
 *
 * 왜 분리했나: 실드 → 체력 순서 차감, 부위 우선순위, 태그 변환은 서버 판정 규칙 그 자체다.
 *   AttributeSet 안에 묻어 두면 GAS 월드 없이는 검증할 수 없으므로, 계산만 떼어 자동화 테스트로 고정한다.
 */

// 피해 한 번을 체력/실드에 적용한 결과.
struct FValorDamageResolution
{
	float NewHealth = 0.0f;
	float NewShield = 0.0f;

	// 실제로 깎인 양(남은 체력보다 큰 피해는 잘린다 — 히트 확인 UI의 "준 피해" 표시에 쓴다).
	float ShieldDamage = 0.0f;
	float HealthDamage = 0.0f;

	// 이번 피해로 체력이 0이 되었는가(이미 0이던 대상은 false — 사망 처리가 두 번 일어나지 않게).
	bool bKilled = false;
};

// 서버가 피해를 적용한 직후 AttributeSet이 알리는 사건(서버 전용, 복제하지 않음).
// 피격자 방향 표시, 사수 히트마커, 사망 처리(킬 크레딧)가 모두 이 하나에서 출발한다 → 총 외의 피해(스킬)도 같은 경로를 탄다.
struct FValorDamageEvent
{
	// 피해를 준 쪽(사수 캐릭터)과 피해 수단(무기 액터). 사수가 그새 파괴됐을 수 있어 약참조로 둔다.
	TWeakObjectPtr<AActor> Instigator;
	TWeakObjectPtr<AActor> DamageCauser;

	// 피해가 날아온 위치(사수의 눈 위치). 피격 방향 표시에 쓴다.
	FVector Origin = FVector::ZeroVector;
	bool bHasOrigin = false;

	// 요청된 피해(감쇠 전)와 실제 적용 결과.
	float RequestedDamage = 0.0f;
	FValorDamageResolution Resolution;

	EValorHitZone HitZone = EValorHitZone::None;
	bool bPenetrated = false;
};

namespace ValorDamage
{
	// 발로란트 규칙: 실드가 피해를 먼저 100% 흡수하고, 남은 피해가 체력을 깎는다(풀실드 150 = 밴달 몸샷 4발).
	VALOR_API FValorDamageResolution Resolve(float Health, float Shield, float Damage);

	// 한 발(산탄은 여러 펠릿)이 한 대상의 여러 부위를 맞혔을 때 대표 부위: 머리 > 몸 > 다리.
	VALOR_API EValorHitZone MoreSevereZone(EValorHitZone A, EValorHitZone B);

	// 부위 ↔ 피해 스펙 동적 태그.
	VALOR_API FGameplayTag HitZoneToTag(EValorHitZone HitZone);
	VALOR_API EValorHitZone HitZoneFromTags(const FGameplayTagContainer& Tags);
}

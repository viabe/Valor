#pragma once

#include "CoreMinimal.h"
#include "ValorAnimTypes.generated.h"

// 무기별 상체 포즈와 레이어 분기를 위한 공통 분류다.
UENUM(BlueprintType)
enum class EValorWeaponAnimationType : uint8
{
	None,
	Rifle,
	Sidearm,
	Knife,
	AbilityDevice
};

// 능력 사용 애니메이션은 캐스팅, 루프, 종료를 별도로 나눌 수 있도록 준비한다.
UENUM(BlueprintType)
enum class EValorAbilityAnimationState : uint8
{
	None,
	Casting,
	Looping,
	Recovery
};

// 피격 방향 보정이나 히트 리액션 몽타주 분기를 위한 공통 타입이다.
UENUM(BlueprintType)
enum class EValorHitReactType : uint8
{
	None,
	Front,
	Back,
	Left,
	Right
};

// 사망 애니메이션은 기초 틀만 두고, 실제 세부 상태는 이후 확장한다.
UENUM(BlueprintType)
enum class EValorDeathAnimationState : uint8
{
	Alive,
	Dying,
	Dead
};

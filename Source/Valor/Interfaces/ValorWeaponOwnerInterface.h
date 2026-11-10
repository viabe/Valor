#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ValorWeaponOwnerInterface.generated.h"

class USceneComponent;

UINTERFACE(BlueprintType)
class VALOR_API UValorWeaponOwnerInterface : public UInterface
{
	GENERATED_BODY()
};

class VALOR_API IValorWeaponOwnerInterface
{
	GENERATED_BODY()

public:
	// 무기 시스템이 부착 기준 컴포넌트를 직접 조회할 수 있도록 노출한다.
	virtual USceneComponent* GetWeaponAttachComponent() const = 0;

	// 무기 시스템이 사용할 기본 소켓 이름을 제공한다.
	virtual FName GetWeaponAttachSocketName() const = 0;

	// 1인칭(내 화면) 총을 붙일 컴포넌트. 이 머신의 로컬 플레이어가 조종하는 살아 있는 캐릭터일 때만 값을 준다.
	// nullptr이면 3인칭 손 소켓에 붙인다(다른 플레이어·AI·서버가 보는 원격 캐릭터).
	// 부착은 복제되지 않으므로 머신마다 각자 결정한다. 같은 총이 사수 화면에서는 카메라에, 남의 화면에서는 손에 붙는다.
	virtual USceneComponent* GetFirstPersonWeaponAttachComponent() const { return nullptr; }

	// 3인칭 손 소켓 공간에서 총 리그(Z = 총구, Y = 아래, X = 왼쪽)의 회전. 손 뼈 축이 총 리그 축과 다르므로 이 회전으로 맞춘다.
	virtual FQuat GetThirdPersonWeaponRotation() const { return FQuat::Identity; }

	// 총기 발사나 조준 계산에서 사용할 현재 시점 정보를 제공한다.
	virtual void GetWeaponViewPoint(FVector& OutLocation, FRotator& OutRotation) const = 0;
};

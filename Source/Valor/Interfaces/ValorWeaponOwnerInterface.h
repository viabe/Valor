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

	// 총기 발사나 조준 계산에서 사용할 현재 시점 정보를 제공한다.
	virtual void GetWeaponViewPoint(FVector& OutLocation, FRotator& OutRotation) const = 0;
};

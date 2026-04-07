#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ValorPlayerController.generated.h"

UCLASS()
class VALOR_API AValorPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
};

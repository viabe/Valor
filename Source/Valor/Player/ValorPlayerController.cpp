#include "ValorPlayerController.h"

void AValorPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	// FPS 기본 입력 모드는 게임 전용으로 유지한다.
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;
}

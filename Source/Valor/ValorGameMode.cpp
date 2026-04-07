#include "ValorGameMode.h"

#include "Player/ValorPlayerController.h"
#include "UObject/ConstructorHelpers.h"

AValorGameMode::AValorGameMode()
{
	// 블루프린트 기본 Pawn은 유지해 기존 메시/입력 자산 연결을 최대한 보존한다.
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != nullptr)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}

	PlayerControllerClass = AValorPlayerController::StaticClass();
}

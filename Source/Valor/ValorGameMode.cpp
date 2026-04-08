#include "ValorGameMode.h"

#include "ValorCharacter.h"
#include "Player/ValorPlayerController.h"

AValorGameMode::AValorGameMode()
{
	// 총기/전투 시스템은 AValorCharacter에 직접 연결되어 있으므로 기본 Pawn도 C++ 캐릭터를 사용한다.
	DefaultPawnClass = AValorCharacter::StaticClass();
	PlayerControllerClass = AValorPlayerController::StaticClass();
}

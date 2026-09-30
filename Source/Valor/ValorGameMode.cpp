#include "ValorGameMode.h"

#include "ValorCharacter.h"
#include "Player/ValorPlayerController.h"
#include "UI/ValorHUD.h"

AValorGameMode::AValorGameMode()
{
	// 총기/전투 시스템은 AValorCharacter에 직접 연결되어 있으므로 기본 Pawn도 C++ 캐릭터를 사용한다.
	DefaultPawnClass = AValorCharacter::StaticClass();
	PlayerControllerClass = AValorPlayerController::StaticClass();

	// 크로스헤어 HUD. HUD는 각 클라이언트의 PlayerController가 로컬에서만 생성하므로 서버/복제 비용이 없다.
	// BP_ValorGameMode는 HUD Class를 덮어쓰지 않으므로 이 기본값을 그대로 상속한다.
	HUDClass = AValorHUD::StaticClass();
}

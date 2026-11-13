#include "Player/ValorBotController.h"

void AValorBotController::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// PlayerState가 있어야 팀·크레딧·전적을 갖고 경기 규칙에 사람처럼 들어간다(AIController의 bWantsPlayerState와 같은 일).
	if (IsValid(this) && GetNetMode() != NM_Client)
	{
		InitPlayerState();
	}
}

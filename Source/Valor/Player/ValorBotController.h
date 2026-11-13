#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Controller.h"
#include "ValorBotController.generated.h"

/**
 * 개발용 봇 컨트롤러(콘솔 ValorAddBot). 아무 행동도 하지 않는 허수아비지만 PlayerState(팀·크레딧·전적)를 가져서
 * 경기 규칙(팀 인원, 전멸 판정, 라운드 스폰, 관전 대상)에 사람과 똑같이 들어간다. 혼자 PIE에서 아군·적이 필요할 때 쓴다.
 * 서버에만 있다(컨트롤러는 복제되지 않는다. 봇의 PlayerState와 캐릭터는 복제된다).
 */
UCLASS()
class VALOR_API AValorBotController : public AController
{
	GENERATED_BODY()

public:
	virtual void PostInitializeComponents() override;
};

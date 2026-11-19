#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ValorMenuGameMode.generated.h"

/**
 * 메인 메뉴 게임 모드(혼자, 캐릭터 없음). 기본 맵(GameDefaultMap = 경기 맵)을 ?game=Menu로 열면 이 모드가 된다.
 *
 * 왜 맵이 아니라 모드인가: 메뉴 전용 맵(에셋)을 새로 만들지 않고, 경기 맵을 배경으로 메뉴를 그린다.
 *   예전 기본 맵 ValorEntry는 레벨 블루프린트가 시작하자마자 127.0.0.1로 접속(OpenLevel)해서 메뉴로 쓸 수 없다.
 *   DefaultEngine.ini의 LocalMapOptions=?game=Menu 덕분에 게임을 켰을 때와 접속이 끊겼을 때(엔진이 기본 맵으로 보냄) 모두 메뉴가 된다.
 * 화면: 캐릭터를 만들지 않고, 컨트롤러 시점을 첫 PlayerStart 위에 둔다(배경). 메뉴는 HUD(ValorHUDMainMenu.cpp)가 캔버스로 그린다.
 */
UCLASS()
class VALOR_API AValorMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AValorMenuGameMode();

	virtual void RestartPlayer(AController* NewPlayer) override;

	// 데디 서버에는 메뉴가 없다. 서버가 메뉴 옵션으로 열렸으면(설정 실수) 맵 기본 모드로 다시 연다.
	virtual void BeginPlay() override;
};

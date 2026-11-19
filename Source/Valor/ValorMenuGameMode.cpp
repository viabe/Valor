#include "ValorMenuGameMode.h"

#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Player/ValorPlayerController.h"
#include "UI/ValorHUD.h"
#include "Engine/World.h"
#include "GameMapsSettings.h"
#include "TimerManager.h"

AValorMenuGameMode::AValorMenuGameMode()
{
	// 캐릭터·관전 폰 없이 컨트롤러만 둔다(메뉴에서 WASD로 움직일 것이 없다).
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AValorPlayerController::StaticClass();
	HUDClass = AValorHUD::StaticClass();
	GameStateClass = AGameStateBase::StaticClass();
	PlayerStateClass = APlayerState::StaticClass();
	bStartPlayersAsSpectators = false;
}

void AValorMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Config/DedicatedServerEngine.ini가 서버의 LocalMapOptions를 비우므로 보통은 오지 않는다. 그래도 오면 경기 맵 기본 모드로.
	if (GetNetMode() == NM_DedicatedServer)
	{
		UE_LOG(LogGameMode, Warning, TEXT("데디 서버가 메뉴 모드로 열렸습니다. %s(맵 기본 모드)로 다시 엽니다."), *UGameMapsSettings::GetGameDefaultMap());
		GetWorldTimerManager().SetTimerForNextTick([this]()
		{
			GetWorld()->ServerTravel(UGameMapsSettings::GetGameDefaultMap(), true);
		});
	}
}

void AValorMenuGameMode::RestartPlayer(AController* NewPlayer)
{
	// 폰을 만들지 않고, 시점만 첫 시작 지점 위(사람 눈높이보다 조금 위)에 둔다.
	if (!NewPlayer)
	{
		return;
	}
	if (const AActor* Start = FindPlayerStart(NewPlayer))
	{
		NewPlayer->SetInitialLocationAndRotation(Start->GetActorLocation() + FVector(0.0f, 0.0f, 120.0f), Start->GetActorRotation());
	}
}

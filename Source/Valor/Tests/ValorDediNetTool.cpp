#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Player/ValorPlayerController.h"
#include "Session/ValorSessionSubsystem.h"
#include "Settings/ValorUserSettings.h"
#include "UnrealClient.h"
#include "ValorGameState.h"
#include "ValorMatchGameMode.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 데디 서버 + 클라 둘(둘 다 맵 없이 켜서 메인 메뉴에서 127.0.0.1로 참가).
//  서버(Server, -server): 대기실 ?Lobby=1?MinPlayers=2. 1명이면 시간 없이 기다림 → 2명이면 카운트다운 → 시작.
//    B가 나갔다 다시 오면 A에게 보내는 패킷을 모두 버린다(A 입장에서는 서버가 갑자기 죽은 것과 같다).
//    A가 시간 초과로 나가면 접속을 닫고(정상 종료) 서버를 끈다.
//  클라 A(이름 DediA): 먼저 참가 → 대기실 1/2(시간 없음) → B가 오면 시작 → 서버 응답이 끊기면 2초부터 연결 경고
//    → 시간 초과(20초) 뒤 "TIMED OUT"과 함께 메뉴.
//  클라 B(이름 DediB): 나중에 참가 → 카운트다운 중 → 시작되면 나가기(오류 없음) → 다시 참가 → 서버가 정상 종료하면 바로 "연결 끊김"과 함께 메뉴.
//  끝나면 이름·주소를 처음대로 되돌린다. 로그는 DEDINET으로 시작한다. 캡처: Saved/DediNet.
// 실행(scratchpad run_dedinet.sh): 서버 → 20초 뒤 A → 15초 뒤 B.
//   UnrealEditor.exe Valor.uproject "/Game/Maps/ThridPersonExampleMap?Lobby=1?MinPlayers=2?LobbyCountdown=5" -server
//     -ExecCmds="Automation RunTests Valor.Tools.DediNet.Server; Quit"
//   UnrealEditor.exe Valor.uproject -game -ExecCmds="Automation RunTests Valor.Tools.DediNet.ClientA; Quit"
//   UnrealEditor.exe Valor.uproject -game -ExecCmds="Automation RunTests Valor.Tools.DediNet.ClientB; Quit"
// =====================================================================================================

namespace ValorDediNetTool
{
	enum class ERole : uint8
	{
		Server,
		ClientA,
		ClientB
	};

	UWorld* FindGameWorld()
	{
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if (WorldContext.WorldType == EWorldType::Game && WorldContext.World())
			{
				return WorldContext.World();
			}
		}
		return nullptr;
	}

	FString PlayerNames(const UWorld& World)
	{
		TArray<FString> Names;
		if (const AGameStateBase* GameState = World.GetGameState())
		{
			for (const APlayerState* Player : GameState->PlayerArray)
			{
				if (Player)
				{
					Names.Add(Player->GetPlayerName());
				}
			}
		}
		Names.Sort();
		return FString::Join(Names, TEXT(","));
	}

	class FValorDediNetCommand : public IAutomationLatentCommand
	{
	public:
		FValorDediNetCommand(FAutomationTestBase* InTest, ERole InRole)
			: Test(InTest)
			, Role(InRole)
		{
			OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DediNet"));
			IFileManager::Get().MakeDirectory(*OutputDirectory, true);
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (StartTime < 0.0)
			{
				StartTime = Now;
			}
			if (Now - StartTime > 240.0)
			{
				Test->AddError(FString::Printf(TEXT("DEDINET %s 240초 안에 끝나지 않았습니다(단계 %d)."), RoleName(), Step));
				Restore();
				return true;
			}
			if (Now < WaitUntil)
			{
				return false;
			}

			UWorld* World = FindGameWorld();
			if (!World)
			{
				return false;
			}
			bool bDone = false;
			if (Role == ERole::Server)
			{
				bDone = UpdateServer(Now, *World);
			}
			else
			{
				AValorPlayerController* Controller = Cast<AValorPlayerController>(GEngine->GetFirstLocalPlayerController(World));
				UValorSessionSubsystem* Session = UValorSessionSubsystem::Get(World);
				if (!Controller || !Session)
				{
					return false;
				}
				bDone = UpdateClient(Now, *World, *Controller, *Session);
			}
			if (bDone)
			{
				Restore();
			}
			return bDone;
		}

	private:
		const TCHAR* RoleName() const
		{
			return Role == ERole::Server ? TEXT("Server") : (Role == ERole::ClientA ? TEXT("ClientA") : TEXT("ClientB"));
		}

		bool UpdateServer(double Now, UWorld& World)
		{
			AValorMatchGameMode* GameMode = World.GetAuthGameMode<AValorMatchGameMode>();
			const AValorGameState* GameState = World.GetGameState<AValorGameState>();
			if (!GameMode || !GameState)
			{
				return false;
			}
			const int32 Humans = GameMode->GetNumPlayers();
			switch (Step)
			{
			case 0:
				Test->AddInfo(FString::Printf(TEXT("DEDINET Server up: netMode=%d map=%s lobbyMin=%d phase=%d"), static_cast<int32>(World.GetNetMode()), *World.GetMapName(),
					GameState->GetLobbyMinPlayers(), static_cast<int32>(GameState->GetRoundPhase())));
				Expect(World.GetNetMode() == NM_DedicatedServer && GameState->GetLobbyMinPlayers() == 2, TEXT("데디 서버, 대기실 최소 2명"));
				Step = 1;
				return false;

			case 1:
				// A 혼자: 3초 동안 카운트다운이 걸리지 않아야 한다.
				if (Humans < 1)
				{
					return false;
				}
				if (OnePlayerSince < 0.0)
				{
					OnePlayerSince = Now;
				}
				if (Humans == 1 && Now - OnePlayerSince < 3.0)
				{
					Expect(GameMode->IsWaitingInLobby() && GameState->GetPhaseTimeRemaining() < 0.0f, TEXT("1/2명이면 시간 없이 대기"));
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("DEDINET Server one player waited %.1fs: players=%s"), Now - OnePlayerSince, *PlayerNames(World)));
				Step = 2;
				return false;

			case 2:
				if (Humans < 2)
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("DEDINET Server two players: countdown=%.1f players=%s"), GameState->GetPhaseTimeRemaining(), *PlayerNames(World)));
				Expect(GameState->GetRoundPhase() == EValorRoundPhase::WaitingToStart && GameState->GetPhaseTimeRemaining() >= 0.0f, TEXT("2/2명이면 카운트다운"));
				Step = 3;
				return false;

			case 3:
				if (GameState->GetRoundPhase() == EValorRoundPhase::WaitingToStart)
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("DEDINET Server started: phase=%d players=%s"), static_cast<int32>(GameState->GetRoundPhase()), *PlayerNames(World)));
				Expect(PlayerNames(World) == TEXT("DediA,DediB"), TEXT("서버가 받은 이름 = 각 클라 메뉴 이름"));
				Step = 4;
				return false;

			case 4:
				if (Humans != 1)
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("DEDINET Server: B left, players=%s"), *PlayerNames(World)));
				Step = 5;
				return false;

			case 5:
				if (Humans < 2)
				{
					return false;
				}
				Test->AddInfo(TEXT("DEDINET Server: B rejoined, cutting A in 2s"));
				Step = 6;
				WaitUntil = Now + 2.0;
				return false;

			case 6:
			{
				// 서버가 갑자기 죽은 상황 흉내: A에게 가는 패킷만 모두 버린다(A가 보내는 것은 받는다).
				// 접속을 닫는 신호가 가지 않으므로 A는 시간 초과로만 알 수 있다.
				bool bCut = false;
#if DO_ENABLE_NET_TEST
				for (FConstPlayerControllerIterator It = World.GetPlayerControllerIterator(); It; ++It)
				{
					APlayerController* PlayerController = It->Get();
					if (PlayerController && PlayerController->PlayerState && PlayerController->PlayerState->GetPlayerName() == TEXT("DediA") && PlayerController->NetConnection)
					{
						PlayerController->NetConnection->PacketSimulationSettings.PktLoss = 100;
						bCut = true;
					}
				}
#endif
				Expect(bCut, TEXT("A에게 가는 패킷 끊기"));
				Test->AddInfo(TEXT("DEDINET Server: stopped sending to A"));
				CutTime = Now;
				Step = 7;
				return false;
			}

			case 7:
				// A는 20초 뒤 시간 초과로 끊고 더 보내지 않는다(닫기 신호도 보내지 않음). 서버도 그로부터 20초 뒤 A를 시간 초과로 내보낸다(합 약 40초).
				if (Humans > 1)
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("DEDINET Server: A gone %.1fs after cut, players=%s, shutting down in 2s"), Now - CutTime, *PlayerNames(World)));
				Expect(PlayerNames(World) == TEXT("DediB"), TEXT("A만 나감"));
				Step = 8;
				WaitUntil = Now + 2.0;
				return false;

			case 8:
				// 정상 종료: 접속을 닫아 클라에 알린다(프로세스만 끝나면 클라는 시간 초과(60초)로만 알게 된다).
				Test->AddInfo(TEXT("DEDINET Server: closing connections"));
				GEngine->ShutdownWorldNetDriver(&World);
				Step = 9;
				WaitUntil = Now + 2.0;
				return false;

			default:
				return true;
			}
		}

		bool UpdateClient(double Now, UWorld& World, AValorPlayerController& Controller, UValorSessionSubsystem& Session)
		{
			const bool bA = Role == ERole::ClientA;
			const FString MyName = bA ? TEXT("DediA") : TEXT("DediB");
			const AValorGameState* GameState = World.GetGameState<AValorGameState>();
			const bool bJoined = World.GetNetMode() == NM_Client && GameState && Controller.PlayerState && Controller.PlayerState->GetPlayerName() == MyName;
			switch (Step)
			{
			case 0:
			{
				if (!Controller.IsInMainMenu())
				{
					return false;
				}
				TakeSnapshot(*MyName);
				FString Error;
				Expect(Session.JoinGame(TEXT("127.0.0.1"), Error), TEXT("참가 시작"));
				JoinStartTime = Now;
				Step = 1;
				return false;
			}

			case 1:
				if (!bJoined)
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("DEDINET %s joined in %.1fs: lobbyMin=%d phase=%d timeLeft=%.1f players=%s"), RoleName(), Now - JoinStartTime,
					GameState->GetLobbyMinPlayers(), static_cast<int32>(GameState->GetRoundPhase()), GameState->GetPhaseTimeRemaining(), *PlayerNames(World)));
				Expect(GameState->GetLobbyMinPlayers() == 2, TEXT("클라도 대기실 최소 인원을 받음"));
				if (bA)
				{
					Expect(GameState->GetRoundPhase() == EValorRoundPhase::WaitingToStart && GameState->GetPhaseTimeRemaining() < 0.0f, TEXT("A: 혼자면 시간 없이 대기"));
				}
				else
				{
					Expect(GameState->GetRoundPhase() == EValorRoundPhase::WaitingToStart && GameState->GetPhaseTimeRemaining() >= 0.0f, TEXT("B: 둘이 되면 카운트다운"));
				}
				Step = 2;
				WaitUntil = Now + 0.5;
				return false;

			case 2:
				FScreenshotRequest::RequestScreenshot(OutputDirectory / FString::Printf(TEXT("%s_Lobby.png"), RoleName()), true, false);
				Step = 3;
				return false;

			case 3:
				// 카운트다운 뒤 시작.
				if (!GameState || GameState->GetRoundPhase() == EValorRoundPhase::WaitingToStart)
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("DEDINET %s saw start: phase=%d players=%s"), RoleName(), static_cast<int32>(GameState->GetRoundPhase()), *PlayerNames(World)));
				Expect(PlayerNames(World) == TEXT("DediA,DediB"), TEXT("두 이름이 보임"));
				if (bA)
				{
					Step = 20;
				}
				else
				{
					Session.ReturnToMainMenu();
					Step = 4;
				}
				return false;

			case 4:
			{
				// B: 나간 뒤 메뉴(오류 없음) → 다시 참가.
				if (!Controller.IsInMainMenu())
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("DEDINET ClientB left: netMode=%d error=%s"), static_cast<int32>(World.GetNetMode()), *Session.GetLastError()));
				Expect(World.GetNetMode() == NM_Standalone && Session.GetLastError().IsEmpty(), TEXT("스스로 나가면 메뉴, 오류 없음"));
				FString Error;
				Expect(Session.JoinGame(TEXT("127.0.0.1"), Error), TEXT("다시 참가 시작"));
				JoinStartTime = Now;
				Step = 5;
				return false;
			}

			case 5:
				if (!bJoined)
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("DEDINET ClientB rejoined in %.1fs mid-match: phase=%d"), Now - JoinStartTime, static_cast<int32>(GameState->GetRoundPhase())));
				Step = 10;
				return false;

			case 20:
				// A: 서버 응답이 끊기면 2초부터 경고(끊김까지 남은 시간).
				if (Controller.GetServerSilenceSeconds() < 2.5f)
				{
					return false;
				}
				SilenceSeenTime = Now - Controller.GetServerSilenceSeconds();
				Test->AddInfo(FString::Printf(TEXT("DEDINET ClientA: server silent %.1fs, timeout=%.0f"), Controller.GetServerSilenceSeconds(), Controller.GetServerTimeoutSeconds()));
				Expect(FMath::IsNearlyEqual(Controller.GetServerTimeoutSeconds(), 20.0f), TEXT("끊김 판정 20초(DefaultEngine.ini)"));
				FScreenshotRequest::RequestScreenshot(OutputDirectory / TEXT("ClientA_ConnectionProblem.png"), true, false);
				Step = 21;
				return false;

			case 21:
				// 시간 초과 뒤 엔진이 기본 맵(메뉴)으로 보낸다.
				if (!Controller.IsInMainMenu())
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("DEDINET ClientA back in menu %.1fs after server went silent: error=%s"), Now - SilenceSeenTime, *Session.GetLastError()));
				Expect(Session.GetLastError() == TEXT("CONNECTION TIMED OUT"), TEXT("서버가 응답 없으면 메뉴에 시간 초과 표시"));
				Expect(Now - SilenceSeenTime > 15.0 && Now - SilenceSeenTime < 30.0, TEXT("약 20초 뒤 메뉴로"));
				Step = 11;
				WaitUntil = Now + 1.0;
				return false;

			case 10:
				// 서버가 꺼지면 엔진이 기본 맵(메뉴)으로 보낸다.
				if (!Controller.IsInMainMenu())
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("DEDINET %s back in menu after server shutdown: error=%s"), RoleName(), *Session.GetLastError()));
				Expect(Session.GetLastError() == TEXT("CONNECTION TO THE SERVER WAS LOST"), TEXT("서버가 꺼지면 메뉴에 연결 끊김 표시"));
				Step = 11;
				WaitUntil = Now + 1.0;
				return false;

			case 11:
				FScreenshotRequest::RequestScreenshot(OutputDirectory / FString::Printf(TEXT("%s_ServerGone.png"), RoleName()), true, false);
				Step = 12;
				WaitUntil = Now + 0.5;
				return false;

			default:
				return true;
			}
		}

		void TakeSnapshot(const TCHAR* TestName)
		{
			UValorUserSettings& Settings = UValorUserSettings::Get();
			SavedName = Settings.GetPlayerName();
			SavedAddress = Settings.GetLastJoinAddress();
			bSnapshotTaken = true;
			Settings.SetPlayerName(TestName);
		}

		void Restore()
		{
			if (!bSnapshotTaken)
			{
				return;
			}
			UValorUserSettings& Settings = UValorUserSettings::Get();
			Settings.SetPlayerName(SavedName);
			Settings.SetLastJoinAddress(SavedAddress);
			Settings.Save();
			bSnapshotTaken = false;
		}

		void Expect(bool bCondition, const TCHAR* What)
		{
			if (!bCondition)
			{
				Test->AddError(FString::Printf(TEXT("DEDINET %s 실패: %s"), RoleName(), What));
			}
		}

		FAutomationTestBase* Test = nullptr;
		ERole Role = ERole::Server;
		FString OutputDirectory;
		int32 Step = 0;
		double StartTime = -1.0;
		double WaitUntil = 0.0;
		double JoinStartTime = 0.0;
		double OnePlayerSince = -1.0;
		double CutTime = 0.0;
		double SilenceSeenTime = 0.0;
		bool bSnapshotTaken = false;
		FString SavedName;
		FString SavedAddress;
	};
}

// 서버는 데디 서버 프로세스에서 돈다(ServerContext).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorDediNetServerTool, "Valor.Tools.DediNet.Server", EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter)
bool FValorDediNetServerTool::RunTest(const FString& Parameters)
{
	// A가 응답을 멈추면 서버도 A를 시간 초과로 내보내며 엔진이 오류 로그를 남긴다(이 도구에서는 기다리는 결과다).
	AddExpectedError(TEXT("BroadcastNetworkFailure"), EAutomationExpectedErrorFlags::Contains, 0);
	ADD_LATENT_AUTOMATION_COMMAND(ValorDediNetTool::FValorDediNetCommand(this, ValorDediNetTool::ERole::Server));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorDediNetClientATool, "Valor.Tools.DediNet.ClientA", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorDediNetClientATool::RunTest(const FString& Parameters)
{
	// 서버가 꺼지면 엔진이 연결 끊김을 오류 로그로 남긴다(이 도구에서는 기다리는 결과다).
	AddExpectedError(TEXT("BroadcastNetworkFailure"), EAutomationExpectedErrorFlags::Contains, 0);
	ADD_LATENT_AUTOMATION_COMMAND(ValorDediNetTool::FValorDediNetCommand(this, ValorDediNetTool::ERole::ClientA));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorDediNetClientBTool, "Valor.Tools.DediNet.ClientB", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorDediNetClientBTool::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("BroadcastNetworkFailure"), EAutomationExpectedErrorFlags::Contains, 0);
	ADD_LATENT_AUTOMATION_COMMAND(ValorDediNetTool::FValorDediNetCommand(this, ValorDediNetTool::ERole::ClientB));
	return true;
}

#endif

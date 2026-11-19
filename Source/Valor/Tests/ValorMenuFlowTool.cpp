#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Player/ValorPlayerController.h"
#include "Session/ValorSessionSubsystem.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "IPAddress.h"
#include "Settings/ValorUserSettings.h"
#include "UI/ValorHUD.h"
#include "UnrealClient.h"
#include "ValorGameState.h"
#include "ValorMenuGameMode.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 메인 메뉴 혼자 확인(맵 없이 켜면 기본 맵 + ?game=Menu = 메뉴).
//  1) 메뉴: 캐릭터 없음, 커서 보임. 실제 키로 이름 입력(Shift, Backspace, Enter) → 설정에 저장.
//  2) 주소: 잘못된 포트 → 오류 문구, 확인으로 지움. 아무도 없는 포트 → 접속 중 표시 → 취소.
//  3) START LOCAL SERVER(경기) → 이 PC에 데디 서버 프로세스를 띄우고 붙음, 대기실(최소 1명) → 카운트다운 뒤 저절로 시작, 내 이름.
//  4) F10 → LEAVE MATCH 두 번 → 메뉴로(오류 없음, 로컬 서버는 계속) → STOP LOCAL SERVER로 프로세스 끄기.
//  캡처: Saved/MenuFlow. 끝나면 이름·주소를 처음대로 되돌린다. 로그는 MENU로 시작한다.
// 실행:
//   UnrealEditor.exe Valor.uproject -game -windowed -ResX=1280 -ResY=720 -ExecCmds="Automation RunTests Valor.Tools.MenuFlow; Quit"
// =====================================================================================================

namespace ValorMenuFlowTool
{
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

	class FValorMenuFlowCommand : public IAutomationLatentCommand
	{
	public:
		explicit FValorMenuFlowCommand(FAutomationTestBase* InTest)
			: Test(InTest)
		{
			OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MenuFlow"));
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
				Test->AddError(FString::Printf(TEXT("MENU 240초 안에 끝나지 않았습니다(단계 %d)."), Step));
				Restore();
				return true;
			}
			if (Now < WaitUntil)
			{
				return false;
			}

			UWorld* World = FindGameWorld();
			AValorPlayerController* Controller = World ? Cast<AValorPlayerController>(GEngine->GetFirstLocalPlayerController(World)) : nullptr;
			AValorHUD* HUD = Controller ? Cast<AValorHUD>(Controller->GetHUD()) : nullptr;
			UValorSessionSubsystem* Session = UValorSessionSubsystem::Get(World);
			if (!Controller || !HUD || !Session)
			{
				return false;
			}
			UValorUserSettings& Settings = UValorUserSettings::Get();

			// 키는 한 프레임에 하나씩 보낸다(Shift 같은 누름 상태는 다음 프레임 입력 처리에서 반영된다).
			if (KeyQueue.Num() > 0)
			{
				const FQueuedKey Next = KeyQueue[0];
				KeyQueue.RemoveAt(0);
				Controller->InputKey(FInputKeyParams(Next.Key, Next.bDown ? IE_Pressed : IE_Released, Next.bDown ? 1.0 : 0.0, false));
				return false;
			}

			switch (Step)
			{
			case 0:
				if (!Controller->IsInMainMenu())
				{
					return false;
				}
				SavedName = Settings.GetPlayerName();
				SavedAddress = Settings.GetLastJoinAddress();
				bSnapshotTaken = true;
				Test->AddInfo(FString::Printf(TEXT("MENU start: map=%s pawn=%s cursor=%d name=%s localIp=%s"), *World->GetMapName(), *GetNameSafe(Controller->GetPawn()),
					Controller->bShowMouseCursor ? 1 : 0, *SavedName, *Session->GetLocalAddress()));
				Expect(Controller->GetPawn() == nullptr && Controller->bShowMouseCursor, TEXT("메뉴: 캐릭터 없음, 커서 보임"));

				// 1) 이름 칸에 실제 키로 입력: 지우고 "Tester_1"(공백은 거른다).
				HUD->NotifyHitBoxClick(TEXT("Menu_Field_Name"));
				for (int32 Index = 0; Index < 20; ++Index)
				{
					QueueTap(EKeys::BackSpace);
				}
				QueueShiftTap(EKeys::T);
				for (const FKey& Key : {EKeys::E, EKeys::S, EKeys::T, EKeys::E, EKeys::R})
				{
					QueueTap(Key);
				}
				QueueShiftTap(EKeys::Hyphen);
				QueueTap(EKeys::One);
				QueueTap(EKeys::SpaceBar);
				QueueTap(EKeys::Enter);
				Step = 1;
				return false;

			case 1:
				Test->AddInfo(FString::Printf(TEXT("MENU typed name: field=%s saved=%s focus=%d"), *Controller->GetMenuNameText(), *Settings.GetPlayerName(), static_cast<int32>(Controller->GetMenuFocus())));
				Expect(Controller->GetMenuNameText() == TEXT("Tester_1") && Settings.GetPlayerName() == TEXT("Tester_1"), TEXT("이름 입력·저장(Shift·_·공백 무시)"));
				Expect(Controller->GetMenuFocus() == EValorMenuField::None, TEXT("Enter로 입력 칸에서 나감"));

				// 2) 잘못된 포트.
				HUD->NotifyHitBoxClick(TEXT("Menu_Field_Address"));
				for (int32 Index = 0; Index < 40; ++Index)
				{
					QueueTap(EKeys::BackSpace);
				}
				QueueAddress(TEXT("1.2.3.4:99999"));
				QueueTap(EKeys::Enter);
				Step = 2;
				return false;

			case 2:
				Test->AddInfo(FString::Printf(TEXT("MENU bad address: field=%s message=%s connecting=%d"), *Controller->GetMenuAddressText(), *Controller->GetMenuMessage(), Session->IsConnecting() ? 1 : 0));
				Expect(Controller->GetMenuAddressText() == TEXT("1.2.3.4:99999"), TEXT("주소를 키로 입력(Shift+; = :)"));
				Expect(Controller->GetMenuMessage() == TEXT("PORT MUST BE 1-65535") && !Session->IsConnecting(), TEXT("잘못된 포트는 접속하지 않고 오류 문구"));
				Step = 3;
				Wait(0.3);
				return false;

			case 3:
				FScreenshotRequest::RequestScreenshot(OutputDirectory / TEXT("Menu_Error.png"), true, false);
				Step = 4;
				Wait(0.5);
				return false;

			case 4:
				HUD->NotifyHitBoxClick(TEXT("Menu_Ok"));
				Expect(Controller->GetMenuMessage().IsEmpty(), TEXT("OK로 오류 문구 지움"));

				// 아무도 없는 포트: 접속 중 → 취소.
				HUD->NotifyHitBoxClick(TEXT("Menu_Field_Address"));
				for (int32 Index = 0; Index < 40; ++Index)
				{
					QueueTap(EKeys::BackSpace);
				}
				QueueAddress(TEXT("127.0.0.1:7790"));
				Step = 5;
				return false;

			case 5:
				HUD->NotifyHitBoxClick(TEXT("Menu_Join"));
				Step = 50;
				Wait(1.5);
				return false;

			case 50:
				Test->AddInfo(FString::Printf(TEXT("MENU connecting: %d to=%s savedAddress=%s inMenu=%d"), Session->IsConnecting() ? 1 : 0, *Session->GetConnectingAddress(), *Settings.GetLastJoinAddress(), Controller->IsInMainMenu() ? 1 : 0));
				Expect(Session->IsConnecting() && Controller->IsInMainMenu(), TEXT("접속하는 동안 메뉴가 그대로 보임"));
				Expect(Settings.GetLastJoinAddress() == TEXT("127.0.0.1:7790"), TEXT("마지막 주소 저장"));
				FScreenshotRequest::RequestScreenshot(OutputDirectory / TEXT("Menu_Connecting.png"), true, false);
				Step = 51;
				Wait(0.5);
				return false;

			case 51:
				HUD->NotifyHitBoxClick(TEXT("Menu_Cancel"));
				Step = 52;
				Wait(1.0);
				return false;

			case 52:
				Test->AddInfo(FString::Printf(TEXT("MENU after cancel: connecting=%d inMenu=%d error=%s"), Session->IsConnecting() ? 1 : 0, Controller->IsInMainMenu() ? 1 : 0, *Session->GetLastError()));
				Expect(!Session->IsConnecting() && Controller->IsInMainMenu(), TEXT("취소하면 접속을 멈추고 메뉴에 남음"));

				// 설정 창: 메뉴 버튼으로 열고 실제 Esc로 닫는다.
				HUD->NotifyHitBoxClick(TEXT("Menu_Settings"));
				Expect(Controller->IsSettingsOpen(), TEXT("메뉴에서 설정 창 열림"));
				QueueTap(EKeys::Escape);
				Step = 53;
				return false;

			case 53:
				Expect(!Controller->IsSettingsOpen() && Controller->bShowMouseCursor, TEXT("Esc로 설정 창 닫힘(메뉴 커서 유지)"));

				// 3-0) 이 PC에 이미 서버가 7777을 쓰고 있으면(테스트가 포트를 잡아 흉내) 띄우지 않고 알린다.
				//      예전에는 새 서버가 7778로 옮겨 가고 클라는 먼저 떠 있던 서버에 붙었다.
				Expect(Session->CanStartLocalServer(), TEXT("에디터 실행 파일이라 로컬 서버를 띄울 수 있음"));
				{
					ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
					PortBlocker = SocketSubsystem->CreateUniqueSocket(NAME_DGram, TEXT("MenuFlowPortBlocker"), FNetworkProtocolTypes::IPv4);
					TSharedRef<FInternetAddr> Address = SocketSubsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
					Address->SetAnyAddress();
					Address->SetPort(7777);
					Expect(PortBlocker && PortBlocker->Bind(*Address), TEXT("테스트가 7777을 먼저 잡음"));
				}
				HUD->NotifyHitBoxClick(TEXT("Menu_Mode_0"));
				HUD->NotifyHitBoxClick(TEXT("Menu_StartServer"));
				Step = 54;
				Wait(0.3);
				return false;

			case 54:
				Test->AddInfo(FString::Printf(TEXT("MENU port in use: message=%s serverRunning=%d connecting=%d"), *Controller->GetMenuMessage(), Session->IsLocalServerRunning() ? 1 : 0, Session->IsConnecting() ? 1 : 0));
				Expect(Controller->GetMenuMessage().Contains(TEXT("PORT 7777 IS IN USE")), TEXT("7777 사용 중이면 오류 문구"));
				Expect(!Session->IsLocalServerRunning() && !Session->IsConnecting() && World->GetNetMode() == NM_Standalone, TEXT("7777 사용 중이면 서버를 띄우지도 붙지도 않음"));
				FScreenshotRequest::RequestScreenshot(OutputDirectory / TEXT("PortInUse.png"), true, false);
				Step = 55;
				Wait(0.3);
				return false;

			case 55:
				// 포트를 놓고 OK로 문구를 지운 뒤 진짜로 띄운다.
				PortBlocker.Reset();
				HUD->NotifyHitBoxClick(TEXT("Menu_Ok"));

				// 3) 로컬 데디 서버(경기, 최소 1명) 띄우고 붙기.
				HUD->NotifyHitBoxClick(TEXT("Menu_StartServer"));
				ServerStartTime = Now;
				Step = 6;
				return false;

			case 6:
			{
				// 서버 프로세스가 뜨고(에디터 실행 파일이라 수십 초) 접속해 이름이 바뀔 때까지.
				const AValorGameState* GameState = World->GetGameState<AValorGameState>();
				if (World->GetNetMode() != NM_Client || !GameState || !Controller->GetPawn() || !Controller->PlayerState || Controller->PlayerState->GetPlayerName() != TEXT("Tester_1"))
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("MENU joined local server in %.1fs: map=%s netMode=%d serverRunning=%d lobbyMin=%d phase=%d timeLeft=%.1f name=%s"),
					Now - ServerStartTime, *World->GetMapName(), static_cast<int32>(World->GetNetMode()), Session->IsLocalServerRunning() ? 1 : 0, GameState->GetLobbyMinPlayers(),
					static_cast<int32>(GameState->GetRoundPhase()), GameState->GetPhaseTimeRemaining(), *Controller->PlayerState->GetPlayerName()));
				Expect(Session->IsLocalServerRunning(), TEXT("로컬 서버 프로세스가 떠 있음"));
				Expect(GameState->GetLobbyMinPlayers() == 1, TEXT("로컬 서버 대기실 최소 1명"));
				Expect(Controller->PlayerState->GetPlayerName() == TEXT("Tester_1"), TEXT("점수판 이름 = 메뉴에서 입력한 이름(데디 서버가 받음)"));
				Expect(!Controller->bShowMouseCursor, TEXT("게임에서는 커서가 꺼짐"));
				Step = 7;
				Wait(0.3);
				return false;
			}

			case 7:
				FScreenshotRequest::RequestScreenshot(OutputDirectory / TEXT("Lobby.png"), true, false);
				Step = 8;
				return false;

			case 8:
			{
				// 1/1명이 모였으니 카운트다운 뒤 저절로 시작한다(방장 버튼 없음).
				const AValorGameState* GameState = World->GetGameState<AValorGameState>();
				if (!GameState || GameState->GetRoundPhase() == EValorRoundPhase::WaitingToStart)
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("MENU started by countdown: phase=%d after %.1fs in game"), static_cast<int32>(GameState->GetRoundPhase()), Now - ServerStartTime));

				// 4) 나가기: 실제 F10 → LEAVE MATCH 두 번.
				QueueTap(EKeys::F10);
				Step = 11;
				Wait(0.3);
				return false;
			}

			case 11:
				Expect(Controller->IsSettingsOpen(), TEXT("F10 설정 창"));
				HUD->NotifyHitBoxClick(TEXT("Set_Leave"));
				Expect(Controller->IsInMainMenu() == false, TEXT("한 번 누르면 아직 나가지 않음(확인 단계)"));
				Step = 12;
				Wait(0.3);
				return false;

			case 12:
				FScreenshotRequest::RequestScreenshot(OutputDirectory / TEXT("Leave_Confirm.png"), true, false);
				Step = 13;
				Wait(0.5);
				return false;

			case 13:
				HUD->NotifyHitBoxClick(TEXT("Set_Leave"));
				Step = 14;
				return false;

			case 14:
				if (!Controller->IsInMainMenu())
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("MENU back in menu: netMode=%d error=%s name=%s serverRunning=%d"), static_cast<int32>(World->GetNetMode()),
					*Session->GetLastError(), *Controller->GetMenuNameText(), Session->IsLocalServerRunning() ? 1 : 0));
				Expect(World->GetNetMode() == NM_Standalone && Session->GetLastError().IsEmpty(), TEXT("나가면 메뉴(혼자)로, 오류 표시 없음"));
				Expect(Controller->GetMenuNameText() == TEXT("Tester_1"), TEXT("메뉴에 저장된 이름이 다시 보임"));
				Expect(Session->IsLocalServerRunning(), TEXT("나가도 로컬 서버는 계속 떠 있음(다시 들어갈 수 있게)"));
				Step = 15;
				Wait(0.5);
				return false;

			case 15:
				FScreenshotRequest::RequestScreenshot(OutputDirectory / TEXT("Menu.png"), true, false);
				Step = 16;
				Wait(0.5);
				return false;

			case 16:
				HUD->NotifyHitBoxClick(TEXT("Menu_StopServer"));
				Step = 17;
				Wait(2.0);
				return false;

			case 17:
				Test->AddInfo(FString::Printf(TEXT("MENU stopped local server: running=%d"), Session->IsLocalServerRunning() ? 1 : 0));
				Expect(!Session->IsLocalServerRunning(), TEXT("STOP으로 로컬 서버 프로세스가 꺼짐"));
				Step = 18;
				return false;

			default:
				Restore();
				return true;
			}
		}

	private:
		void Expect(bool bCondition, const TCHAR* What)
		{
			if (!bCondition)
			{
				Test->AddError(FString::Printf(TEXT("MENU 실패: %s"), What));
			}
		}

		void Wait(double Seconds)
		{
			WaitUntil = FPlatformTime::Seconds() + Seconds;
		}

		// 실제 키보드와 같은 경로(뷰포트 → 컨트롤러 InputKey)로, 한 프레임에 하나씩 보낸다.
		struct FQueuedKey
		{
			FKey Key;
			bool bDown = true;
		};
		TArray<FQueuedKey> KeyQueue;

		void QueueTap(const FKey& Key)
		{
			KeyQueue.Add({Key, true});
			KeyQueue.Add({Key, false});
		}

		void QueueShiftTap(const FKey& Key)
		{
			KeyQueue.Add({EKeys::LeftShift, true});
			QueueTap(Key);
			KeyQueue.Add({EKeys::LeftShift, false});
		}

		// 숫자·점·콜론만(주소용).
		void QueueAddress(const FString& Text)
		{
			static const FKey Digits[] = {EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
			for (const TCHAR Character : Text)
			{
				if (FChar::IsDigit(Character))
				{
					QueueTap(Digits[Character - TEXT('0')]);
				}
				else if (Character == TEXT('.'))
				{
					QueueTap(EKeys::Period);
				}
				else if (Character == TEXT(':'))
				{
					QueueShiftTap(EKeys::Semicolon);
				}
			}
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

		FAutomationTestBase* Test = nullptr;
		FString OutputDirectory;
		int32 Step = 0;
		double StartTime = -1.0;
		double WaitUntil = 0.0;
		double ServerStartTime = 0.0;
		FUniqueSocket PortBlocker;
		bool bSnapshotTaken = false;
		FString SavedName;
		FString SavedAddress;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorMenuFlowTool, "Valor.Tools.MenuFlow", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorMenuFlowTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorMenuFlowTool::FValorMenuFlowCommand(this));
	return true;
}

#endif

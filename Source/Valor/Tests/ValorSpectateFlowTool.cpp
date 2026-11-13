#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Player/ValorPlayerController.h"
#include "Player/ValorPlayerState.h"
#include "UnrealClient.h"
#include "ValorCharacter.h"
#include "ValorGameState.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 관전 확인(혼자 + 봇, 단독 실행 = 서버 권한)
// 구매 단계에 봇 셋(아군 둘, 적 하나)을 넣고, 교전이 시작되면 나를 죽인다(ValorKillMe). 시체 시점 뒤 아군 관전으로 넘어가는지,
// 실제 왼쪽·오른쪽 클릭으로 아군을 바꾸는지, 보던 아군이 죽으면 다음 아군으로, 아군이 다 죽으면 시체 시점으로 돌아가는지,
// 다음 라운드에 내 캐릭터 시점으로 돌아오는지 본다. 저장 위치: Saved/SpectateFlow. 로그는 SPECTATE로 시작한다.
// 실행:
//   UnrealEditor.exe Valor.uproject "/Game/Maps/ThridPersonExampleMap?game=Match?PreMatchTime=12?BuyTime=4?RoundTime=60?RoundEndTime=3"
//     -game -windowed -ResX=1280 -ResY=720 -ExecCmds="Automation RunTests Valor.Tools.SpectateFlow; Quit"
// =====================================================================================================

namespace ValorSpectateFlowTool
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

	FString NameOf(const AValorCharacter* Character)
	{
		return Character && Character->GetPlayerState() ? Character->GetPlayerState()->GetPlayerName() : FString(TEXT("none"));
	}

	class FValorSpectateFlowCommand : public IAutomationLatentCommand
	{
	public:
		explicit FValorSpectateFlowCommand(FAutomationTestBase* InTest)
			: Test(InTest)
		{
			OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SpectateFlow"));
			IFileManager::Get().MakeDirectory(*OutputDirectory, true);
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (StartTime < 0.0)
			{
				StartTime = Now;
			}
			if (Now - StartTime > 90.0)
			{
				Test->AddError(FString::Printf(TEXT("SPECTATE 90초 안에 끝나지 않았습니다(단계 %d)."), static_cast<int32>(Step)));
				return true;
			}
			if (Now < WaitUntil)
			{
				return false;
			}

			UWorld* World = FindGameWorld();
			AValorGameState* GameState = World ? World->GetGameState<AValorGameState>() : nullptr;
			AValorPlayerController* Controller = World ? Cast<AValorPlayerController>(GEngine->GetFirstLocalPlayerController(World)) : nullptr;
			AValorCharacter* Character = Controller ? Cast<AValorCharacter>(Controller->GetPawn()) : nullptr;
			if (!GameState || !Controller || !Character)
			{
				return false;
			}

			const AActor* ViewTarget = Controller->PlayerCameraManager ? Controller->PlayerCameraManager->GetViewTarget() : nullptr;
			switch (Step)
			{
			case EStep::WaitBuy:
				if (GameState->GetRoundPhase() != EValorRoundPhase::BuyPhase || GameState->GetRoundNumber() != 1)
				{
					return false;
				}
				Controller->ValorAddBot(TEXT("A"));
				Controller->ValorAddBot(TEXT("A"));
				Controller->ValorAddBot(TEXT("B"));
				Step = EStep::WaitCombat;
				return false;

			case EStep::WaitCombat:
				if (GameState->GetRoundPhase() != EValorRoundPhase::Combat)
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("SPECTATE combat: players=%d"), GameState->PlayerArray.Num()));
				Expect(GameState->PlayerArray.Num() == 4, TEXT("나 + 봇 셋"));
				Controller->ValorKillMe();
				Step = EStep::DeathCam;
				Wait(0.5);
				return false;

			case EStep::DeathCam:
				Test->AddInfo(FString::Printf(TEXT("SPECTATE death cam: alive=%d spectating=%d"), Character->IsAlive() ? 1 : 0, Controller->IsSpectating() ? 1 : 0));
				Expect(!Character->IsAlive() && !Controller->IsSpectating(), TEXT("죽은 직후는 시체 시점(아직 관전 아님)"));
				Step = EStep::FirstSpectate;
				Wait(2.2);
				return false;

			case EStep::FirstSpectate:
			{
				const AValorCharacter* Target = Controller->GetSpectateTarget();
				const float Distance = Target && ViewTarget ? FVector::Dist(ViewTarget->GetActorLocation(), Target->GetPawnViewLocation()) : -1.0f;
				Test->AddInfo(FString::Printf(TEXT("SPECTATE start: target=%s viewTargetIsCamera=%d cameraToEye=%.0fcm"), *NameOf(Target),
					Cast<ACameraActor>(ViewTarget) ? 1 : 0, Distance));
				Expect(Target && Target != Character && Target->IsAlive() && ValorTeams::AreAllies(Target, Character), TEXT("살아 있는 아군을 관전"));
				Expect(Cast<ACameraActor>(ViewTarget) != nullptr && Distance > 50.0f && Distance < 300.0f, TEXT("화면이 아군 어깨 너머 카메라"));
				FirstTarget = Target;
				Capture(TEXT("Spectating"));
				Controller->InputKey(FInputKeyParams(EKeys::LeftMouseButton, IE_Pressed, 1.0, false));
				Step = EStep::ReleaseLeft;
				Wait(0.1);
				return false;
			}

			case EStep::ReleaseLeft:
				Controller->InputKey(FInputKeyParams(EKeys::LeftMouseButton, IE_Released, 0.0, false));
				Step = EStep::AfterNext;
				Wait(0.3);
				return false;

			case EStep::AfterNext:
				Test->AddInfo(FString::Printf(TEXT("SPECTATE left click: %s -> %s"), *NameOf(FirstTarget.Get()), *NameOf(Controller->GetSpectateTarget())));
				Expect(Controller->GetSpectateTarget() && Controller->GetSpectateTarget() != FirstTarget.Get(), TEXT("왼쪽 클릭으로 다른 아군"));
				Controller->InputKey(FInputKeyParams(EKeys::RightMouseButton, IE_Pressed, 1.0, false));
				Step = EStep::ReleaseRight;
				Wait(0.1);
				return false;

			case EStep::ReleaseRight:
				Controller->InputKey(FInputKeyParams(EKeys::RightMouseButton, IE_Released, 0.0, false));
				Step = EStep::AfterPrevious;
				Wait(0.3);
				return false;

			case EStep::AfterPrevious:
				Expect(Controller->GetSpectateTarget() == FirstTarget.Get(), TEXT("오른쪽 클릭으로 처음 아군"));
				// 보던 아군을 죽인다 → 남은 아군으로.
				if (AValorCharacter* Watched = Controller->GetSpectateTarget())
				{
					Watched->KillByEnvironment(nullptr);
				}
				Step = EStep::AfterTargetDied;
				Wait(0.3);
				return false;

			case EStep::AfterTargetDied:
			{
				const AValorCharacter* Target = Controller->GetSpectateTarget();
				Test->AddInfo(FString::Printf(TEXT("SPECTATE watched died: now %s"), *NameOf(Target)));
				Expect(Target && Target->IsAlive() && Target != FirstTarget.Get(), TEXT("보던 아군이 죽으면 남은 아군으로"));
				if (AValorCharacter* LastTeammate = Controller->GetSpectateTarget())
				{
					LastTeammate->KillByEnvironment(nullptr);
				}
				Step = EStep::TeamWiped;
				Wait(0.3);
				return false;
			}

			case EStep::TeamWiped:
				Test->AddInfo(FString::Printf(TEXT("SPECTATE team wiped: spectating=%d viewTargetIsMyPawn=%d phase=%d winner=%d reason=%d"), Controller->IsSpectating() ? 1 : 0,
					ViewTarget == Character ? 1 : 0, static_cast<int32>(GameState->GetRoundPhase()), static_cast<int32>(GameState->GetLastRoundWinner()), static_cast<int32>(GameState->GetLastRoundEndReason())));
				Expect(!Controller->IsSpectating() && ViewTarget == Character, TEXT("아군이 다 죽으면 시체 시점으로"));
				Expect(GameState->GetRoundPhase() == EValorRoundPhase::RoundEnd && GameState->GetLastRoundWinner() == EValorTeam::TeamB, TEXT("공격(TeamA) 전멸 → 수비 승"));
				Step = EStep::WaitRound2;
				return false;

			case EStep::WaitRound2:
				if (GameState->GetRoundNumber() != 2 || GameState->GetRoundPhase() != EValorRoundPhase::BuyPhase)
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("SPECTATE round2: alive=%d spectating=%d viewTargetIsMyPawn=%d"), Character->IsAlive() ? 1 : 0, Controller->IsSpectating() ? 1 : 0, ViewTarget == Character ? 1 : 0));
				Expect(Character->IsAlive() && !Controller->IsSpectating() && ViewTarget == Character, TEXT("다음 라운드에는 내 캐릭터 시점"));
				Step = EStep::Done;
				return false;

			case EStep::Done:
			default:
				Test->AddInfo(FString::Printf(TEXT("SPECTATE 캡처 저장 위치: %s"), *OutputDirectory));
				return true;
			}
		}

	private:
		enum class EStep : uint8
		{
			WaitBuy,
			WaitCombat,
			DeathCam,
			FirstSpectate,
			ReleaseLeft,
			AfterNext,
			ReleaseRight,
			AfterPrevious,
			AfterTargetDied,
			TeamWiped,
			WaitRound2,
			Done
		};

		void Expect(bool bCondition, const TCHAR* What)
		{
			if (!bCondition)
			{
				Test->AddError(FString::Printf(TEXT("SPECTATE 실패: %s"), What));
			}
		}

		void Wait(double Seconds)
		{
			WaitUntil = FPlatformTime::Seconds() + Seconds;
		}

		void Capture(const FString& ShotName)
		{
			FScreenshotRequest::RequestScreenshot(OutputDirectory / (ShotName + TEXT(".png")), true, false);
		}

		FAutomationTestBase* Test = nullptr;
		FString OutputDirectory;
		EStep Step = EStep::WaitBuy;
		double StartTime = -1.0;
		double WaitUntil = 0.0;
		TWeakObjectPtr<const AValorCharacter> FirstTarget;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorSpectateFlowTool, "Valor.Tools.SpectateFlow", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorSpectateFlowTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorSpectateFlowTool::FValorSpectateFlowCommand(this));
	return true;
}

#endif

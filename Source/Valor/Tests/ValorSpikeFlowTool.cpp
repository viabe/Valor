#include "Components/ValorCharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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
#include "World/ValorSpike.h"
#include "World/ValorSpikeSite.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 스파이크 설치·폭발 확인(혼자, 단독 실행 = 서버 권한)
// 1라운드 공격(혼자 = TeamA)으로 스파이크를 받고, 지점 A로 순간이동해 실제 4 키를 누르고 있어 설치한다.
// 설치 중 이동 고정, 설치 보너스 300, 설치 단계 전환, 폭발(공격 승)과 폭발 반경 안의 사망을 확인하고 화면을 PNG로 저장한다.
// 맵에 지점이 없으면 경기 모드가 만든 임시 지점 A·B를 쓴다. 저장 위치: Saved/SpikeFlow. 로그는 SPIKE로 시작한다.
// 실행:
//   UnrealEditor.exe Valor.uproject "/Game/Maps/ThridPersonExampleMap?game=Match?PreMatchTime=15?BuyTime=3?RoundTime=60?SpikeTime=5?RoundEndTime=3"
//     -game -windowed -ResX=1280 -ResY=720 -ExecCmds="Automation RunTests Valor.Tools.SpikeFlow; Quit"
// =====================================================================================================

namespace ValorSpikeFlowTool
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

	class FValorSpikeFlowCommand : public IAutomationLatentCommand
	{
	public:
		explicit FValorSpikeFlowCommand(FAutomationTestBase* InTest)
			: Test(InTest)
		{
			OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SpikeFlow"));
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
				Test->AddError(FString::Printf(TEXT("SPIKE 90초 안에 끝나지 않았습니다(단계 %d)."), static_cast<int32>(Step)));
				return true;
			}
			if (Now < WaitUntil)
			{
				return false;
			}

			UWorld* World = FindGameWorld();
			AValorGameState* GameState = World ? World->GetGameState<AValorGameState>() : nullptr;
			AValorPlayerController* Controller = World ? Cast<AValorPlayerController>(GEngine->GetFirstLocalPlayerController(World)) : nullptr;
			AValorPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AValorPlayerState>() : nullptr;
			AValorCharacter* Character = Controller ? Cast<AValorCharacter>(Controller->GetPawn()) : nullptr;
			AValorSpike* Spike = AValorSpike::FindSpike(World);
			if (!GameState || !PlayerState || !Character)
			{
				return false;
			}

			switch (Step)
			{
			case EStep::WaitCombat:
			{
				if (GameState->GetRoundPhase() != EValorRoundPhase::Combat || GameState->GetRoundNumber() != 1 || !Spike)
				{
					return false;
				}

				TArray<AValorSpikeSite*> Sites;
				for (TActorIterator<AValorSpikeSite> It(World); It; ++It)
				{
					Sites.Add(*It);
				}
				Test->AddInfo(FString::Printf(TEXT("SPIKE combat: sites=%d carrier=%s state=%d"), Sites.Num(), *GetNameSafe(Spike->GetCarrier()), static_cast<int32>(Spike->GetState())));
				Expect(Sites.Num() >= 2, TEXT("지점이 둘 이상(맵 또는 임시)"));
				Expect(Spike->GetCarrier() == Character && Spike->GetState() == EValorSpikeState::Carried, TEXT("공격(혼자)이 스파이크를 들고 시작"));
				Expect(!Spike->CanBeUsedBy(*Character), TEXT("지점 밖에서는 설치할 수 없음"));

				AValorSpikeSite* const* FoundSiteA = Sites.FindByPredicate([](const AValorSpikeSite* Site) { return Site->GetSiteName() == TEXT("A"); });
				const AValorSpikeSite* SiteA = FoundSiteA ? *FoundSiteA : nullptr;
				if (!SiteA)
				{
					Test->AddError(TEXT("SPIKE 지점 A가 없습니다."));
					return true;
				}
				Character->TeleportTo(SiteA->GetActorLocation() + FVector(0.0f, 0.0f, 60.0f), Character->GetActorRotation());
				Step = EStep::PressPlant;
				Wait(1.0);
				return false;
			}

			case EStep::PressPlant:
				Expect(Spike && Spike->CanBeUsedBy(*Character), TEXT("지점 A 안, 땅 위에서는 설치할 수 있음"));
				CreditsBeforePlant = PlayerState->GetCredits();
				Controller->InputKey(FInputKeyParams(EKeys::Four, IE_Pressed, 1.0, false));
				Step = EStep::MidPlant;
				Wait(1.5);
				return false;

			case EStep::MidPlant:
				Test->AddInfo(FString::Printf(TEXT("SPIKE planting: progress=%.2f maxSpeed=%.0f wantsUse=%d"), Spike ? Spike->GetPlantProgress() : -1.0f,
					Character->GetCharacterMovement()->GetMaxSpeed(), Cast<UValorCharacterMovementComponent>(Character->GetCharacterMovement())->WantsToUseSpike() ? 1 : 0));
				Expect(Spike && Spike->GetPlantProgress() > 0.5f, TEXT("4 키를 누르고 있으면 설치 진행"));
				Expect(Character->GetCharacterMovement()->GetMaxSpeed() == 0.0f, TEXT("설치 중 이동 고정"));
				Capture(TEXT("Planting"));
				Step = EStep::WaitPlanted;
				return false;

			case EStep::WaitPlanted:
				if (!Spike || Spike->GetState() != EValorSpikeState::Planted)
				{
					return false;
				}
				Controller->InputKey(FInputKeyParams(EKeys::Four, IE_Released, 0.0, false));
				Test->AddInfo(FString::Printf(TEXT("SPIKE planted: site=%s phase=%d credits=%d (before %d) timeLeft=%.1f"), *Spike->GetPlantedSiteName().ToString(),
					static_cast<int32>(GameState->GetRoundPhase()), PlayerState->GetCredits(), CreditsBeforePlant, GameState->GetPhaseTimeRemaining()));
				Expect(GameState->GetRoundPhase() == EValorRoundPhase::SpikePlanted, TEXT("설치 단계로 전환"));
				Expect(Spike->GetPlantedSiteName() == TEXT("A"), TEXT("지점 A에 설치"));
				Expect(PlayerState->GetCredits() == CreditsBeforePlant + 300, TEXT("설치 보너스 300"));
				Step = EStep::PlantedShot;
				Wait(0.5);
				return false;

			case EStep::PlantedShot:
				Expect(Character->GetCharacterMovement()->GetMaxSpeed() > 0.0f, TEXT("설치 뒤 다시 움직임"));
				Capture(TEXT("Planted"));
				Step = EStep::WaitDetonation;
				return false;

			case EStep::WaitDetonation:
				if (GameState->GetRoundPhase() != EValorRoundPhase::RoundEnd)
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("SPIKE round end: winner=%d reason=%d spikeState=%d alive=%d"), static_cast<int32>(GameState->GetLastRoundWinner()),
					static_cast<int32>(GameState->GetLastRoundEndReason()), Spike ? static_cast<int32>(Spike->GetState()) : -1, Character->IsAlive() ? 1 : 0));
				Expect(GameState->GetLastRoundWinner() == EValorTeam::TeamA && GameState->GetLastRoundEndReason() == EValorRoundEndReason::SpikeDetonated, TEXT("폭발 → 공격 승"));
				Expect(!Character->IsAlive(), TEXT("폭발 반경 안에 있던 사람은 죽음"));
				Step = EStep::Done;
				Wait(0.3);
				return false;

			case EStep::Done:
			default:
				Test->AddInfo(FString::Printf(TEXT("SPIKE 캡처 저장 위치: %s"), *OutputDirectory));
				return true;
			}
		}

	private:
		enum class EStep : uint8
		{
			WaitCombat,
			PressPlant,
			MidPlant,
			WaitPlanted,
			PlantedShot,
			WaitDetonation,
			Done
		};

		void Expect(bool bCondition, const TCHAR* What)
		{
			if (!bCondition)
			{
				Test->AddError(FString::Printf(TEXT("SPIKE 실패: %s"), What));
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
		EStep Step = EStep::WaitCombat;
		double StartTime = -1.0;
		double WaitUntil = 0.0;
		int32 CreditsBeforePlant = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorSpikeFlowTool, "Valor.Tools.SpikeFlow", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorSpikeFlowTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorSpikeFlowTool::FValorSpikeFlowCommand(this));
	return true;
}

#endif

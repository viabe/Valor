#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystemComponent.h"
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
#include "World/ValorSmokeCloud.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 요원 스킬 틀 확인(혼자, 단독 실행 = 서버 권한)
// 샘플 요원 Drift(C 회복 / Q 연막 통 / E 돌진 / X 과충전)로 스킬 충전 구매·되팔기, 실제 C·Q·E·X 키 사용, 충전 소모,
// 궁극기 포인트, 다음 라운드 충전 유지·시그니처 재충전을 확인하고 상점·스킬 칸 화면을 PNG로 저장한다.
// 저장 위치: Saved/AgentFlow. 로그는 AGENT로 시작한다.
// 실행:
//   UnrealEditor.exe Valor.uproject "/Game/Maps/ThridPersonExampleMap?game=Match?PreMatchTime=12?BuyTime=25?RoundTime=3?RoundEndTime=2"
//     -game -windowed -ResX=1280 -ResY=720 -ExecCmds="Automation RunTests Valor.Tools.AgentFlow; Quit"
// =====================================================================================================

namespace ValorAgentFlowTool
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

	class FValorAgentFlowCommand : public IAutomationLatentCommand
	{
	public:
		explicit FValorAgentFlowCommand(FAutomationTestBase* InTest)
			: Test(InTest)
		{
			OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AgentFlow"));
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
				Test->AddError(FString::Printf(TEXT("AGENT 90초 안에 끝나지 않았습니다(단계 %d)."), Step));
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
			UAbilitySystemComponent* AbilitySystem = Character ? Character->GetAbilitySystemComponent() : nullptr;
			if (!GameState || !PlayerState || !Character || !AbilitySystem)
			{
				return false;
			}

			auto Charges = [PlayerState](EValorAbilitySlot Slot) { return PlayerState->GetAbilityCharges(Slot); };
			auto Health = [AbilitySystem]() { return AbilitySystem->GetNumericAttribute(UValorCombatAttributeSet::GetHealthAttribute()); };
			auto Shield = [AbilitySystem]() { return AbilitySystem->GetNumericAttribute(UValorCombatAttributeSet::GetShieldAttribute()); };
			auto CountSmoke = [World]() { int32 Count = 0; for (TActorIterator<AValorSmokeCloud> It(World); It; ++It) { ++Count; } return Count; };
			auto Press = [Controller](FKey Key) { Controller->InputKey(FInputKeyParams(Key, IE_Pressed, 1.0, false)); };
			auto Release = [Controller](FKey Key) { Controller->InputKey(FInputKeyParams(Key, IE_Released, 0.0, false)); };

			switch (Step)
			{
			case 0:
				if (GameState->GetRoundPhase() != EValorRoundPhase::BuyPhase || GameState->GetRoundNumber() != 1)
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("AGENT round1: agent=%s C=%d Q=%d E=%d ult=%d"), PlayerState->GetAgent() ? *PlayerState->GetAgent()->DisplayName.ToString() : TEXT("none"),
					Charges(EValorAbilitySlot::Ability1), Charges(EValorAbilitySlot::Ability2), Charges(EValorAbilitySlot::Signature), PlayerState->GetUltimatePoints()));
				Expect(PlayerState->GetAgent() != nullptr, TEXT("기본 요원 지정"));
				Expect(Charges(EValorAbilitySlot::Ability1) == 0 && Charges(EValorAbilitySlot::Ability2) == 0 && Charges(EValorAbilitySlot::Signature) == 1, TEXT("1라운드: 시그니처만 무료 1"));
				PlayerState->SetCredits(9000);
				Controller->ServerBuyAbility(1);
				Controller->ServerBuyAbility(1);
				Controller->ServerBuyAbility(1);
				Controller->ServerBuyAbility(0);
				Expect(Charges(EValorAbilitySlot::Ability2) == 2 && Charges(EValorAbilitySlot::Ability1) == 1 && PlayerState->GetCredits() == 9000 - 100 - 100 - 200, TEXT("Q 둘(최대)·C 하나 구매, 세 번째 Q는 거부"));
				Controller->ServerSellAbility(1);
				Expect(Charges(EValorAbilitySlot::Ability2) == 1 && PlayerState->GetCredits() == 8700, TEXT("Q 하나 되팔기"));
				Controller->ServerBuyAbility(1);
				Controller->ServerBuyAbility(2);
				Expect(Charges(EValorAbilitySlot::Signature) == 1 && PlayerState->GetCredits() == 8600, TEXT("시그니처는 상점에서 사지 않음"));
				Controller->SetBuyMenuOpen(true);
				Step = 1;
				Wait(0.5);
				return false;

			case 1:
				Capture(TEXT("AbilityShop"));
				Controller->SetBuyMenuOpen(false);
				SmokeBefore = CountSmoke();
				Press(EKeys::Q);
				Step = 2;
				Wait(0.1);
				return false;

			case 2:
				Release(EKeys::Q);
				Step = 3;
				Wait(0.4);
				return false;

			case 3:
				Test->AddInfo(FString::Printf(TEXT("AGENT Q smoke: clouds %d -> %d, Q charges=%d"), SmokeBefore, CountSmoke(), Charges(EValorAbilitySlot::Ability2)));
				Expect(CountSmoke() == SmokeBefore + 1 && Charges(EValorAbilitySlot::Ability2) == 1, TEXT("Q: 연막 생성, 충전 -1"));
				DashStart = Character->GetActorLocation();
				Press(EKeys::E);
				Step = 4;
				Wait(0.1);
				return false;

			case 4:
				Release(EKeys::E);
				Step = 5;
				Wait(0.5);
				return false;

			case 5:
			{
				const float Moved = FVector::Dist2D(DashStart, Character->GetActorLocation());
				Test->AddInfo(FString::Printf(TEXT("AGENT E dash: moved=%.0fcm E charges=%d"), Moved, Charges(EValorAbilitySlot::Signature)));
				Expect(Moved > 250.0f && Charges(EValorAbilitySlot::Signature) == 0, TEXT("E: 돌진, 충전 -1"));
				AbilitySystem->SetNumericAttributeBase(UValorCombatAttributeSet::GetHealthAttribute(), 50.0f);
				Press(EKeys::C);
				Step = 6;
				Wait(0.1);
				return false;
			}

			case 6:
				Release(EKeys::C);
				Step = 7;
				Wait(0.3);
				return false;

			case 7:
				Test->AddInfo(FString::Printf(TEXT("AGENT C mend: health=%.0f C charges=%d"), Health(), Charges(EValorAbilitySlot::Ability1)));
				Expect(Health() == 90.0f && Charges(EValorAbilitySlot::Ability1) == 0, TEXT("C: 체력 50 → 90, 충전 -1"));
				AbilitySystem->SetNumericAttributeBase(UValorCombatAttributeSet::GetHealthAttribute(), 30.0f);
				Press(EKeys::X);
				Step = 8;
				Wait(0.1);
				return false;

			case 8:
				Release(EKeys::X);
				Step = 9;
				Wait(0.3);
				return false;

			case 9:
				Expect(Health() == 30.0f, TEXT("X: 궁극기 포인트 없으면 안 됨"));
				Controller->ValorUltPoints(6);
				Step = 10;
				Wait(0.2);
				return false;

			case 10:
				Press(EKeys::X);
				Step = 11;
				Wait(0.1);
				return false;

			case 11:
				Release(EKeys::X);
				Step = 12;
				Wait(0.3);
				return false;

			case 12:
				Test->AddInfo(FString::Printf(TEXT("AGENT X overdrive: health=%.0f shield=%.0f ult=%d"), Health(), Shield(), PlayerState->GetUltimatePoints()));
				Expect(Health() == 100.0f && Shield() == 50.0f && PlayerState->GetUltimatePoints() == 0, TEXT("X: 체력·실드 가득, 포인트 0"));
				Capture(TEXT("AbilityBar"));
				Step = 13;
				return false;

			case 13:
				if (GameState->GetRoundNumber() != 2 || GameState->GetRoundPhase() != EValorRoundPhase::BuyPhase)
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("AGENT round2: C=%d Q=%d E=%d ult=%d"), Charges(EValorAbilitySlot::Ability1), Charges(EValorAbilitySlot::Ability2),
					Charges(EValorAbilitySlot::Signature), PlayerState->GetUltimatePoints()));
				Expect(Charges(EValorAbilitySlot::Ability2) == 1 && Charges(EValorAbilitySlot::Ability1) == 0 && Charges(EValorAbilitySlot::Signature) == 1, TEXT("2라운드: 남은 Q 유지, 시그니처 다시 1"));
				Step = 14;
				return false;

			default:
				Test->AddInfo(FString::Printf(TEXT("AGENT 캡처 저장 위치: %s"), *OutputDirectory));
				return true;
			}
		}

	private:
		void Expect(bool bCondition, const TCHAR* What)
		{
			if (!bCondition)
			{
				Test->AddError(FString::Printf(TEXT("AGENT 실패: %s"), What));
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
		int32 Step = 0;
		double StartTime = -1.0;
		double WaitUntil = 0.0;
		int32 SmokeBefore = 0;
		FVector DashStart = FVector::ZeroVector;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorAgentFlowTool, "Valor.Tools.AgentFlow", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorAgentFlowTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorAgentFlowTool::FValorAgentFlowCommand(this));
	return true;
}

#endif

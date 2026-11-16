#include "AbilitySystem/Effects/ValorDamageGameplayEffect.h"
#include "AbilitySystem/ValorDamageTypes.h"
#include "AbilitySystem/ValorGameplayTags.h"
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
#include "World/ValorAbilityEffects.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 어시스트·전투 점수·점수판 확인(혼자 + 아군 봇 1 + 적 봇 4, 단독 실행 = 서버 권한)
// 봇은 총을 쏘지 않으므로 피해는 도구가 그 캐릭터의 GAS로 직접 보낸다(실제 피해 GE 경로 그대로). 섬광은 실제 섬광 효과를 터뜨린다.
//  나 → 적1 50, 적2 20, 적3 20 피해. 1초 뒤 아군이 적2 처치(최근 20 → 어시스트). 내가 적4 앞에서 섬광 → 아군이 적4 처치(섬광 어시스트 +25).
//  3초 넘게 지나 아군이 적1 처치(언제든 50 → 어시스트), 적3 처치(3초 지난 20 → 어시스트 아님).
//  내 어시스트 3, 내 전투 점수 = 피해 90 + 25 = 115. 아군: 처치 4(130 / 110+50 / 90+50 / 70+50) + 남은 체력 피해 = 860.
//  마지막에 실제 Tab 키로 점수판을 열어 캡처한다(Saved/ScoreFlow). 로그는 SCORE로 시작한다.
// 실행:
//   UnrealEditor.exe Valor.uproject "/Game/Maps/ThridPersonExampleMap?game=Match?PreMatchTime=12?SelectTime=1?BuyTime=40?RoundTime=3?RoundEndTime=8"
//     -game -windowed -ResX=1280 -ResY=720 -ExecCmds="Automation RunTests Valor.Tools.ScoreFlow; Quit"
// =====================================================================================================

namespace ValorScoreFlowTool
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

	// 실제 피해 GE 경로(공격자 ASC → 맞는 사람 속성)로 피해를 보낸다. 공격자 = 처치·어시스트를 받을 사람.
	void ApplyTestDamage(AValorCharacter& Source, AValorCharacter& Target, float Damage)
	{
		UAbilitySystemComponent* SourceAbilitySystem = Source.GetAbilitySystemComponent();
		UAbilitySystemComponent* TargetAbilitySystem = Target.GetAbilitySystemComponent();
		if (!SourceAbilitySystem || !TargetAbilitySystem)
		{
			return;
		}
		FGameplayEffectContextHandle Context = SourceAbilitySystem->MakeEffectContext();
		Context.AddInstigator(&Source, &Source);
		Context.AddOrigin(Source.GetActorLocation());
		const FGameplayEffectSpecHandle Spec = SourceAbilitySystem->MakeOutgoingSpec(UValorDamageGameplayEffect::StaticClass(), 1.0f, Context);
		if (!Spec.IsValid())
		{
			return;
		}
		Spec.Data->SetSetByCallerMagnitude(ValorGameplayTags::SetByCaller_Damage, Damage);
		Spec.Data->AddDynamicAssetTag(ValorDamage::HitZoneToTag(EValorHitZone::Body));
		SourceAbilitySystem->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetAbilitySystem);
	}

	class FValorScoreFlowCommand : public IAutomationLatentCommand
	{
	public:
		explicit FValorScoreFlowCommand(FAutomationTestBase* InTest)
			: Test(InTest)
		{
			OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ScoreFlow"));
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
				Test->AddError(FString::Printf(TEXT("SCORE 90초 안에 끝나지 않았습니다(단계 %d)."), Step));
				return true;
			}
			if (Now < WaitUntil)
			{
				return false;
			}

			UWorld* World = FindGameWorld();
			AValorGameState* GameState = World ? World->GetGameState<AValorGameState>() : nullptr;
			AValorPlayerController* Controller = World ? Cast<AValorPlayerController>(GEngine->GetFirstLocalPlayerController(World)) : nullptr;
			AValorPlayerState* MyState = Controller ? Controller->GetPlayerState<AValorPlayerState>() : nullptr;
			AValorCharacter* Me = Controller ? Cast<AValorCharacter>(Controller->GetPawn()) : nullptr;
			if (!GameState || !MyState || !Me)
			{
				return false;
			}

			switch (Step)
			{
			case 0:
				if (GameState->GetRoundPhase() != EValorRoundPhase::BuyPhase || GameState->GetRoundNumber() != 1)
				{
					return false;
				}
				Controller->ValorAddBot(TEXT("A"));
				for (int32 Index = 0; Index < 4; ++Index)
				{
					Controller->ValorAddBot(TEXT("B"));
				}
				Step = 1;
				Wait(0.8);
				return false;

			case 1:
			{
				for (TActorIterator<AValorCharacter> It(World); It; ++It)
				{
					if (*It == Me)
					{
						continue;
					}
					if (ValorTeams::GetTeamOf(*It) == MyState->GetTeam())
					{
						Ally = *It;
					}
					else
					{
						Enemies.Add(*It);
					}
				}
				Test->AddInfo(FString::Printf(TEXT("SCORE bots: ally=%s enemies=%d"), *GetNameSafe(Ally.Get()), Enemies.Num()));
				Expect(Ally.IsValid() && Enemies.Num() == 4, TEXT("아군 봇 1, 적 봇 4"));
				if (!Ally.IsValid() || Enemies.Num() != 4)
				{
					return true;
				}
				ApplyTestDamage(*Me, *Enemies[0], 50.0f);
				ApplyTestDamage(*Me, *Enemies[1], 20.0f);
				ApplyTestDamage(*Me, *Enemies[2], 20.0f);
				Step = 2;
				Wait(1.0);
				return false;
			}

			case 2:
			{
				// 아군이 적2 처치(내 최근 피해 20 > 10).
				ApplyTestDamage(*Ally, *Enemies[1], 200.0f);

				// 적4를 내 앞 5m에 세우고(나를 봄), 그 눈앞에서 내 섬광을 터뜨린다.
				const FVector Forward = FRotator(0.0f, Me->GetActorRotation().Yaw, 0.0f).Vector();
				AValorCharacter* Flashed = Enemies[3].Get();
				Flashed->TeleportTo(Me->GetActorLocation() + (Forward * 500.0f), (-Forward).Rotation());
				if (AController* FlashedController = Flashed->GetController())
				{
					FlashedController->SetControlRotation((-Forward).Rotation());
				}
				Step = 3;
				Wait(0.3);
				return false;
			}

			case 3:
			{
				AValorCharacter* Flashed = Enemies[3].Get();
				FActorSpawnParameters Params;
				Params.Instigator = Me;
				Params.Owner = Me;
				const FVector FlashLocation = Flashed->GetPawnViewLocation() + (Flashed->GetBaseAimRotation().Vector() * 120.0f);
				World->SpawnActor<AValorFlashBurst>(AValorFlashBurst::StaticClass(), FlashLocation, FRotator::ZeroRotator, Params);
				Step = 4;
				Wait(0.5);
				return false;
			}

			case 4:
				// 아군이 섬광 맞은 적4 처치(내 섬광 어시스트, 피해 없음 → +25).
				ApplyTestDamage(*Ally, *Enemies[3], 200.0f);
				Step = 5;
				Wait(3.2);
				return false;

			case 5:
				// 내 피해에서 3초 넘게 지났다: 적1(50) → 어시스트, 적3(20) → 아님.
				ApplyTestDamage(*Ally, *Enemies[0], 200.0f);
				ApplyTestDamage(*Ally, *Enemies[2], 200.0f);
				Step = 6;
				Wait(0.5);
				return false;

			case 6:
			{
				const AValorPlayerState* AllyState = Ally.IsValid() ? Ally->GetPlayerState<AValorPlayerState>() : nullptr;
				Test->AddInfo(FString::Printf(TEXT("SCORE me: K=%d D=%d A=%d score=%d | ally: K=%d A=%d score=%d | phase=%d"),
					MyState->GetKills(), MyState->GetDeaths(), MyState->GetAssists(), MyState->GetCombatScore(),
					AllyState ? AllyState->GetKills() : -1, AllyState ? AllyState->GetAssists() : -1, AllyState ? AllyState->GetCombatScore() : -1,
					static_cast<int32>(GameState->GetRoundPhase())));
				Expect(MyState->GetAssists() == 3, TEXT("내 어시스트 3(최근 20, 섬광, 언제든 50). 3초 지난 20은 아님"));
				Expect(MyState->GetCombatScore() == 115, TEXT("내 전투 점수 = 피해 90 + 피해 없는 어시스트 25"));
				Expect(AllyState && AllyState->GetKills() == 4, TEXT("아군 처치 4"));
				Expect(AllyState && AllyState->GetCombatScore() == 860, TEXT("아군 전투 점수 860(처치 130/160/140/120 + 남은 체력 피해 310)"));

				// 실제 Tab 키를 누르고 있는 동안 점수판.
				Controller->InputKey(FInputKeyParams(EKeys::Tab, IE_Pressed, 1.0, false));
				Step = 7;
				Wait(0.4);
				return false;
			}

			case 7:
				FScreenshotRequest::RequestScreenshot(OutputDirectory / TEXT("Scoreboard.png"), true, false);
				Step = 8;
				Wait(0.6);
				return false;

			case 8:
				Controller->InputKey(FInputKeyParams(EKeys::Tab, IE_Released, 0.0, false));
				Step = 9;
				Wait(0.3);
				return false;

			default:
				return true;
			}
		}

	private:
		void Expect(bool bCondition, const TCHAR* What)
		{
			if (!bCondition)
			{
				Test->AddError(FString::Printf(TEXT("SCORE 실패: %s"), What));
			}
		}

		void Wait(double Seconds)
		{
			WaitUntil = FPlatformTime::Seconds() + Seconds;
		}

		FAutomationTestBase* Test = nullptr;
		FString OutputDirectory;
		int32 Step = 0;
		double StartTime = -1.0;
		double WaitUntil = 0.0;
		TWeakObjectPtr<AValorCharacter> Ally;
		TArray<TWeakObjectPtr<AValorCharacter>> Enemies;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorScoreFlowTool, "Valor.Tools.ScoreFlow", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorScoreFlowTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorScoreFlowTool::FValorScoreFlowCommand(this));
	return true;
}

#endif

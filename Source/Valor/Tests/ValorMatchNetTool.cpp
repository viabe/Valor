#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ValorCombatComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Player/ValorPlayerController.h"
#include "Player/ValorPlayerState.h"
#include "AbilitySystem/ValorAgentTypes.h"
#include "ValorCharacter.h"
#include "ValorGameState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "World/ValorSpike.h"
#include "World/ValorSpikeSite.h"
#include "Weapons/ValorMeleeWeapon.h"
#include "Weapons/ValorWeaponBase.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 경기 모드 네트워크 확인(리슨 서버 + 클라이언트 두 프로세스)
// 1라운드: 두 사람이 서로 다른 팀·서로 다른 스폰에 서는지, 교전 중 한 팀이 전멸하면 라운드가 끝나는지(전멸 승), 처치 200 + 승리 3000 /
// 패배 1900 보상이 맞는지 본다. 교전이 시작되면 호스트가 클라를 자기 정면 1.3m에 세운다.
// 클라는 실제 3 키로 칼을 꺼내 앞 베기(50)를 하고, 맞은 호스트가 등을 돌리면 등 뒤 찌르기(150)로 처치한다(칼 예측·서버 검증·되감기·뒤치기).
// 반응 시간: 클라가 요원 선택 단계에 요원을 고르고 확정하며, 구매 단계에 실드를 사고 되판다. 서버 RPC를 보낸 뒤 결과(PlayerState 복제)가
// 클라에 보이기까지의 시간을 잰다(같은 PC라 거의 네트워크 틱 몇 번이어야 한다. 엔진 기본 PlayerState 주기 1초면 평균 0.5초).
// 실행(호스트 먼저, 약 25초 뒤 클라):
//   UnrealEditor.exe Valor.uproject "/Game/Maps/ThridPersonExampleMap?listen?game=Match?PreMatchTime=45?SelectTime=8?BuyTime=6?RoundTime=40?RoundEndTime=4"
//     -game -ExecCmds="Automation RunTests Valor.Tools.MatchNet.Host; Quit"
//   UnrealEditor.exe Valor.uproject 127.0.0.1 -game -ExecCmds="Automation RunTests Valor.Tools.MatchNet.Client; Quit"
// =====================================================================================================

namespace ValorMatchNetTool
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

	class FValorMatchNetCommand : public IAutomationLatentCommand
	{
	public:
		FValorMatchNetCommand(FAutomationTestBase* InTest, bool bInHost)
			: Test(InTest)
			, bHost(bInHost)
		{
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (StartTime < 0.0)
			{
				StartTime = Now;
			}
			if (Now - StartTime > 150.0)
			{
				Test->AddError(TEXT("MATCHNET 150초 안에 끝나지 않았습니다."));
				return true;
			}

			UWorld* World = FindGameWorld();
			AValorGameState* GameState = World ? World->GetGameState<AValorGameState>() : nullptr;
			APlayerController* Controller = World ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
			AValorPlayerState* MyState = Controller ? Controller->GetPlayerState<AValorPlayerState>() : nullptr;
			if (!GameState || !MyState)
			{
				return false;
			}

			// 상대 PlayerState와 캐릭터(시체 포함)를 찾는다.
			AValorPlayerState* OtherState = nullptr;
			for (APlayerState* PlayerState : GameState->PlayerArray)
			{
				if (PlayerState != MyState && Cast<AValorPlayerState>(PlayerState))
				{
					OtherState = Cast<AValorPlayerState>(PlayerState);
				}
			}
			AValorCharacter* Me = Cast<AValorCharacter>(Controller->GetPawn());
			AValorCharacter* Other = OtherState ? Cast<AValorCharacter>(OtherState->GetPawn()) : nullptr;

			if (bReported)
			{
				// 호스트는 클라가 결과를 보고 나갈 때까지 기다린다.
				return !bHost || !OtherState || Now - ReportTime > 20.0;
			}

			if (!bHost)
			{
				MeasureAgentSelect(Now, *GameState, *MyState, Controller);
			}

			if (!OtherState || !Me || GameState->GetRoundNumber() < 1)
			{
				return false;
			}

			const EValorRoundPhase Phase = GameState->GetRoundPhase();
			if (Phase == EValorRoundPhase::BuyPhase && !bLoggedBuy && Other)
			{
				bLoggedBuy = true;
				Test->AddInfo(FString::Printf(TEXT("MATCHNET %s buy: myTeam=%d mySide=%d otherTeam=%d spawnDistance=%.0fcm credits=%d"),
					bHost ? TEXT("Host") : TEXT("Client"), static_cast<int32>(MyState->GetTeam()), static_cast<int32>(GameState->GetSideOfTeam(MyState->GetTeam())),
					static_cast<int32>(OtherState->GetTeam()), FVector::Dist(Me->GetActorLocation(), Other->GetActorLocation()), MyState->GetCredits()));
				Expect(MyState->GetTeam() != EValorTeam::None && MyState->GetTeam() != OtherState->GetTeam(), TEXT("두 사람은 서로 다른 팀"));
				Expect(FVector::Dist(Me->GetActorLocation(), Other->GetActorLocation()) > 500.0f, TEXT("두 팀 스폰이 5m 이상 떨어짐"));

				if (bHost)
				{
					if (UClass* Vandal = LoadObject<UClass>(nullptr, TEXT("/Game/Valor/Gun/BP_Vandal.BP_Vandal_C")))
					{
						Me->GetCombatComponent()->GiveWeapon(Vandal, true, true);
						Other->GetCombatComponent()->GiveWeapon(Vandal, true, true);
					}
				}
				BuyMeasureStartTime = Now + 1.0;
			}

			if (!bHost && Phase == EValorRoundPhase::BuyPhase && GameState->GetRoundNumber() == 1)
			{
				MeasureShopLatency(Now, *GameState, *MyState, Controller);
			}
			if (!bHost && Phase == EValorRoundPhase::Combat && GameState->GetRoundNumber() == 1 && !bReportedLatency)
			{
				ReportLatency();
			}

			if (Phase == EValorRoundPhase::Combat && Other && GameState->GetRoundNumber() == 1)
			{
				if (bHost && !bPlacedClient)
				{
					bPlacedClient = true;
					const FVector Forward = FRotator(0.0f, Me->GetActorRotation().Yaw, 0.0f).Vector();
					Other->TeleportTo(Me->GetActorLocation() + (Forward * 130.0f), (-Forward).Rotation());
				}

				// 호스트: 앞 베기를 맞으면(체력 50 이하) 등을 돌린다 → 클라가 뒤치기를 한다.
				if (bHost && !bTurnedAway && Me->IsAlive() && Me->GetCombatAttributeSet() && Me->GetCombatAttributeSet()->GetHealth() <= 50.0f)
				{
					bTurnedAway = true;
					HealthAfterFirstHit = Me->GetCombatAttributeSet()->GetHealth();
					Controller->SetControlRotation((Me->GetActorLocation() - Other->GetActorLocation()).GetSafeNormal2D().Rotation());
				}

				// 클라: 실제 3 키로 칼을 꺼내고, 호스트 가슴을 겨눠 앞이면 베기, 등 뒤면 찌르기.
				if (bKnifeKeyDown)
				{
					Controller->InputKey(FInputKeyParams(EKeys::Three, IE_Released, 0.0, false));
					bKnifeKeyDown = false;
				}
				UValorCombatComponent* MyCombat = Me->GetCombatComponent();
				if (!bHost && Other->IsAlive() && MyCombat && FVector::Dist(Me->GetActorLocation(), Other->GetActorLocation()) < 250.0f)
				{
					if (!MyCombat->IsMeleeActive() && !bKnifeRequested)
					{
						bKnifeRequested = true;
						Controller->InputKey(FInputKeyParams(EKeys::Three, IE_Pressed, 1.0, false));
						bKnifeKeyDown = true;
					}

					FVector ViewLocation;
					FRotator ViewRotation;
					Me->GetActorEyesViewPoint(ViewLocation, ViewRotation);
					Controller->SetControlRotation(((Other->GetActorLocation() + FVector(0.0f, 0.0f, 30.0f)) - ViewLocation).Rotation());

					if (MyCombat->IsMeleeActive() && MyCombat->IsEquipReady() && Now >= NextShotTime)
					{
						const bool bBehind = ValorMelee::IsBackstab(Me->GetActorLocation(), Other->GetActorLocation(), Other->GetActorForwardVector(), 60.0f);
						if (bBehind)
						{
							MyCombat->HandleADSInputPressed();
							MyCombat->HandleADSInputReleased();
							bUsedBackstab = true;
							NextShotTime = Now + 1.1;
						}
						else
						{
							Me->HandleFireInputPressed();
							Me->HandleFireInputReleased();
							NextShotTime = Now + 0.6;
						}
						++ShotsFired;
					}
				}
			}

			// 라운드 결과(단계)와 보상(PlayerState 크레딧·처치)은 따로 복제되므로, 결과를 본 뒤 1초 기다렸다가 확인한다.
			if ((Phase == EValorRoundPhase::RoundEnd || Phase == EValorRoundPhase::MatchEnd) && GameState->GetRoundNumber() == 1 && RoundEndSeenTime < 0.0)
			{
				RoundEndSeenTime = Now;
			}
			if ((Phase == EValorRoundPhase::RoundEnd || Phase == EValorRoundPhase::MatchEnd) && GameState->GetRoundNumber() == 1 && !bReportedRound1 && Now - RoundEndSeenTime >= 1.0)
			{
				const bool bIWon = GameState->GetLastRoundWinner() == MyState->GetTeam();
				Test->AddInfo(FString::Printf(TEXT("MATCHNET %s round end: winner=%d reason=%d iWon=%d score me%d:other%d credits=%d otherCredits=%d kills=%d shots=%d"),
					bHost ? TEXT("Host") : TEXT("Client"), static_cast<int32>(GameState->GetLastRoundWinner()), static_cast<int32>(GameState->GetLastRoundEndReason()), bIWon ? 1 : 0,
					GameState->GetTeamScore(MyState->GetTeam()), GameState->GetTeamScore(OtherState->GetTeam()), MyState->GetCredits(), OtherState->GetCredits(), MyState->GetKills(), ShotsFired));
				Expect(GameState->GetLastRoundEndReason() == EValorRoundEndReason::Elimination, TEXT("전멸로 라운드 종료"));
				Expect(bIWon == !bHost, TEXT("클라 팀 승리(호스트 전멸)"));
				const TArray<AValorGameState::FKillFeedRecord>& KillFeed = GameState->GetKillFeed();
				const FString KillWeapon = KillFeed.Num() > 0 ? KillFeed.Last().WeaponDisplayName : FString();
				Test->AddInfo(FString::Printf(TEXT("MATCHNET %s knife: killWeapon=%s swings=%d backstab=%d hostHealthAfterFirstHit=%.0f"),
					bHost ? TEXT("Host") : TEXT("Client"), *KillWeapon, ShotsFired, bUsedBackstab ? 1 : 0, HealthAfterFirstHit));
				Expect(KillWeapon == TEXT("Knife"), TEXT("칼로 처치(킬피드 Knife)"));
				if (bHost)
				{
					Expect(HealthAfterFirstHit == 50.0f, TEXT("앞 베기 50(서버 판정)"));
				}
				else
				{
					Expect(bUsedBackstab && ShotsFired == 2, TEXT("앞 베기 한 번 + 등 뒤 찌르기 한 번으로 처치"));
				}
				// 승자: 시작 + 처치 200 + 승리 3000. 패자: 시작 + 1900. 시작은 권총이 없으면 가장 싼 총 값(지금 850).
				bool bHasSidearms = false;
				int32 Cheapest = 0;
				for (const FValorShopWeaponEntry& Entry : GameState->GetShopWeapons())
				{
					bHasSidearms |= Entry.Category == EValorWeaponCategory::Sidearm;
					Cheapest = Cheapest > 0 ? FMath::Min(Cheapest, Entry.Cost) : Entry.Cost;
				}
				const int32 StartCredits = ValorMatchRules::ComputeEconomyResetCredits(800, bHasSidearms, Cheapest);
				Expect(MyState->GetCredits() == StartCredits + (bHost ? 1900 : 3200), TEXT("라운드 보상(승 3000 + 처치 200 / 패 1900)"));
				bReportedRound1 = true;
			}

			// 2라운드: 호스트(공격, 스파이크 소지)가 지점 A에서 실제 4 키로 설치하고, 클라(수비)를 스파이크 옆에 세운다.
			// 클라는 실제 4 키를 누르고 있어 해체한다(이동 예측 플래그 → 서버 스파이크). 해체 승(수비)과 설치 보너스를 확인한다.
			AValorSpike* Spike = AValorSpike::FindSpike(World);
			if (GameState->GetRoundNumber() == 2 && Other && Spike)
			{
				if (bHost && Phase == EValorRoundPhase::Combat)
				{
					if (!bPlantStarted && Spike->GetCarrier() == Me)
					{
						for (TActorIterator<AValorSpikeSite> It(World); It; ++It)
						{
							if (It->GetSiteName() == TEXT("A"))
							{
								Me->TeleportTo(It->GetActorLocation() + FVector(0.0f, 0.0f, 60.0f), Me->GetActorRotation());
							}
						}
						bPlantStarted = true;
						CreditsBeforePlant = MyState->GetCredits();
						PlantPressTime = Now + 1.0;
					}
					if (bPlantStarted && !bSpikeKeyDown && Now >= PlantPressTime)
					{
						Controller->InputKey(FInputKeyParams(EKeys::Four, IE_Pressed, 1.0, false));
						bSpikeKeyDown = true;
					}
				}

				if (bHost && Phase == EValorRoundPhase::SpikePlanted && !bPlacedDefuser)
				{
					if (bSpikeKeyDown)
					{
						Controller->InputKey(FInputKeyParams(EKeys::Four, IE_Released, 0.0, false));
						bSpikeKeyDown = false;
					}
					bPlacedDefuser = true;
					Test->AddInfo(FString::Printf(TEXT("MATCHNET Host planted: site=%s credits=%d (before %d)"), *Spike->GetPlantedSiteName().ToString(), MyState->GetCredits(), CreditsBeforePlant));
					Expect(MyState->GetCredits() == CreditsBeforePlant + 300, TEXT("설치 보너스 300"));
					Other->TeleportTo(Spike->GetActorLocation() + FVector(80.0f, 0.0f, 100.0f), FRotator::ZeroRotator);
				}

				if (!bHost && Phase == EValorRoundPhase::SpikePlanted && !bSpikeKeyDown && FVector::Dist2D(Me->GetActorLocation(), Spike->GetActorLocation()) < 140.0f)
				{
					Controller->InputKey(FInputKeyParams(EKeys::Four, IE_Pressed, 1.0, false));
					bSpikeKeyDown = true;
					DefuseCheckTime = Now + 2.0;
				}

				if (!bHost && bSpikeKeyDown && DefuseCheckTime > 0.0 && Now >= DefuseCheckTime)
				{
					DefuseCheckTime = -1.0;
					Test->AddInfo(FString::Printf(TEXT("MATCHNET Client defusing: progress=%.2f defuserIsMe=%d maxSpeed=%.0f"), Spike->GetDefuseProgress(),
						Spike->GetDefuser() == Me ? 1 : 0, Me->GetCharacterMovement()->GetMaxSpeed()));
					Expect(Spike->GetDefuser() == Me && Spike->GetDefuseProgress() > 1.0f, TEXT("4 키를 누르고 있으면 해체 진행(서버 진행이 복제됨)"));
					Expect(Me->GetCharacterMovement()->GetMaxSpeed() == 0.0f, TEXT("해체 중 이동 고정(클라 예측)"));
				}
			}

			if (GameState->GetRoundNumber() == 2 && (Phase == EValorRoundPhase::RoundEnd || Phase == EValorRoundPhase::MatchEnd))
			{
				if (bSpikeKeyDown)
				{
					Controller->InputKey(FInputKeyParams(EKeys::Four, IE_Released, 0.0, false));
					bSpikeKeyDown = false;
				}
				const bool bIWon = GameState->GetLastRoundWinner() == MyState->GetTeam();
				Test->AddInfo(FString::Printf(TEXT("MATCHNET %s round2 end: winner=%d reason=%d iWon=%d"), bHost ? TEXT("Host") : TEXT("Client"),
					static_cast<int32>(GameState->GetLastRoundWinner()), static_cast<int32>(GameState->GetLastRoundEndReason()), bIWon ? 1 : 0));
				Expect(GameState->GetLastRoundEndReason() == EValorRoundEndReason::SpikeDefused, TEXT("2라운드: 해체로 종료"));
				Expect(bIWon == !bHost, TEXT("2라운드: 수비(클라) 승리"));
				bReported = true;
				ReportTime = Now;
			}

			return false;
		}

	private:
		// 클라: 요원 선택 단계에 남은 요원 하나를 고르고(ServerSelectAgent) 확정한다(ServerLockAgent). 각각 결과가 복제돼 보이기까지 잰다.
		void MeasureAgentSelect(double Now, const AValorGameState& GameState, const AValorPlayerState& MyState, APlayerController* Controller)
		{
			AValorPlayerController* ValorController = Cast<AValorPlayerController>(Controller);
			if (!ValorController || GameState.GetRoundPhase() != EValorRoundPhase::AgentSelect || GameState.GetAgentRoster().Num() < 2)
			{
				return;
			}
			if (AgentSelectSeenTime < 0.0)
			{
				AgentSelectSeenTime = Now;
			}
			if (SelectSentTime < 0.0 && Now - AgentSelectSeenTime >= 0.5)
			{
				TargetAgent = GameState.GetAgentRoster()[1];
				SelectSentTime = Now;
				ValorController->ServerSelectAgent(TargetAgent);
			}
			if (SelectSentTime >= 0.0 && SelectLatency < 0.0 && MyState.GetAgentClass() == TargetAgent)
			{
				SelectLatency = Now - SelectSentTime;
				LockSentTime = Now;
				ValorController->ServerLockAgent();
			}
			if (LockSentTime >= 0.0 && LockLatency < 0.0 && MyState.IsAgentLocked())
			{
				LockLatency = Now - LockSentTime;
			}
		}

		// 클라: 구매 단계에 경량 실드를 사고 되팔기를 번갈아 한다(마지막은 되팔기라 크레딧은 처음과 같다). 크레딧이 바뀌어 보이기까지 잰다.
		void MeasureShopLatency(double Now, const AValorGameState& GameState, const AValorPlayerState& MyState, APlayerController* Controller)
		{
			AValorPlayerController* ValorController = Cast<AValorPlayerController>(Controller);
			if (!ValorController || BuyMeasureStartTime < 0.0 || Now < BuyMeasureStartTime)
			{
				return;
			}
			if (ShopSentTime >= 0.0)
			{
				if (MyState.GetCredits() != CreditsBeforeShop)
				{
					ShopLatencies.Add(Now - ShopSentTime);
					bShieldBought = !bShieldBought;
					ShopSentTime = -1.0;
					NextShopTime = Now + 0.15;
				}
				return;
			}
			// 6번(산 뒤 되팔기 세 번). 되팔기는 구매 단계에만 되므로, 실드를 산 상태가 아니면 남은 시간이 넉넉할 때만 새로 산다.
			const bool bCanStartCycle = bShieldBought || GameState.GetPhaseTimeRemaining() > 2.0f;
			if (ShopLatencies.Num() < 6 && Now >= NextShopTime && bCanStartCycle)
			{
				CreditsBeforeShop = MyState.GetCredits();
				ShopSentTime = Now;
				if (bShieldBought)
				{
					ValorController->ServerSellShield(0);
				}
				else
				{
					ValorController->ServerBuyShield(0);
				}
			}
		}

		void ReportLatency()
		{
			bReportedLatency = true;
			double Sum = 0.0;
			double Max = 0.0;
			for (const double Latency : ShopLatencies)
			{
				Sum += Latency;
				Max = FMath::Max(Max, Latency);
			}
			const double Average = ShopLatencies.Num() > 0 ? Sum / ShopLatencies.Num() : -1.0;
			Test->AddInfo(FString::Printf(TEXT("MATCHNET Client latency: select=%.3fs lock=%.3fs shop n=%d avg=%.3fs max=%.3fs shieldBought=%d"),
				SelectLatency, LockLatency, ShopLatencies.Num(), Average, Max, bShieldBought ? 1 : 0));
			// 같은 PC의 리슨 서버라 왕복 + 네트워크 틱 몇 번이면 된다. 0.25초를 넘으면 사람이 "한 박자 늦다"고 느낀다.
			Expect(SelectLatency >= 0.0 && SelectLatency < 0.25, TEXT("요원 선택이 0.25초 안에 보임"));
			Expect(LockLatency >= 0.0 && LockLatency < 0.25, TEXT("요원 확정이 0.25초 안에 보임"));
			Expect(ShopLatencies.Num() >= 2 && Max < 0.25, TEXT("구매·되팔기 크레딧이 0.25초 안에 보임"));
			Expect(!bShieldBought, TEXT("측정 뒤 실드를 되팔아 크레딧이 처음과 같음"));
		}

		void Expect(bool bCondition, const TCHAR* What)
		{
			if (!bCondition)
			{
				Test->AddError(FString::Printf(TEXT("MATCHNET %s 실패: %s"), bHost ? TEXT("Host") : TEXT("Client"), What));
			}
		}

		FAutomationTestBase* Test = nullptr;
		bool bHost = false;
		double StartTime = -1.0;
		double NextShotTime = 0.0;
		double ReportTime = 0.0;
		int32 ShotsFired = 0;
		bool bLoggedBuy = false;
		bool bPlacedClient = false;
		bool bReported = false;
		bool bReportedRound1 = false;
		double RoundEndSeenTime = -1.0;
		bool bPlantStarted = false;
		bool bSpikeKeyDown = false;
		bool bPlacedDefuser = false;
		double PlantPressTime = 0.0;
		double DefuseCheckTime = -1.0;
		int32 CreditsBeforePlant = 0;

		// 칼(1라운드).
		bool bKnifeRequested = false;
		bool bKnifeKeyDown = false;
		bool bUsedBackstab = false;
		bool bTurnedAway = false;
		float HealthAfterFirstHit = -1.0f;

		// 반응 시간 측정(클라).
		double AgentSelectSeenTime = -1.0;
		double SelectSentTime = -1.0;
		double LockSentTime = -1.0;
		double SelectLatency = -1.0;
		double LockLatency = -1.0;
		TSubclassOf<UValorAgentDefinition> TargetAgent;
		double BuyMeasureStartTime = -1.0;
		double ShopSentTime = -1.0;
		double NextShopTime = 0.0;
		int32 CreditsBeforeShop = 0;
		bool bShieldBought = false;
		bool bReportedLatency = false;
		TArray<double> ShopLatencies;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorMatchNetHostTool, "Valor.Tools.MatchNet.Host", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorMatchNetHostTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorMatchNetTool::FValorMatchNetCommand(this, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorMatchNetClientTool, "Valor.Tools.MatchNet.Client", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorMatchNetClientTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorMatchNetTool::FValorMatchNetCommand(this, false));
	return true;
}

#endif

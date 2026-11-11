#include "Components/SkeletalMeshComponent.h"
#include "Components/ValorCombatComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Player/ValorPlayerState.h"
#include "ValorCharacter.h"
#include "ValorGameState.h"
#include "Weapons/ValorWeaponBase.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 경기 모드 네트워크 확인(리슨 서버 + 클라이언트 두 프로세스)
// 두 사람이 서로 다른 팀·서로 다른 스폰에 서는지, 교전 중 한 팀이 전멸하면 라운드가 끝나는지(전멸 승), 처치 200 + 승리 3000 /
// 패배 1900 보상이 맞는지 본다. 호스트가 구매 단계에 둘에게 밴달을 주고, 교전이 시작되면 클라를 호스트 정면 3m에 세운다.
// 클라는 호스트 머리를 쏴 처치한다.
// 실행(호스트 먼저, 약 25초 뒤 클라):
//   UnrealEditor.exe Valor.uproject "/Game/Maps/ThridPersonExampleMap?listen?game=Match?PreMatchTime=45?BuyTime=6?RoundTime=40?RoundEndTime=4"
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
			}

			if (Phase == EValorRoundPhase::Combat && Other)
			{
				if (bHost && !bPlacedClient)
				{
					bPlacedClient = true;
					const FVector Forward = FRotator(0.0f, Me->GetActorRotation().Yaw, 0.0f).Vector();
					Other->TeleportTo(Me->GetActorLocation() + (Forward * 300.0f), (-Forward).Rotation());
				}

				// 클라: 호스트가 정면에 오면 머리를 겨눠 0.4초마다 한 발씩 쏜다.
				if (!bHost && Other->IsAlive() && FVector::Dist(Me->GetActorLocation(), Other->GetActorLocation()) < 400.0f)
				{
					FVector ViewLocation;
					FRotator ViewRotation;
					Me->GetActorEyesViewPoint(ViewLocation, ViewRotation);
					const FVector HeadTarget = Other->GetMesh()->GetSocketLocation(TEXT("head")) + FVector(0.0f, 0.0f, 5.0f);
					Controller->SetControlRotation((HeadTarget - ViewLocation).Rotation());
					if (Now >= NextShotTime)
					{
						NextShotTime = Now + 0.4;
						Me->HandleFireInputPressed();
						Me->HandleFireInputReleased();
						++ShotsFired;
					}
				}
			}

			if (Phase == EValorRoundPhase::RoundEnd || Phase == EValorRoundPhase::MatchEnd)
			{
				const bool bIWon = GameState->GetLastRoundWinner() == MyState->GetTeam();
				Test->AddInfo(FString::Printf(TEXT("MATCHNET %s round end: winner=%d reason=%d iWon=%d score me%d:other%d credits=%d otherCredits=%d kills=%d shots=%d"),
					bHost ? TEXT("Host") : TEXT("Client"), static_cast<int32>(GameState->GetLastRoundWinner()), static_cast<int32>(GameState->GetLastRoundEndReason()), bIWon ? 1 : 0,
					GameState->GetTeamScore(MyState->GetTeam()), GameState->GetTeamScore(OtherState->GetTeam()), MyState->GetCredits(), OtherState->GetCredits(), MyState->GetKills(), ShotsFired));
				Expect(GameState->GetLastRoundEndReason() == EValorRoundEndReason::Elimination, TEXT("전멸로 라운드 종료"));
				Expect(bIWon == !bHost, TEXT("클라 팀 승리(호스트 전멸)"));
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
				bReported = true;
				ReportTime = Now;
			}

			return false;
		}

	private:
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

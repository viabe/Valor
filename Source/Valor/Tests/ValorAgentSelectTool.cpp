#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
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
#include "World/ValorAbilityActor.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 요원 선택과 새 요원 확인(혼자 + 봇, 단독 실행 = 서버 권한)
// 접속 대기 중 봇 둘(아군 A, 적 B)을 넣고, 요원 선택 단계에서 실제 마우스로 카드를 골라 LOCK IN 한다.
//   아군 봇이 먼저 확정한 요원은 고를 수 없음, 모두 확정하면 바로 1라운드, 고른 요원의 스킬이 생김.
//   그다음 Cinder 화염탄으로 적 봇에 지속 피해, Rook 넓은 정찰(궁극기)로 적 표시를 확인한다.
// 저장 위치: Saved/AgentSelect. 로그는 SELECT로 시작한다.
// 실행:
//   UnrealEditor.exe Valor.uproject "/Game/Maps/ThridPersonExampleMap?game=Match?PreMatchTime=12?SelectTime=30?BuyTime=40?RoundTime=3?RoundEndTime=2"
//     -game -windowed -ResX=1280 -ResY=720 -ExecCmds="Automation RunTests Valor.Tools.AgentSelect; Quit"
// =====================================================================================================

namespace ValorAgentSelectTool
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

	// HUD 클릭 영역 위로 마우스를 옮긴다(영역 가운데). 찾지 못하면 false.
	bool MoveMouseToHitBox(APlayerController* Controller, const TCHAR* BoxName)
	{
		const AHUD* HUD = Controller->GetHUD();
		const FHUDHitBox* Box = HUD ? HUD->GetHitBoxWithName(BoxName) : nullptr;
		FViewport* Viewport = Controller->GetLocalPlayer() && Controller->GetLocalPlayer()->ViewportClient ? Controller->GetLocalPlayer()->ViewportClient->Viewport : nullptr;
		if (!Box || !Viewport)
		{
			return false;
		}

		const FIntPoint Size = Viewport->GetSizeXY();
		FVector2D Sum = FVector2D::ZeroVector;
		int32 Count = 0;
		for (int32 Y = 0; Y < Size.Y; Y += 6)
		{
			for (int32 X = 0; X < Size.X; X += 6)
			{
				if (Box->Contains(FVector2D(X, Y)))
				{
					Sum += FVector2D(X, Y);
					++Count;
				}
			}
		}
		if (Count == 0)
		{
			return false;
		}
		Viewport->SetMouse(FMath::RoundToInt(Sum.X / Count), FMath::RoundToInt(Sum.Y / Count));
		return true;
	}

	int32 RosterIndexOf(const AValorGameState& GameState, const TCHAR* AgentName)
	{
		const TArray<TSubclassOf<UValorAgentDefinition>>& Roster = GameState.GetAgentRoster();
		for (int32 Index = 0; Index < Roster.Num(); ++Index)
		{
			if (Roster[Index] && Roster[Index]->GetDefaultObject<UValorAgentDefinition>()->DisplayName.ToString() == AgentName)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	class FValorAgentSelectCommand : public IAutomationLatentCommand
	{
	public:
		explicit FValorAgentSelectCommand(FAutomationTestBase* InTest)
			: Test(InTest)
		{
			OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AgentSelect"));
			IFileManager::Get().MakeDirectory(*OutputDirectory, true);
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (StartTime < 0.0)
			{
				StartTime = Now;
			}
			if (Now - StartTime > 110.0)
			{
				Test->AddError(FString::Printf(TEXT("SELECT 110초 안에 끝나지 않았습니다(단계 %d)."), Step));
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
			if (!GameState || !PlayerState || !Character)
			{
				return false;
			}

			// 누른 키는 다음 업데이트에 뗀다.
			if (PendingRelease.IsValid())
			{
				Controller->InputKey(FInputKeyParams(PendingRelease, IE_Released, 0.0, false));
				PendingRelease = FKey();
			}
			auto Tap = [this, Controller](FKey Key) { Controller->InputKey(FInputKeyParams(Key, IE_Pressed, 1.0, false)); PendingRelease = Key; };

			AValorCharacter* Enemy = nullptr;
			const AValorPlayerState* TeammateBot = nullptr;
			for (TActorIterator<AValorCharacter> It(World); It; ++It)
			{
				if (*It != Character && It->IsAlive() && ValorTeams::GetTeamOf(*It) == EValorTeam::TeamB)
				{
					Enemy = *It;
				}
			}
			for (const APlayerState* Other : GameState->PlayerArray)
			{
				const AValorPlayerState* OtherState = Cast<AValorPlayerState>(Other);
				if (OtherState && OtherState != PlayerState && OtherState->GetTeam() == PlayerState->GetTeam())
				{
					TeammateBot = OtherState;
				}
			}

			switch (Step)
			{
			case 0:
				if (GameState->GetRoundPhase() != EValorRoundPhase::WaitingToStart)
				{
					return false;
				}
				Controller->ValorAddBot(TEXT("A"));
				Controller->ValorAddBot(TEXT("B"));
				Step = 1;
				return false;

			case 1:
				if (GameState->GetRoundPhase() != EValorRoundPhase::AgentSelect)
				{
					return false;
				}
				// 선택 창은 컨트롤러 틱에서 열리므로 한 프레임 뒤에 본다.
				Step = 100;
				Wait(0.3);
				return false;

			case 100:
			{
				const FString BotAgent = TeammateBot && TeammateBot->GetAgent() ? TeammateBot->GetAgent()->DisplayName.ToString() : TEXT("none");
				Test->AddInfo(FString::Printf(TEXT("SELECT phase: open=%d cursor=%d roster=%d teammateBot=%s locked=%d moveLocked=%d fire=%d"), Controller->IsAgentSelectOpen() ? 1 : 0,
					Controller->bShowMouseCursor ? 1 : 0, GameState->GetAgentRoster().Num(), *BotAgent, TeammateBot && TeammateBot->IsAgentLocked() ? 1 : 0,
					GameState->IsMovementLocked() ? 1 : 0, GameState->IsWeaponFireAllowed() ? 1 : 0));
				Expect(Controller->IsAgentSelectOpen() && Controller->bShowMouseCursor, TEXT("요원 선택 창이 열리고 커서가 보임"));
				Expect(GameState->GetAgentRoster().Num() == 4, TEXT("요원 4명"));
				Expect(TeammateBot && TeammateBot->IsAgentLocked(), TEXT("아군 봇은 바로 확정"));
				Expect(GameState->IsMovementLocked() && !GameState->IsWeaponFireAllowed(), TEXT("선택 중에는 이동·사격 불가"));
				TeammateAgentIndex = TeammateBot && TeammateBot->GetAgentClass() ? GameState->GetAgentRoster().IndexOfByKey(TeammateBot->GetAgentClass()) : INDEX_NONE;
				Step = 2;
				Wait(0.5);
				return false;
			}

			case 2:
			{
				Capture(TEXT("AgentSelect"));
				// 아군이 확정한 요원 카드는 클릭 영역이 없다(고를 수 없음).
				const FString TakenBox = FString::Printf(TEXT("SelectAgent_%d"), TeammateAgentIndex);
				Expect(TeammateAgentIndex != INDEX_NONE && !Controller->GetHUD()->GetHitBoxWithName(*TakenBox), TEXT("아군이 확정한 요원은 고를 수 없음"));
				const int32 CinderIndex = RosterIndexOf(*GameState, TEXT("Cinder"));
				const FString CinderBox = FString::Printf(TEXT("SelectAgent_%d"), CinderIndex);
				Expect(MoveMouseToHitBox(Controller, *CinderBox), TEXT("Cinder 카드 클릭 영역"));
				Step = 3;
				Wait(0.3);
				return false;
			}

			case 3:
				Tap(EKeys::LeftMouseButton);
				Step = 4;
				Wait(0.4);
				return false;

			case 4:
				Test->AddInfo(FString::Printf(TEXT("SELECT picked: %s locked=%d"), PlayerState->GetAgent() ? *PlayerState->GetAgent()->DisplayName.ToString() : TEXT("none"), PlayerState->IsAgentLocked() ? 1 : 0));
				Expect(PlayerState->GetAgent() && PlayerState->GetAgent()->DisplayName.ToString() == TEXT("Cinder") && !PlayerState->IsAgentLocked(), TEXT("클릭으로 Cinder 선택(아직 확정 전)"));
				Expect(MoveMouseToHitBox(Controller, TEXT("LockIn")), TEXT("LOCK IN 버튼"));
				Step = 5;
				Wait(0.3);
				return false;

			case 5:
				Tap(EKeys::LeftMouseButton);
				Step = 6;
				Wait(0.4);
				return false;

			case 6:
				// 마우스 위치가 HUD 클릭 판정에 반영되기 전에 눌렸을 수 있으니 확정될 때까지 세 번까지 다시 누른다.
				if (!PlayerState->IsAgentLocked() && LockAttempts < 3)
				{
					++LockAttempts;
					MoveMouseToHitBox(Controller, TEXT("LockIn"));
					Step = 5;
					Wait(0.5);
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("SELECT lock: locked=%d retries=%d"), PlayerState->IsAgentLocked() ? 1 : 0, LockAttempts));
				Expect(PlayerState->IsAgentLocked(), TEXT("LOCK IN으로 확정"));
				Step = 7;
				return false;

			case 7:
				if (GameState->GetRoundPhase() != EValorRoundPhase::BuyPhase || GameState->GetRoundNumber() != 1)
				{
					return false;
				}
				Step = 101;
				Wait(0.3);
				return false;

			case 101:
			{
				Test->AddInfo(FString::Printf(TEXT("SELECT round1: agent=%s selectOpen=%d cursor=%d"), PlayerState->GetAgent() ? *PlayerState->GetAgent()->DisplayName.ToString() : TEXT("none"),
					Controller->IsAgentSelectOpen() ? 1 : 0, Controller->bShowMouseCursor ? 1 : 0));
				Expect(PlayerState->GetAgent() && PlayerState->GetAgent()->DisplayName.ToString() == TEXT("Cinder"), TEXT("1라운드: 확정한 요원"));
				Expect(!Controller->IsAgentSelectOpen() && !Controller->bShowMouseCursor, TEXT("선택 창이 닫힘"));

				// Cinder 화염탄: 적 봇을 앞 5m에 세우고 발치로 던진다.
				PlayerState->SetCredits(9000);
				Controller->ServerBuyAbility(1);
				if (Enemy)
				{
					const FVector Forward = FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f).Vector();
					Enemy->TeleportTo(Character->GetActorLocation() + (Forward * 500.0f), (-Forward).Rotation());
				}
				Step = 8;
				Wait(0.5);
				return false;
			}

			case 8:
			{
				if (!Enemy)
				{
					Test->AddError(TEXT("SELECT 적 봇이 없습니다."));
					return true;
				}
				FVector Eye;
				FRotator Unused;
				Character->GetActorEyesViewPoint(Eye, Unused);
				const FVector Feet = Enemy->GetActorLocation() - FVector(0.0f, 0.0f, Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
				Controller->SetControlRotation((Feet - Eye).Rotation());
				EnemyHealthBefore = Enemy->GetAbilitySystemComponent()->GetNumericAttribute(UValorCombatAttributeSet::GetHealthAttribute());
				Tap(EKeys::Q);
				Step = 90;
				return false;
			}

			case 90:
			{
				// 화염탄이 튕기다 멈춘 자리에 화염 지대가 생기면 적 봇을 그 안에 세운다(튕긴 위치는 물리 결과라 테스트는 지대 피해만 본다).
				AValorAbilityActor* Zone = nullptr;
				for (TActorIterator<AValorAbilityActor> It(World); It; ++It)
				{
					if (It->GetClass()->GetName().Contains(TEXT("DamageZone")))
					{
						Zone = *It;
					}
				}
				if (!Zone)
				{
					return false;
				}
				if (Enemy)
				{
					Enemy->TeleportTo(Zone->GetActorLocation() + FVector(0.0f, 0.0f, Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.0f), Enemy->GetActorRotation());
				}
				Step = 9;
				Wait(1.5);
				return false;
			}

			case 9:
			{
				const float EnemyHealth = Enemy ? Enemy->GetAbilitySystemComponent()->GetNumericAttribute(UValorCombatAttributeSet::GetHealthAttribute()) : -1.0f;
				Test->AddInfo(FString::Printf(TEXT("SELECT firebomb: enemy health %.0f -> %.0f alive=%d Q charges=%d"), EnemyHealthBefore, EnemyHealth, Enemy && Enemy->IsAlive() ? 1 : 0, PlayerState->GetAbilityCharges(EValorAbilitySlot::Ability2)));
				for (TActorIterator<AValorAbilityActor> It(World); It; ++It)
				{
					Test->AddInfo(FString::Printf(TEXT("SELECT   ability actor %s at %s, %.0fcm from enemy"), *It->GetClass()->GetName(), *It->GetActorLocation().ToCompactString(),
						Enemy ? FVector::Dist(It->GetActorLocation(), Enemy->GetActorLocation()) : -1.0f));
				}
				Expect(Enemy && (EnemyHealth < EnemyHealthBefore || !Enemy->IsAlive()), TEXT("Cinder 화염탄: 적 봇에 지속 피해"));
				Capture(TEXT("Firebomb"));
				Controller->ValorAgent(TEXT("Rook"));
				Controller->ValorUltPoints(6);
				Step = 10;
				Wait(0.5);
				return false;
			}

			case 10:
				Tap(EKeys::X);
				Step = 11;
				Wait(0.5);
				return false;

			case 11:
				Test->AddInfo(FString::Printf(TEXT("SELECT watchtower: agent=%s enemyRevealed=%d ult=%d"), PlayerState->GetAgent() ? *PlayerState->GetAgent()->DisplayName.ToString() : TEXT("none"),
					Enemy && Enemy->IsRevealedTo(EValorTeam::TeamA) ? 1 : 0, PlayerState->GetUltimatePoints()));
				Expect(!Enemy || !Enemy->IsAlive() || Enemy->IsRevealedTo(EValorTeam::TeamA), TEXT("Rook 넓은 정찰: 적 표시"));
				Step = 12;
				return false;

			default:
				Test->AddInfo(FString::Printf(TEXT("SELECT 캡처 저장 위치: %s"), *OutputDirectory));
				return true;
			}
		}

	private:
		void Expect(bool bCondition, const TCHAR* What)
		{
			if (!bCondition)
			{
				Test->AddError(FString::Printf(TEXT("SELECT 실패: %s"), What));
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
		int32 TeammateAgentIndex = INDEX_NONE;
		int32 LockAttempts = 0;
		float EnemyHealthBefore = 0.0f;
		FKey PendingRelease;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorAgentSelectTool, "Valor.Tools.AgentSelect", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorAgentSelectTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorAgentSelectTool::FValorAgentSelectCommand(this));
	return true;
}

#endif

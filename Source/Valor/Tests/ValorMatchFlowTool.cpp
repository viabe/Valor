#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/ValorCharacterMovementComponent.h"
#include "Components/ValorCombatComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerInput.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Player/ValorPlayerController.h"
#include "Player/ValorPlayerState.h"
#include "UnrealClient.h"
#include "ValorCharacter.h"
#include "ValorGameState.h"
#include "Weapons/ValorWeaponBase.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 경기 흐름 확인(혼자, 단독 실행 = 서버 권한)
// 짧은 시간 설정으로 실제 경기 모드를 돌려 구매 단계(이동·사격 금지, 구매·되팔기·바꿔 사기), 교전, 시간 종료(수비 승),
// 라운드 보상(연패 1900), 다음 라운드로 가져가는 장비를 확인하고 HUD(구매 메뉴, 라운드 결과)를 PNG로 저장한다.
// 저장 위치: Saved/MatchFlow/*.png. 로그는 MATCH로 시작한다.
// 실행:
//   UnrealEditor.exe Valor.uproject "/Game/Maps/ThridPersonExampleMap?game=Match?PreMatchTime=15?BuyTime=6?RoundTime=4?RoundEndTime=3"
//     -game -windowed -ResX=1280 -ResY=720 -ExecCmds="Automation RunTests Valor.Tools.MatchFlow; Quit"
// =====================================================================================================

namespace ValorMatchFlowTool
{
	// 경제 초기화 라운드의 기대 시작 크레딧(게임 모드와 같은 규칙: 권총이 없으면 가장 싼 총 값까지).
	int32 ExpectedStartCredits(const AValorGameState& GameState)
	{
		bool bHasSidearms = false;
		int32 Cheapest = 0;
		for (const FValorShopWeaponEntry& Entry : GameState.GetShopWeapons())
		{
			bHasSidearms |= Entry.Category == EValorWeaponCategory::Sidearm;
			Cheapest = Cheapest > 0 ? FMath::Min(Cheapest, Entry.Cost) : Entry.Cost;
		}
		return ValorMatchRules::ComputeEconomyResetCredits(800, bHasSidearms, Cheapest);
	}

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

	class FValorMatchFlowCommand : public IAutomationLatentCommand
	{
	public:
		explicit FValorMatchFlowCommand(FAutomationTestBase* InTest)
			: Test(InTest)
		{
			OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MatchFlow"));
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
				Test->AddError(FString::Printf(TEXT("MATCH 90초 안에 끝나지 않았습니다(단계 %d)."), static_cast<int32>(Step)));
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

			if (!GameState->UsesRounds())
			{
				Test->AddError(TEXT("MATCH 경기 모드가 아닙니다(URL에 ?game=Match 필요)."));
				return true;
			}

			UValorCombatComponent* Combat = Character->GetCombatComponent();
			const UAbilitySystemComponent* AbilitySystem = Character->GetAbilitySystemComponent();
			auto PrimaryName = [Combat]() { const AValorWeaponBase* Weapon = Combat->GetWeaponInSlot(EValorWeaponSlot::Primary); return Weapon ? Weapon->GetClass()->GetName() : FString(TEXT("none")); };
			auto Shield = [AbilitySystem]() { return AbilitySystem->GetNumericAttribute(UValorCombatAttributeSet::GetShieldAttribute()); };
			auto FindWeapon = [GameState](const TCHAR* Name) -> UClass*
			{
				const FValorShopWeaponEntry* Entry = GameState->GetShopWeapons().FindByPredicate([Name](const FValorShopWeaponEntry& Candidate) { return Candidate.WeaponClass->GetName().StartsWith(Name); });
				return Entry ? Entry->WeaponClass.Get() : nullptr;
			};

			switch (Step)
			{
			case EStep::WaitRound1Buy:
				if (GameState->GetRoundPhase() != EValorRoundPhase::BuyPhase || GameState->GetRoundNumber() != 1)
				{
					return false;
				}
				Step = EStep::Round1Checks;
				Wait(0.3);
				return false;

			case EStep::Round1Checks:
			{
				bool bHasSidearm = false;
				for (const FValorShopWeaponEntry& Entry : GameState->GetShopWeapons())
				{
					bHasSidearm |= Entry.Category == EValorWeaponCategory::Sidearm;
				}
				Test->AddInfo(FString::Printf(TEXT("MATCH round1 buy: team=%d side=%d credits=%d shopWeapons=%d sidearmInShop=%d moveLocked=%d maxSpeed=%.0f fireAllowed=%d"),
					static_cast<int32>(PlayerState->GetTeam()), static_cast<int32>(GameState->GetSideOfTeam(PlayerState->GetTeam())), PlayerState->GetCredits(),
					GameState->GetShopWeapons().Num(), bHasSidearm ? 1 : 0, GameState->IsMovementLocked() ? 1 : 0, Character->GetCharacterMovement()->GetMaxSpeed(), GameState->IsWeaponFireAllowed() ? 1 : 0));
				Test->AddInfo(FString::Printf(TEXT("MATCH debug: pawn=%s cmc=%s mode=%d"), *Character->GetClass()->GetPathName(), *Character->GetCharacterMovement()->GetClass()->GetName(), static_cast<int32>(Character->GetCharacterMovement()->MovementMode)));
				// 캐릭터 BP가 옛 이동 컴포넌트를 저장하고 있으면 -game에서 걷기·무기별 속도·구매 단계 잠금이 모두 빠진다(ValorRepairCharacterBlueprint 커맨드렛으로 고침).
				Expect(Character->GetCharacterMovement()->IsA<UValorCharacterMovementComponent>(), TEXT("캐릭터 이동 컴포넌트가 UValorCharacterMovementComponent"));
				Expect(PlayerState->GetTeam() == EValorTeam::TeamA, TEXT("첫 플레이어는 TeamA"));
				Expect(GameState->GetSideOfTeam(PlayerState->GetTeam()) == EValorTeamSide::Attack, TEXT("1라운드 TeamA 공격"));
				const int32 StartCredits = ExpectedStartCredits(*GameState);
				Test->AddInfo(FString::Printf(TEXT("MATCH start credits: %d (expected %d)"), PlayerState->GetCredits(), StartCredits));
				Expect(PlayerState->GetCredits() == StartCredits, TEXT("시작 크레딧(권총이 없으면 가장 싼 총 값)"));
				Expect(GameState->GetShopWeapons().ContainsByPredicate([StartCredits](const FValorShopWeaponEntry& Entry) { return Entry.Cost <= StartCredits; }), TEXT("1라운드에 총을 하나 이상 살 수 있음"));
				Expect(GameState->GetShopWeapons().Num() > 0 && !bHasSidearm, TEXT("상점에 총이 있고 권총은 없다"));
				Expect(!GameState->IsMovementLocked() && Character->GetCharacterMovement()->GetMaxSpeed() > 0.0f, TEXT("구매 단계에도 이동 가능(발로란트)"));
				Expect(GameState->IsWeaponFireAllowed(), TEXT("구매 단계에도 사격 가능(발로란트)"));

				// 실제 키 입력 경로: B(라운드 시작에 캐릭터가 새로 스폰된 뒤라 예전 입력 컨텍스트가 남아 있으면 여기서 막힌다).
				Controller->InputKey(FInputKeyParams(EKeys::B, IE_Pressed, 1.0, false));
				Step = EStep::ReleaseBOpen;
				Wait(0.1);
				return false;
			}

			case EStep::ReleaseBOpen:
				Controller->InputKey(FInputKeyParams(EKeys::B, IE_Released, 0.0, false));
				Step = EStep::AimAtShieldCard;
				Wait(0.4);
				return false;

			case EStep::AimAtShieldCard:
			{
				Test->AddInfo(FString::Printf(TEXT("MATCH key B: buyMenuOpen=%d cursor=%d"), Controller->IsBuyMenuOpen() ? 1 : 0, Controller->bShowMouseCursor ? 1 : 0));
				Expect(Controller->IsBuyMenuOpen(), TEXT("B 키로 구매 메뉴가 열림"));

				// 경량 실드 칸(HUD 클릭 영역) 위로 마우스를 옮긴다. 칸 좌표는 HUD가 매 프레임 정하므로 화면을 훑어 찾는다.
				const AHUD* HUD = Controller->GetHUD();
				const FHUDHitBox* ShieldCard = HUD ? HUD->GetHitBoxWithName(TEXT("BuyShield_0")) : nullptr;
				FViewport* Viewport = Controller->GetLocalPlayer() && Controller->GetLocalPlayer()->ViewportClient ? Controller->GetLocalPlayer()->ViewportClient->Viewport : nullptr;
				bool bFound = false;
				if (ShieldCard && Viewport)
				{
					const FIntPoint ViewSize = Viewport->GetSizeXY();
					FVector2D Sum = FVector2D::ZeroVector;
					int32 Count = 0;
					for (int32 Y = 0; Y < ViewSize.Y; Y += 6)
					{
						for (int32 X = 0; X < ViewSize.X; X += 6)
						{
							if (ShieldCard->Contains(FVector2D(X, Y)))
							{
								Sum += FVector2D(X, Y);
								++Count;
							}
						}
					}
					if (Count > 0)
					{
						const FVector2D Center = Sum / Count;
						Viewport->SetMouse(FMath::RoundToInt(Center.X), FMath::RoundToInt(Center.Y));
						bFound = true;
					}
				}
				Expect(bFound, TEXT("경량 실드 칸의 클릭 영역을 찾음"));
				Step = EStep::ClickShield;
				Wait(0.4);
				return false;
			}

			case EStep::ClickShield:
				AmmoBeforeClick = Combat->GetEquippedWeapon() ? Combat->GetEquippedWeapon()->GetCurrentMagazineAmmo() : -1;
				Controller->InputKey(FInputKeyParams(EKeys::LeftMouseButton, IE_Pressed, 1.0, false));
				Step = EStep::ReleaseClick;
				Wait(0.1);
				return false;

			case EStep::ReleaseClick:
				Controller->InputKey(FInputKeyParams(EKeys::LeftMouseButton, IE_Released, 0.0, false));
				Step = EStep::AfterClick;
				Wait(0.4);
				return false;

			case EStep::AfterClick:
			{
				float MouseX = -1.0f;
				float MouseY = -1.0f;
				Controller->GetMousePosition(MouseX, MouseY);
				Test->AddInfo(FString::Printf(TEXT("MATCH click light shield: mouse=(%.0f,%.0f) credits=%d shield=%.0f"), MouseX, MouseY, PlayerState->GetCredits(), Shield()));
				Expect(PlayerState->GetCredits() == ExpectedStartCredits(*GameState) - 400 && Shield() == 25.0f, TEXT("마우스 클릭으로 경량 실드 구매: -400, 실드 25"));
				Expect(AmmoBeforeClick < 0 || (Combat->GetEquippedWeapon() && Combat->GetEquippedWeapon()->GetCurrentMagazineAmmo() == AmmoBeforeClick), TEXT("메뉴 클릭은 총을 쏘지 않음"));

				// 실드: 경량 구매 → 중형은 돈 부족(경량 환불 400 + 남은 400 < 1000) → 경량 되팔기.
				Controller->ServerBuyShield(0);
				Expect(PlayerState->GetCredits() == ExpectedStartCredits(*GameState) - 400 && Shield() == 25.0f, TEXT("경량 실드 구매: -400, 실드 25"));
				Controller->ServerBuyShield(1);
				Expect(PlayerState->GetCredits() == ExpectedStartCredits(*GameState) - 400 && Shield() == 25.0f, TEXT("중형 실드는 크레딧 부족으로 거부"));
				Controller->ServerSellShield(0);
				Expect(PlayerState->GetCredits() == ExpectedStartCredits(*GameState) && Shield() == 0.0f, TEXT("경량 실드 되팔기: 시작 크레딧, 실드 0"));

				// 총: 크레딧을 올린 뒤 밴달 → 팬텀(밴달 자동 환불) → 팬텀 되팔기 → 다시 밴달 + 중형 실드(다음 라운드로 가져가기 확인용).
				UClass* Vandal = FindWeapon(TEXT("BP_Vandal"));
				UClass* Phantom = FindWeapon(TEXT("BP_Phantom"));
				Expect(Vandal && Phantom, TEXT("상점에 밴달·팬텀"));
				PlayerState->SetCredits(5000);
				Controller->ServerBuyWeapon(Vandal);
				Expect(PlayerState->GetCredits() == 2100 && PrimaryName().StartsWith(TEXT("BP_Vandal")), TEXT("밴달 구매: 2100"));
				Controller->ServerBuyWeapon(Phantom);
				Expect(PlayerState->GetCredits() == 2100 && PrimaryName().StartsWith(TEXT("BP_Phantom")), TEXT("팬텀으로 바꿔 사기: 밴달 자동 환불, 2100"));
				Controller->ServerSellWeapon(Phantom);
				Expect(PlayerState->GetCredits() == 5000 && PrimaryName() == TEXT("none"), TEXT("팬텀 되팔기: 5000, 주무기 없음"));
				Controller->ServerBuyWeapon(Vandal);
				Controller->ServerBuyShield(1);
				Expect(PlayerState->GetCredits() == 1100 && Shield() == 50.0f, TEXT("밴달 + 중형 실드: 1100, 실드 50"));
				Test->AddInfo(FString::Printf(TEXT("MATCH round1 after buys: credits=%d primary=%s shield=%.0f purchases=%d"), PlayerState->GetCredits(), *PrimaryName(), Shield(), PlayerState->GetRoundPurchases().Num()));

				Step = EStep::BuyMenuShot;
				Wait(0.5);
				return false;
			}

			case EStep::BuyMenuShot:
				Expect(Controller->IsBuyMenuOpen(), TEXT("구매 메뉴가 열려 있음"));
				Capture(TEXT("BuyMenu"));
				Controller->InputKey(FInputKeyParams(EKeys::B, IE_Pressed, 1.0, false));
				Step = EStep::CloseMenu;
				Wait(0.1);
				return false;

			case EStep::CloseMenu:
				Controller->InputKey(FInputKeyParams(EKeys::B, IE_Released, 0.0, false));
				Step = EStep::CheckMenuClosed;
				Wait(0.3);
				return false;

			case EStep::CheckMenuClosed:
				Expect(!Controller->IsBuyMenuOpen() && !Controller->bShowMouseCursor, TEXT("B 키로 구매 메뉴가 닫힘"));
				Step = EStep::WaitCombat;
				return false;

			case EStep::WaitCombat:
				if (GameState->GetRoundPhase() != EValorRoundPhase::Combat)
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("MATCH round1 combat: moveLocked=%d maxSpeed=%.0f fireAllowed=%d buyMenuOpen=%d"),
					GameState->IsMovementLocked() ? 1 : 0, Character->GetCharacterMovement()->GetMaxSpeed(), GameState->IsWeaponFireAllowed() ? 1 : 0, Controller->IsBuyMenuOpen() ? 1 : 0));
				Expect(!GameState->IsMovementLocked() && Character->GetCharacterMovement()->GetMaxSpeed() > 0.0f, TEXT("교전 이동 가능"));
				Expect(GameState->IsWeaponFireAllowed(), TEXT("교전 사격 가능"));
				Controller->SetBuyMenuOpen(true);
				Expect(!Controller->IsBuyMenuOpen(), TEXT("교전 중에는 구매 메뉴가 열리지 않음"));
				Step = EStep::WaitRoundEnd;
				return false;

			case EStep::WaitRoundEnd:
				if (GameState->GetRoundPhase() != EValorRoundPhase::RoundEnd)
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("MATCH round1 end: winner=%d reason=%d score A%d:B%d credits=%d"),
					static_cast<int32>(GameState->GetLastRoundWinner()), static_cast<int32>(GameState->GetLastRoundEndReason()),
					GameState->GetTeamScore(EValorTeam::TeamA), GameState->GetTeamScore(EValorTeam::TeamB), PlayerState->GetCredits()));
				Expect(GameState->GetLastRoundWinner() == EValorTeam::TeamB && GameState->GetLastRoundEndReason() == EValorRoundEndReason::TimeExpired, TEXT("시간 종료 → 수비(TeamB) 승"));
				Expect(GameState->GetTeamScore(EValorTeam::TeamB) == 1 && GameState->GetTeamScore(EValorTeam::TeamA) == 0, TEXT("점수 0:1"));
				Expect(PlayerState->GetCredits() == 3000, TEXT("패배 보상 1900: 1100 → 3000"));
				Step = EStep::RoundEndShot;
				Wait(0.3);
				return false;

			case EStep::RoundEndShot:
				Capture(TEXT("RoundEnd"));
				Step = EStep::WaitRound2Buy;
				return false;

			case EStep::WaitRound2Buy:
				if (GameState->GetRoundPhase() != EValorRoundPhase::BuyPhase || GameState->GetRoundNumber() != 2)
				{
					return false;
				}
				Step = EStep::Round2Checks;
				Wait(0.3);
				return false;

			case EStep::Round2Checks:
				Test->AddInfo(FString::Printf(TEXT("MATCH round2 buy: credits=%d primary=%s shield=%.0f purchases=%d alive=%d"),
					PlayerState->GetCredits(), *PrimaryName(), Shield(), PlayerState->GetRoundPurchases().Num(), Character->IsAlive() ? 1 : 0));
				Expect(PrimaryName().StartsWith(TEXT("BP_Vandal")) && Shield() == 50.0f, TEXT("살아남은 장비(밴달, 실드 50)를 가져감"));
				Expect(PlayerState->GetRoundPurchases().Num() == 0, TEXT("새 라운드는 되팔기 목록이 비어 있음"));
				Expect(PlayerState->GetCredits() == 3000, TEXT("크레딧 3000 유지"));
				Capture(TEXT("Round2Buy"));
				Step = EStep::Done;
				Wait(0.5);
				return false;

			case EStep::Done:
			default:
				Test->AddInfo(FString::Printf(TEXT("MATCH 캡처 저장 위치: %s"), *OutputDirectory));
				return true;
			}
		}

	private:
		enum class EStep : uint8
		{
			WaitRound1Buy,
			Round1Checks,
			ReleaseBOpen,
			AimAtShieldCard,
			ClickShield,
			ReleaseClick,
			AfterClick,
			BuyMenuShot,
			CloseMenu,
			CheckMenuClosed,
			WaitCombat,
			WaitRoundEnd,
			RoundEndShot,
			WaitRound2Buy,
			Round2Checks,
			Done
		};

		void Expect(bool bCondition, const TCHAR* What)
		{
			if (!bCondition)
			{
				Test->AddError(FString::Printf(TEXT("MATCH 실패: %s"), What));
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
		EStep Step = EStep::WaitRound1Buy;
		int32 AmmoBeforeClick = -1;
		double StartTime = -1.0;
		double WaitUntil = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorMatchFlowTool, "Valor.Tools.MatchFlow", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorMatchFlowTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorMatchFlowTool::FValorMatchFlowCommand(this));
	return true;
}

#endif

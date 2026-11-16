#include "Components/CapsuleComponent.h"
#include "Components/ValorCombatComponent.h"
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
#include "World/ValorAbilityActor.h"
#include "World/ValorAbilityEffects.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 스킬 부품 확인(혼자 + 적 봇, 단독 실행 = 서버 권한)
// 샘플 요원 Flare(C 도약 / Q 섬광탄 / E 정찰 다트 / X 방벽)로 실제 키를 눌러 부품을 하나씩 확인한다:
//   정찰 다트 → 적 봇이 우리 팀에 표시됨, 섬광탄 → 정면으로 본 나는 눈부심, 도약 → 위로 뜸, 방벽 → 벽 생성·총에 맞으면 체력 감소,
//   다음 라운드 → 스킬 물체 모두 정리. 저장 위치: Saved/AbilityBlocks. 로그는 BLOCKS로 시작한다.
// 실행:
//   UnrealEditor.exe Valor.uproject "/Game/Maps/ThridPersonExampleMap?game=Match?PreMatchTime=12?SelectTime=1?BuyTime=40?RoundTime=3?RoundEndTime=2"
//     -game -windowed -ResX=1280 -ResY=720 -ExecCmds="Automation RunTests Valor.Tools.AbilityBlocks; Quit"
// =====================================================================================================

namespace ValorAbilityBlocksTool
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

	class FValorAbilityBlocksCommand : public IAutomationLatentCommand
	{
	public:
		explicit FValorAbilityBlocksCommand(FAutomationTestBase* InTest)
			: Test(InTest)
		{
			OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AbilityBlocks"));
			IFileManager::Get().MakeDirectory(*OutputDirectory, true);
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (StartTime < 0.0)
			{
				StartTime = Now;
			}
			if (Now - StartTime > 100.0)
			{
				Test->AddError(FString::Printf(TEXT("BLOCKS 100초 안에 끝나지 않았습니다(단계 %d)."), Step));
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

			AValorCharacter* Enemy = nullptr;
			for (TActorIterator<AValorCharacter> It(World); It; ++It)
			{
				if (*It != Character && It->IsAlive() && ValorTeams::GetTeamOf(*It) == EValorTeam::TeamB)
				{
					Enemy = *It;
				}
			}
			AValorAbilityWall* Wall = nullptr;
			for (TActorIterator<AValorAbilityWall> It(World); It; ++It)
			{
				Wall = *It;
			}
			auto Tap = [this, Controller](FKey Key) { Controller->InputKey(FInputKeyParams(Key, IE_Pressed, 1.0, false)); PendingRelease = Key; };
			auto AimAt = [Controller, Character](const FVector& Target)
			{
				FVector Eye;
				FRotator Unused;
				Character->GetActorEyesViewPoint(Eye, Unused);
				Controller->SetControlRotation((Target - Eye).Rotation());
			};

			// 누른 키는 다음 업데이트에 뗀다(같은 프레임에 누르고 떼면 입력 시스템이 누름을 못 본다).
			if (PendingRelease.IsValid())
			{
				Controller->InputKey(FInputKeyParams(PendingRelease, IE_Released, 0.0, false));
				PendingRelease = FKey();
			}

			switch (Step)
			{
			case 0:
				if (GameState->GetRoundPhase() != EValorRoundPhase::BuyPhase || GameState->GetRoundNumber() != 1)
				{
					return false;
				}
				Controller->ValorAgent(TEXT("Flare"));
				Controller->ValorAddBot(TEXT("B"));
				PlayerState->SetCredits(9000);
				Step = 1;
				Wait(0.3);
				return false;

			case 1:
			{
				Controller->ServerBuyAbility(0);
				Controller->ServerBuyAbility(0);
				Controller->ServerBuyAbility(1);
				Controller->ServerBuyAbility(1);
				Test->AddInfo(FString::Printf(TEXT("BLOCKS agent=%s C=%d Q=%d E=%d enemy=%s"), PlayerState->GetAgent() ? *PlayerState->GetAgent()->DisplayName.ToString() : TEXT("none"),
					PlayerState->GetAbilityCharges(EValorAbilitySlot::Ability1), PlayerState->GetAbilityCharges(EValorAbilitySlot::Ability2),
					PlayerState->GetAbilityCharges(EValorAbilitySlot::Signature), *GetNameSafe(Enemy)));
				Expect(PlayerState->GetAgent() && PlayerState->GetAgent()->DisplayName.ToString() == TEXT("Flare"), TEXT("요원을 Flare로 바꿈"));
				Expect(Enemy != nullptr, TEXT("적 봇"));
				if (!Enemy)
				{
					return true;
				}

				// 적 봇을 내 앞 8m에 세우고, 정찰 다트를 봇 발치로 던진다.
				const FVector Forward = FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f).Vector();
				Enemy->TeleportTo(Character->GetActorLocation() + (Forward * 800.0f), (-Forward).Rotation());
				Step = 2;
				Wait(0.5);
				return false;
			}

			case 2:
				AimAt(Enemy->GetActorLocation() - FVector(0.0f, 0.0f, Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 20.0f) - (FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f).Vector() * 150.0f));
				Tap(EKeys::E);
				Step = 3;
				Wait(1.5);
				return false;

			case 3:
				Test->AddInfo(FString::Printf(TEXT("BLOCKS recon: enemyRevealedToA=%d E charges=%d"), Enemy && Enemy->IsRevealedTo(EValorTeam::TeamA) ? 1 : 0, PlayerState->GetAbilityCharges(EValorAbilitySlot::Signature)));
				Expect(Enemy && Enemy->IsRevealedTo(EValorTeam::TeamA), TEXT("정찰 다트: 적이 우리 팀 화면에 표시됨"));
				Capture(TEXT("Recon"));
				// 섬광탄: 정면(수평)으로 던진다 → 0.6초 뒤 내 앞에서 터진다.
				Controller->SetControlRotation(FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f));
				Tap(EKeys::Q);
				Step = 4;
				Wait(0.9);
				return false;

			case 4:
				Test->AddInfo(FString::Printf(TEXT("BLOCKS flash: myFlashAlpha=%.2f Q charges=%d"), Controller->GetFlashAlpha(), PlayerState->GetAbilityCharges(EValorAbilitySlot::Ability2)));
				Expect(Controller->GetFlashAlpha() > 0.9f, TEXT("섬광탄: 정면으로 본 나는 눈부심"));
				Capture(TEXT("Flash"));
				Step = 5;
				Wait(2.0);
				return false;

			case 5:
				Expect(Controller->GetFlashAlpha() == 0.0f, TEXT("눈부심이 걷힘"));
				UpdraftStartZ = Character->GetActorLocation().Z;
				Tap(EKeys::C);
				Step = 6;
				Wait(0.35);
				return false;

			case 6:
			{
				const float Rise = Character->GetActorLocation().Z - UpdraftStartZ;
				Test->AddInfo(FString::Printf(TEXT("BLOCKS updraft: rise=%.0fcm C charges=%d"), Rise, PlayerState->GetAbilityCharges(EValorAbilitySlot::Ability1)));
				Expect(Rise > 150.0f, TEXT("도약: 위로 뜸"));
				Controller->ValorUltPoints(6);
				Step = 7;
				Wait(2.0);
				return false;
			}

			case 7:
				Controller->SetControlRotation(FRotator(-10.0f, Controller->GetControlRotation().Yaw, 0.0f));
				Tap(EKeys::X);
				Step = 8;
				Wait(0.4);
				return false;

			case 8:
				Expect(Wall != nullptr && PlayerState->GetUltimatePoints() == 0, TEXT("방벽(궁극기): 벽 생성, 포인트 0"));
				if (!Wall)
				{
					return true;
				}
				WallHealthBefore = Wall->GetHealth();
				if (UClass* Vandal = LoadObject<UClass>(nullptr, TEXT("/Game/Valor/Gun/BP_Vandal.BP_Vandal_C")))
				{
					Character->GetCombatComponent()->GiveWeapon(Vandal, true, true);
				}
				// 적 봇이 벽 앞에 서 있으므로 벽 가운데에서 옆으로 1.8m 비켜 쏜다(봇에 막힌 탄은 벽에 닿지 않는다).
				AimAt(Wall->GetActorLocation() + (Wall->GetActorRightVector() * 180.0f));
				Step = 9;
				Wait(0.3);
				return false;

			case 9:
			case 10:
			case 11:
				Character->HandleFireInputPressed();
				Character->HandleFireInputReleased();
				++Step;
				Wait(0.35);
				return false;

			case 12:
				Test->AddInfo(FString::Printf(TEXT("BLOCKS wall: health %.0f -> %.0f"), WallHealthBefore, Wall ? Wall->GetHealth() : -1.0f));
				Expect(Wall && Wall->GetHealth() < WallHealthBefore, TEXT("방벽: 총에 맞으면 체력이 깎임"));
				Capture(TEXT("Wall"));
				Controller->ValorSkipPhase();
				Step = 13;
				return false;

			case 13:
				if (GameState->GetRoundNumber() != 2 || GameState->GetRoundPhase() != EValorRoundPhase::BuyPhase)
				{
					return false;
				}
				{
					int32 Remaining = 0;
					for (TActorIterator<AValorAbilityActor> It(World); It; ++It)
					{
						++Remaining;
					}
					Test->AddInfo(FString::Printf(TEXT("BLOCKS round2: abilityActorsLeft=%d"), Remaining));
					Expect(Remaining == 0, TEXT("다음 라운드: 스킬 물체 모두 정리"));
				}
				Step = 14;
				return false;

			default:
				Test->AddInfo(FString::Printf(TEXT("BLOCKS 캡처 저장 위치: %s"), *OutputDirectory));
				return true;
			}
		}

	private:
		void Expect(bool bCondition, const TCHAR* What)
		{
			if (!bCondition)
			{
				Test->AddError(FString::Printf(TEXT("BLOCKS 실패: %s"), What));
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
		float UpdraftStartZ = 0.0f;
		float WallHealthBefore = 0.0f;
		FKey PendingRelease;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorAbilityBlocksTool, "Valor.Tools.AbilityBlocks", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorAbilityBlocksTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorAbilityBlocksTool::FValorAbilityBlocksCommand(this));
	return true;
}

#endif

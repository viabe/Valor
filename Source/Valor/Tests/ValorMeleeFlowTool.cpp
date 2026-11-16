#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystemComponent.h"
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

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 칼 공격 확인(혼자 + 적 봇, 단독 실행 = 서버 권한)
// 실제 키로 확인한다: 3 키로 칼 꺼내기 → 좌클릭을 누르고 있으면 0.5초마다 계속 벤다(3연속 번호 0·1·2) →
//   2m 밖은 안 맞음 → 앞 베기 50 → 앞 찌르기 75 → 등 뒤 베기 100 → 등 뒤 찌르기 150으로 처치, 킬피드 "Knife".
// 적 봇의 체력·실드는 단계마다 도구가 다시 채운다. 저장 위치: Saved/MeleeFlow. 로그는 MELEE로 시작한다.
// 실행:
//   UnrealEditor.exe Valor.uproject "/Game/Maps/ThridPersonExampleMap?game=Match?PreMatchTime=12?SelectTime=1?BuyTime=40?RoundTime=3?RoundEndTime=2"
//     -game -windowed -ResX=1280 -ResY=720 -ExecCmds="Automation RunTests Valor.Tools.MeleeFlow; Quit"
// =====================================================================================================

namespace ValorMeleeFlowTool
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

	void SetVitals(AValorCharacter& Target, float Health, float Shield)
	{
		UAbilitySystemComponent* AbilitySystem = Target.GetAbilitySystemComponent();
		if (!AbilitySystem)
		{
			return;
		}
		AbilitySystem->SetNumericAttributeBase(UValorCombatAttributeSet::GetMaxShieldAttribute(), FMath::Max(50.0f, Shield));
		AbilitySystem->SetNumericAttributeBase(UValorCombatAttributeSet::GetShieldAttribute(), Shield);
		AbilitySystem->SetNumericAttributeBase(UValorCombatAttributeSet::GetHealthAttribute(), Health);
	}

	float GetTotalVitals(const AValorCharacter& Target)
	{
		const UValorCombatAttributeSet* Attributes = Target.GetCombatAttributeSet();
		return Attributes ? Attributes->GetHealth() + Attributes->GetShield() : -1.0f;
	}

	class FValorMeleeFlowCommand : public IAutomationLatentCommand
	{
	public:
		explicit FValorMeleeFlowCommand(FAutomationTestBase* InTest)
			: Test(InTest)
		{
			OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MeleeFlow"));
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
				Test->AddError(FString::Printf(TEXT("MELEE 90초 안에 끝나지 않았습니다(단계 %d)."), Step));
				return true;
			}

			UWorld* World = FindGameWorld();
			AValorGameState* GameState = World ? World->GetGameState<AValorGameState>() : nullptr;
			AValorPlayerController* Controller = World ? Cast<AValorPlayerController>(GEngine->GetFirstLocalPlayerController(World)) : nullptr;
			AValorPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AValorPlayerState>() : nullptr;
			AValorCharacter* Character = Controller ? Cast<AValorCharacter>(Controller->GetPawn()) : nullptr;
			UValorCombatComponent* Combat = Character ? Character->GetCombatComponent() : nullptr;
			if (!GameState || !PlayerState || !Character || !Combat)
			{
				return false;
			}

			// 휘두르기 횟수: 로컬 연출 시각이 바뀔 때마다 1회(키를 누르고 있는 동안 연속 공격 확인).
			if (Combat->GetLastMeleeAttackWorldTime() != LastSeenSwingTime)
			{
				LastSeenSwingTime = Combat->GetLastMeleeAttackWorldTime();
				++SwingCount;
				ComboIndicesSeen.Add(Combat->GetMeleeComboIndex());
			}

			// 누른 키는 다음 업데이트에 뗀다(같은 프레임에 누르고 떼면 입력 시스템이 누름을 못 본다).
			if (PendingRelease.IsValid() && Now >= ReleaseAt)
			{
				Controller->InputKey(FInputKeyParams(PendingRelease, IE_Released, 0.0, false));
				PendingRelease = FKey();
			}
			if (Now < WaitUntil)
			{
				return false;
			}

			AValorCharacter* Enemy = nullptr;
			for (TActorIterator<AValorCharacter> It(World); It; ++It)
			{
				if (*It != Character && ValorTeams::GetTeamOf(*It) == EValorTeam::TeamB)
				{
					Enemy = *It;
				}
			}

			auto Press = [this, Controller, Now](FKey Key, double HoldSeconds)
			{
				Controller->InputKey(FInputKeyParams(Key, IE_Pressed, 1.0, false));
				PendingRelease = Key;
				ReleaseAt = Now + HoldSeconds;
			};
			// 적을 내 앞 Distance(cm)에 세운다. bFaceAway = 나와 같은 방향을 보게(등을 보임), 아니면 나를 보게.
			auto PlaceEnemy = [this, Character, Controller, &Enemy](float Distance, bool bFaceAway)
			{
				const FVector EnemyLocation = Character->GetActorLocation() + (Forward * Distance);
				const FRotator EnemyRotation = (bFaceAway ? Forward : -Forward).Rotation();
				Enemy->TeleportTo(EnemyLocation, EnemyRotation);
				if (AController* EnemyController = Enemy->GetController())
				{
					EnemyController->SetControlRotation(EnemyRotation);
				}
				FVector Eye;
				FRotator Unused;
				Character->GetActorEyesViewPoint(Eye, Unused);
				Controller->SetControlRotation(((EnemyLocation + FVector(0.0f, 0.0f, 30.0f)) - Eye).Rotation());
			};

			switch (Step)
			{
			case 0:
				if (GameState->GetRoundPhase() != EValorRoundPhase::BuyPhase || GameState->GetRoundNumber() != 1)
				{
					return false;
				}
				Controller->ValorAddBot(TEXT("B"));
				Step = 1;
				Wait(0.5);
				return false;

			case 1:
				Expect(Enemy != nullptr, TEXT("적 봇"));
				if (!Enemy)
				{
					return true;
				}
				Forward = FRotator(0.0f, Character->GetActorRotation().Yaw, 0.0f).Vector();
				PlaceEnemy(700.0f, false);
				Press(EKeys::Three, 0.0);
				Step = 2;
				Wait(0.9);
				return false;

			case 2:
				Test->AddInfo(FString::Printf(TEXT("MELEE equip: slot=%d ready=%d"), static_cast<int32>(Combat->GetActiveSlot()), Combat->IsEquipReady() ? 1 : 0));
				Expect(Combat->GetActiveSlot() == EValorWeaponSlot::Melee && Combat->IsMeleeActive(), TEXT("3 키로 칼을 꺼냄"));
				Expect(Combat->IsEquipReady(), TEXT("칼 장착 0.6초 뒤 준비"));
				SwingCount = 0;
				ComboIndicesSeen.Reset();
				Press(EKeys::LeftMouseButton, 1.2);
				Step = 3;
				Wait(1.6);
				return false;

			case 3:
			{
				FString Combos;
				for (const int32 Index : ComboIndicesSeen)
				{
					Combos += FString::Printf(TEXT("%d "), Index);
				}
				Test->AddInfo(FString::Printf(TEXT("MELEE hold 1.2s: swings=%d combos=[ %s]"), SwingCount, *Combos));
				Expect(SwingCount == 3, TEXT("좌클릭을 1.2초 누르면 0.5초마다 3번 벤다"));
				Expect(ComboIndicesSeen.Num() == 3 && ComboIndicesSeen[0] == 0 && ComboIndicesSeen[1] == 1 && ComboIndicesSeen[2] == 2, TEXT("연속 베기 번호 0·1·2"));
				SetVitals(*Enemy, 100.0f, 0.0f);
				PlaceEnemy(300.0f, false);
				Step = 4;
				Wait(0.4);
				return false;
			}

			case 4:
				Press(EKeys::LeftMouseButton, 0.0);
				Step = 5;
				Wait(0.4);
				return false;

			case 5:
				Test->AddInfo(FString::Printf(TEXT("MELEE 3m: vitals=%.0f probeTarget=%s"), GetTotalVitals(*Enemy), *GetNameSafe(Combat->GetLastLocalMeleeProbe().Target)));
				Expect(GetTotalVitals(*Enemy) == 100.0f, TEXT("2m 밖(3m)은 안 맞음"));
				// 판정 도형 기록이 새 위치를 담도록 옮긴 뒤 잠깐 기다렸다 친다(서버는 공격 시각으로 되감아 본다).
				PlaceEnemy(130.0f, false);
				Step = 6;
				Wait(0.4);
				return false;

			case 6:
				Press(EKeys::LeftMouseButton, 0.0);
				Step = 7;
				Wait(0.4);
				return false;

			case 7:
				Test->AddInfo(FString::Printf(TEXT("MELEE front slash: vitals=%.0f backstab=%d ring=%d"), GetTotalVitals(*Enemy),
					Combat->GetLastLocalMeleeProbe().bBackstab ? 1 : 0, Combat->GetLastLocalMeleeProbe().Ring));
				Expect(GetTotalVitals(*Enemy) == 50.0f, TEXT("앞 베기 50"));
				Capture(TEXT("FrontSlash"));
				SetVitals(*Enemy, 100.0f, 0.0f);
				Step = 8;
				Wait(0.5);
				return false;

			case 8:
				Press(EKeys::RightMouseButton, 0.0);
				Step = 9;
				Wait(0.4);
				return false;

			case 9:
				Test->AddInfo(FString::Printf(TEXT("MELEE front stab: vitals=%.0f"), GetTotalVitals(*Enemy)));
				Expect(GetTotalVitals(*Enemy) == 25.0f, TEXT("앞 찌르기 75"));
				SetVitals(*Enemy, 100.0f, 50.0f);
				PlaceEnemy(130.0f, true);
				// 찌르기 회복 1초가 지나야 다음 공격이 나간다.
				Step = 10;
				Wait(1.0);
				return false;

			case 10:
				Press(EKeys::LeftMouseButton, 0.0);
				Step = 11;
				Wait(0.4);
				return false;

			case 11:
				Test->AddInfo(FString::Printf(TEXT("MELEE back slash: vitals=%.0f backstab=%d"), GetTotalVitals(*Enemy), Combat->GetLastLocalMeleeProbe().bBackstab ? 1 : 0));
				Expect(Combat->GetLastLocalMeleeProbe().bBackstab, TEXT("등 뒤에서 치면 뒤치기"));
				Expect(GetTotalVitals(*Enemy) == 50.0f, TEXT("등 뒤 베기 100(실드 50 + 체력 100 → 50)"));
				SetVitals(*Enemy, 100.0f, 50.0f);
				Step = 12;
				Wait(0.6);
				return false;

			case 12:
				Press(EKeys::RightMouseButton, 0.0);
				Step = 13;
				Wait(0.5);
				return false;

			case 13:
			{
				const TArray<AValorGameState::FKillFeedRecord>& KillFeed = GameState->GetKillFeed();
				const FString WeaponName = KillFeed.Num() > 0 ? KillFeed.Last().WeaponDisplayName : FString();
				const FString KillerName = KillFeed.Num() > 0 ? KillFeed.Last().Entry.KillerName : FString();
				Test->AddInfo(FString::Printf(TEXT("MELEE back stab: enemyAlive=%d killFeed=%d weapon=%s killer=%s me=%s"), Enemy->IsAlive() ? 1 : 0,
					KillFeed.Num(), *WeaponName, *KillerName, *PlayerState->GetPlayerName()));
				Expect(!Enemy->IsAlive(), TEXT("등 뒤 찌르기 150 = 실드가 있어도 한 방"));
				Expect(WeaponName == TEXT("Knife") && KillerName == PlayerState->GetPlayerName(), TEXT("킬피드에 내 이름과 Knife"));
				Capture(TEXT("Backstab"));
				Step = 14;
				Wait(0.5);
				return false;
			}

			default:
				return true;
			}
		}

	private:
		void Expect(bool bCondition, const TCHAR* What)
		{
			if (!bCondition)
			{
				Test->AddError(FString::Printf(TEXT("MELEE 실패: %s"), What));
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
		FKey PendingRelease;
		double ReleaseAt = 0.0;
		FVector Forward = FVector::ForwardVector;
		float LastSeenSwingTime = -1000.0f;
		int32 SwingCount = 0;
		TArray<int32> ComboIndicesSeen;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorMeleeFlowTool, "Valor.Tools.MeleeFlow", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorMeleeFlowTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorMeleeFlowTool::FValorMeleeFlowCommand(this));
	return true;
}

#endif

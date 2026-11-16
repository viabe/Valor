#include "Components/ValorCharacterMovementComponent.h"
#include "Components/ValorCombatComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Player/ValorPlayerController.h"
#include "ValorCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 피격 감속(태깅) 네트워크 확인(리슨 서버 + 클라이언트 두 프로세스, 데스매치)
// 감속은 서버가 정하고 소유 클라가 이동 예측에 넣는다. 둘이 어긋나면 클라가 뒤로 끌려간다(위치 보정). 이 도구는 그것을 직접 센다.
//  1회차(정상): 호스트가 클라를 자기 앞 4m에 세우고, 클라는 호스트 반대쪽으로 달린다. 다 달리는 속도가 되면 호스트가 몸을 한 발 쏜다.
//     클라: 감속 상태가 복제된 뒤 속도가 27.5% 근처까지 떨어졌다가 회복하는지, 그동안 위치 보정이 0번인지.
//  2회차(조작 흉내): 클라가 Valor.Debug.IgnoreTagPrediction 1로 감속을 무시하고 호스트 쪽으로 달린다. 호스트가 다시 쏜다.
//     호스트(서버): 클라 주장을 잘라 서버 쪽 속도가 느려지는지. 클라: 보정을 받는지.
//  3회차(개발 명령): 클라가 ValorTagMe 0.8(0.8초마다 서버가 나에게 감속)을 켜고 2초 달린다. 감속이 여러 번 걸리고 보정이 0번인지.
// 맵: 호스트 자리에서 8방향 중 가장 긴 빈 길(가슴 높이)을 고른다. 로그는 TAGNET으로 시작한다.
// 실행(호스트 먼저, 약 25초 뒤 클라):
//   UnrealEditor.exe Valor.uproject "/Game/Maps/ThridPersonExampleMap?listen?game=DM" -game -ExecCmds="Automation RunTests Valor.Tools.TagNet.Host; Quit"
//   UnrealEditor.exe Valor.uproject 127.0.0.1 -game -ExecCmds="Automation RunTests Valor.Tools.TagNet.Client; Quit"
// =====================================================================================================

namespace ValorTagNetTool
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

	float GetFullRunSpeed(const AValorCharacter& Character)
	{
		const UValorCharacterMovementComponent* Movement = Character.GetValorMovement();
		const UValorCombatComponent* Combat = Character.GetCombatComponent();
		return Movement && Combat ? Movement->GetBaseRunSpeed() * Combat->GetEquipMoveSpeedMultiplier() : 0.0f;
	}

	class FValorTagNetCommand : public IAutomationLatentCommand
	{
	public:
		FValorTagNetCommand(FAutomationTestBase* InTest, bool bInHost)
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
				Test->AddError(FString::Printf(TEXT("TAGNET %s 150초 안에 끝나지 않았습니다(단계 %d)."), bHost ? TEXT("Host") : TEXT("Client"), Step));
				SetIgnorePrediction(false);
				return true;
			}

			// 호스트는 두 번 다 잰 뒤 클라가 결과를 남기고 나갈 시간만 주고 끝낸다(클라가 나가면 아래 상대 찾기에서 멈추므로 먼저 본다).
			if (bHost && Step >= 5)
			{
				return Now - ReportTime > 8.0;
			}

			UWorld* World = FindGameWorld();
			APlayerController* Controller = World ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
			AValorCharacter* Me = Controller ? Cast<AValorCharacter>(Controller->GetPawn()) : nullptr;
			AValorCharacter* Other = nullptr;
			if (World)
			{
				for (TActorIterator<AValorCharacter> It(World); It; ++It)
				{
					if (*It != Me && It->IsAlive())
					{
						Other = *It;
					}
				}
			}
			if (!Me || !Other || !Me->IsAlive())
			{
				return false;
			}

			return bHost ? UpdateHost(Now, *World, *Controller, *Me, *Other) : UpdateClient(Now, *Controller, *Me, *Other);
		}

	private:
		// ===== 호스트(서버) =====
		bool UpdateHost(double Now, UWorld& World, APlayerController& Controller, AValorCharacter& Me, AValorCharacter& Other)
		{
			if (Now < WaitUntil)
			{
				return false;
			}
			const float OtherSpeed = Other.GetVelocity().Size2D();
			const float OtherFullSpeed = GetFullRunSpeed(Other);

			switch (Step)
			{
			case 0:
			{
				// 가장 긴 빈 길을 골라 클라를 그 방향 4m에 세운다.
				FVector Eye;
				FRotator Unused;
				Me.GetActorEyesViewPoint(Eye, Unused);
				FCollisionQueryParams Params(SCENE_QUERY_STAT(ValorTagNetLane), false, &Me);
				Params.AddIgnoredActor(&Other);
				float BestLength = -1.0f;
				for (int32 Index = 0; Index < 8; ++Index)
				{
					const FVector Direction = FRotator(0.0f, Index * 45.0f, 0.0f).Vector();
					FHitResult Hit;
					const float Length = World.LineTraceSingleByChannel(Hit, Eye, Eye + (Direction * 3000.0f), ECC_Visibility, Params) ? Hit.Distance : 3000.0f;
					if (Length > BestLength)
					{
						BestLength = Length;
						LaneDirection = Direction;
					}
				}
				Other.TeleportTo(Me.GetActorLocation() + (LaneDirection * 400.0f), LaneDirection.Rotation());
				Controller.SetControlRotation(LaneDirection.Rotation());
				Test->AddInfo(FString::Printf(TEXT("TAGNET Host lane: length=%.0fcm"), BestLength));
				Step = 1;
				return false;
			}

			case 1:
			case 3:
				// 클라가 다 달리는 속도로 0.25초 달리면 쏜다(1회차: 멀어짐, 2회차: 다가옴).
				FastSince = (OtherFullSpeed > 0.0f && OtherSpeed > OtherFullSpeed * 0.85f) ? (FastSince < 0.0 ? Now : FastSince) : -1.0;
				if (FastSince >= 0.0 && Now - FastSince >= 0.25)
				{
					FVector Eye;
					FRotator Unused;
					Me.GetActorEyesViewPoint(Eye, Unused);
					Controller.SetControlRotation(((Other.GetActorLocation() + FVector(0.0f, 0.0f, 25.0f)) - Eye).Rotation());
					Me.HandleFireInputPressed();
					Me.HandleFireInputReleased();
					ShotTime = Now;
					SpeedBeforeShot = OtherSpeed;
					MinSpeedAfterShot = OtherSpeed;
					Step += 1;
				}
				return false;

			case 2:
			case 4:
			{
				MinSpeedAfterShot = FMath::Min(MinSpeedAfterShot, OtherSpeed);
				if (Now - ShotTime < 0.8)
				{
					return false;
				}
				const bool bCheatRun = Step == 4;
				Test->AddInfo(FString::Printf(TEXT("TAGNET Host run%d (server view): speedBefore=%.0f minAfterShot=%.0f (%.0f%% of full %.0f) speedNow=%.0f"),
					bCheatRun ? 2 : 1, SpeedBeforeShot, MinSpeedAfterShot, OtherFullSpeed > 0.0f ? 100.0f * MinSpeedAfterShot / OtherFullSpeed : -1.0f, OtherFullSpeed, OtherSpeed));
				Expect(MinSpeedAfterShot < OtherFullSpeed * 0.5f, bCheatRun ? TEXT("감속을 무시한 클라도 서버에서는 느려짐(검증)") : TEXT("맞은 클라가 서버에서 느려짐"));
				if (bCheatRun)
				{
					ReportTime = Now;
					Step = 5;
				}
				else
				{
					Step = 3;
					FastSince = -1.0;
					// 클라가 멈췄다가 다시 달리기 시작할 때까지 기다린다(빠른 속도 0.25초 조건이 1회차 끝과 섞이지 않게 잠깐 쉰다).
					WaitUntil = Now + 1.0;
				}
				return false;
			}

			default:
				return true;
			}
		}

		// ===== 클라 =====
		bool UpdateClient(double Now, APlayerController& Controller, AValorCharacter& Me, AValorCharacter& Other)
		{
			const UValorCharacterMovementComponent* Movement = Me.GetValorMovement();
			if (!Movement || Now < WaitUntil)
			{
				return false;
			}
			const float Speed = Me.GetVelocity().Size2D();
			const float FullSpeed = GetFullRunSpeed(Me);

			switch (Step)
			{
			case 0:
				// 호스트가 나를 길 위(호스트 앞 4m)로 옮길 때까지 기다린다.
				if (FVector::Dist2D(Me.GetActorLocation(), Other.GetActorLocation()) > 450.0f)
				{
					return false;
				}
				Step = 1;
				WaitUntil = Now + 1.0;
				return false;

			case 1:
			case 4:
			{
				// 달리기 시작: 1회차는 호스트 반대쪽, 2회차는 호스트 쪽.
				const bool bCheatRun = Step == 4;
				RunDirection = (Me.GetActorLocation() - Other.GetActorLocation()).GetSafeNormal2D() * (bCheatRun ? -1.0f : 1.0f);
				Controller.SetControlRotation(RunDirection.Rotation());
				SetIgnorePrediction(bCheatRun);
				CorrectionsAtStart = Movement->GetClientCorrectionCount();
				TagStartAtRun = Me.GetTagState().StartServerTime;
				RunStartTime = Now;
				TagSeenTime = -1.0;
				MinSpeedAfterTag = TNumericLimits<float>::Max();
				SpeedBeforeTag = 0.0f;
				Step += 1;
				return false;
			}

			case 2:
			case 5:
			{
				const bool bCheatRun = Step == 5;
				Me.AddMovementInput(RunDirection, 1.0f);
				if (TagSeenTime < 0.0 && Me.GetTagState().StartServerTime != TagStartAtRun)
				{
					TagSeenTime = Now;
					SpeedBeforeTag = Speed;
				}
				if (TagSeenTime >= 0.0)
				{
					MinSpeedAfterTag = FMath::Min(MinSpeedAfterTag, Speed);
				}

				const bool bDone = (TagSeenTime >= 0.0 && Now - TagSeenTime >= 0.75) || Now - RunStartTime > 4.0;
				if (!bDone)
				{
					return false;
				}

				const int32 Corrections = Movement->GetClientCorrectionCount() - CorrectionsAtStart;
				Test->AddInfo(FString::Printf(TEXT("TAGNET Client run%d%s: tagSeen=%d speedBefore=%.0f minAfterTag=%.0f (%.0f%% of full %.0f) speedNow=%.0f corrections=%d"),
					bCheatRun ? 2 : 1, bCheatRun ? TEXT("(ignore tag)") : TEXT(""), TagSeenTime >= 0.0 ? 1 : 0, SpeedBeforeTag,
					TagSeenTime >= 0.0 ? MinSpeedAfterTag : -1.0f, FullSpeed > 0.0f && TagSeenTime >= 0.0 ? 100.0f * MinSpeedAfterTag / FullSpeed : -1.0f, FullSpeed, Speed, Corrections));
				Expect(TagSeenTime >= 0.0, TEXT("감속 상태가 클라에 복제됨"));
				if (!bCheatRun)
				{
					Expect(MinSpeedAfterTag <= FullSpeed * 0.35f, TEXT("맞은 뒤 27.5% 근처까지 느려짐(예측)"));
					Expect(Speed >= FullSpeed * 0.9f, TEXT("0.75초 뒤 다시 다 달림"));
					Expect(Corrections == 0, TEXT("감속 동안 위치 보정 0번(서버와 예측이 같음)"));
					Step = 3;
					WaitUntil = Now + 1.5;
				}
				else
				{
					Expect(Corrections > 0, TEXT("감속을 무시하면 서버가 보정함"));
					SetIgnorePrediction(false);
					Step = 6;
					WaitUntil = Now + 1.5;
				}
				return false;
			}

			case 3:
				Step = 4;
				return false;

			case 6:
			{
				// 3회차: 개발 명령으로 서버가 반복해서 감속을 건다. 호스트 반대쪽으로 2초 달린다.
				AValorPlayerController* ValorController = Cast<AValorPlayerController>(&Controller);
				Expect(ValorController != nullptr, TEXT("Valor 플레이어 컨트롤러"));
				if (!ValorController)
				{
					return true;
				}
				ValorController->ValorTagMe(TEXT("0.8"), FString());
				RunDirection = (Me.GetActorLocation() - Other.GetActorLocation()).GetSafeNormal2D();
				Controller.SetControlRotation(RunDirection.Rotation());
				CorrectionsAtStart = Movement->GetClientCorrectionCount();
				LoopSequenceSeen = Me.GetTagState().Sequence;
				LoopTagsSeen = 0;
				MinSpeedAfterTag = TNumericLimits<float>::Max();
				RunStartTime = Now;
				Step = 7;
				return false;
			}

			case 7:
			{
				Me.AddMovementInput(RunDirection, 1.0f);
				if (Me.GetTagState().Sequence != LoopSequenceSeen)
				{
					LoopSequenceSeen = Me.GetTagState().Sequence;
					++LoopTagsSeen;
				}
				if (Now - RunStartTime > 0.3)
				{
					MinSpeedAfterTag = FMath::Min(MinSpeedAfterTag, Speed);
				}
				if (Now - RunStartTime < 2.0)
				{
					return false;
				}
				if (AValorPlayerController* ValorController = Cast<AValorPlayerController>(&Controller))
				{
					ValorController->ValorTagMe(TEXT("off"), FString());
				}
				const int32 Corrections = Movement->GetClientCorrectionCount() - CorrectionsAtStart;
				Test->AddInfo(FString::Printf(TEXT("TAGNET Client run3(ValorTagMe 0.8): tags=%d minSpeed=%.0f (%.0f%% of full %.0f) corrections=%d"),
					LoopTagsSeen, MinSpeedAfterTag, FullSpeed > 0.0f ? 100.0f * MinSpeedAfterTag / FullSpeed : -1.0f, FullSpeed, Corrections));
				Expect(LoopTagsSeen >= 2, TEXT("ValorTagMe: 2초 동안 감속이 여러 번 걸림"));
				Expect(MinSpeedAfterTag <= FullSpeed * 0.35f, TEXT("ValorTagMe: 27.5% 근처까지 느려짐"));
				Expect(Corrections == 0, TEXT("ValorTagMe: 위치 보정 0번"));
				Step = 8;
				return false;
			}

			default:
				return true;
			}
		}

		void SetIgnorePrediction(bool bIgnore)
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Valor.Debug.IgnoreTagPrediction")))
			{
				Variable->Set(bIgnore ? 1 : 0, ECVF_SetByCode);
			}
		}

		void Expect(bool bCondition, const TCHAR* What)
		{
			if (!bCondition)
			{
				Test->AddError(FString::Printf(TEXT("TAGNET %s 실패: %s"), bHost ? TEXT("Host") : TEXT("Client"), What));
			}
		}

		FAutomationTestBase* Test = nullptr;
		bool bHost = false;
		int32 Step = 0;
		double StartTime = -1.0;
		double WaitUntil = 0.0;
		double ReportTime = 0.0;

		// 호스트.
		FVector LaneDirection = FVector::ForwardVector;
		double FastSince = -1.0;
		double ShotTime = 0.0;
		float SpeedBeforeShot = 0.0f;
		float MinSpeedAfterShot = 0.0f;

		// 클라.
		FVector RunDirection = FVector::ForwardVector;
		int32 CorrectionsAtStart = 0;
		float TagStartAtRun = 0.0f;
		double RunStartTime = 0.0;
		double TagSeenTime = -1.0;
		float SpeedBeforeTag = 0.0f;
		float MinSpeedAfterTag = 0.0f;
		uint8 LoopSequenceSeen = 0;
		int32 LoopTagsSeen = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorTagNetHostTool, "Valor.Tools.TagNet.Host", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorTagNetHostTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorTagNetTool::FValorTagNetCommand(this, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorTagNetClientTool, "Valor.Tools.TagNet.Client", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorTagNetClientTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorTagNetTool::FValorTagNetCommand(this, false));
	return true;
}

#endif

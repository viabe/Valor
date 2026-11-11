#include "Components/SkeletalMeshComponent.h"
#include "Components/ValorCombatComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Player/ValorPlayerController.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#include "ValorCharacter.h"
#include "Weapons/ValorWeaponBase.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 3인칭 조준 정렬 네트워크 확인
// PIE와 같은 상황(리슨 서버 + 클라이언트, 서로 머리를 겨눔)을 프로세스 두 개로 재현해 "상대 화면에서 내 총구가 상대 눈을 향하나"를 잰다.
// 단독 실행(ViewmodelCapture)의 더미는 복제를 거치지 않으므로, 복제된 피치·시뮬레이티드 프록시 경로는 이 도구로만 확인된다.
// 호스트가 두 캐릭터에게 밴달을 주고 클라 캐릭터를 내 앞 DistanceCm 거리로 옮긴다. 양쪽은 매 프레임 상대의 보이는 머리를 겨눈다(플레이 테스트와 같게).
// 각 프로세스는 "상대 총"을 잰다: 총구와 내 카메라 사이 각도, 총구 선이 내 카메라를 비켜 가는 거리(위/오른쪽), 총의 기울기(cant).
// 저장 위치: Saved/ThirdPersonAimNet/Host.png, Client.png. 수치는 각 프로세스 로그의 "TPNET" 줄.
// 실행(Git Bash는 MSYS_NO_PATHCONV=1, 호스트를 먼저 띄운다):
//   UnrealEditor.exe Valor.uproject /Game/Maps/ThridPersonExampleMap?listen -game -windowed -ResX=960 -ResY=540
//     -ExecCmds="Automation RunTests Valor.Tools.ThirdPersonAimNet.Host; Quit"
//   UnrealEditor.exe Valor.uproject 127.0.0.1 -game -windowed -ResX=960 -ResY=540
//     -ExecCmds="Automation RunTests Valor.Tools.ThirdPersonAimNet.Client; Quit"
// =====================================================================================================

// 유니티 빌드에서 다른 파일의 이름(DistanceCm 등)과 겹치지 않도록 이름 있는 네임스페이스에 둔다.
namespace ValorAimNetTool
{
	constexpr float DistanceCm = 300.0f;
	// 클라 프로세스는 접속 후 약 10초 뒤에야 테스트(겨누기)를 시작한다. 호스트는 그 뒤에 재야 클라의 실제 조준을 본다.
	constexpr double ClientMeasureStartSeconds = 8.0;
	constexpr double HostMeasureStartSeconds = 22.0;
	constexpr double MeasureDurationSeconds = 3.0;
	// 클라가 몸통을 겨누기 시작하는 시각. 호스트 측정이 끝난 뒤여야 한다(클라는 호스트보다 늦게 짝을 찾으므로 충분하다).
	constexpr double FireAimSeconds = HostMeasureStartSeconds + MeasureDurationSeconds + 1.0;

	UWorld* FindGameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}

		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if (WorldContext.WorldType == EWorldType::Game && WorldContext.World())
			{
				return WorldContext.World();
			}
		}

		return nullptr;
	}

	FVector GetViewLocation(const AValorCharacter& Character)
	{
		FVector Location;
		FRotator Rotation;
		Character.GetWeaponViewPoint(Location, Rotation);
		return Location;
	}

	// 사람이 겨누는 곳 = 3인칭 메시의 보이는 머리 중심(head 뼈는 목·두개골 경계라 조금 위). 그 머신의 메시 자세 기준.
	FVector GetVisibleHeadLocation(const AValorCharacter& Character)
	{
		const USkeletalMeshComponent* Mesh = Character.GetMesh();
		return (Mesh && Mesh->DoesSocketExist(TEXT("head")))
			? Mesh->GetSocketLocation(TEXT("head")) + FVector(0.0f, 0.0f, 9.0f)
			: GetViewLocation(Character);
	}

	struct FAimNetStats
	{
		int32 Count = 0;
		float AngleSum = 0.0f;
		float AngleMax = 0.0f;
		float BaseAngleSum = 0.0f;
		FVector2D MissSum = FVector2D::ZeroVector;
		float CantSum = 0.0f;
		float AimErrorSum = 0.0f;
		float EyeHeightDiffSum = 0.0f;
		float HeadToEyeSum = 0.0f;
		int32 TickEnabledCount = 0;
	};

	class FValorThirdPersonAimNetCommand : public IAutomationLatentCommand
	{
	public:
		FValorThirdPersonAimNetCommand(FAutomationTestBase* InTest, bool bInHost)
			: Test(InTest)
			, bHost(bInHost)
		{
			OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ThirdPersonAimNet"));
			IFileManager::Get().MakeDirectory(*OutputDirectory, true);
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (StartTime < 0.0)
			{
				StartTime = Now;
			}

			UWorld* World = FindGameWorld();
			APlayerController* PlayerController = World ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
			AValorCharacter* Local = PlayerController ? Cast<AValorCharacter>(PlayerController->GetPawn()) : nullptr;
			AValorCharacter* Other = nullptr;
			if (World && Local)
			{
				for (TActorIterator<AValorCharacter> It(World); It; ++It)
				{
					if (*It != Local && It->IsAlive())
					{
						Other = *It;
						break;
					}
				}
			}

			// 마지막 탄(머리)은 상대를 처치하므로, 상대가 살아 있는 목록에서 빠진 뒤에도 그 탄의 명중 확인 결과는 끝까지 본다.
			if (Local && !Other && !bHost && FireWorldTime >= 0.0)
			{
				return UpdateFireCheck(*Local, Now - PairFoundTime);
			}

			if (!Local || !Other)
			{
				// 다 잰 뒤 상대 프로세스가 먼저 끝나면 그대로 끝낸다.
				if (bReported)
				{
					return true;
				}

				if (Now - StartTime > 120.0)
				{
					Test->AddError(TEXT("120초 안에 두 캐릭터를 찾지 못했습니다(호스트를 ?listen으로 먼저 띄웠는지 확인)."));
					return true;
				}
				return false;
			}

			if (PairFoundTime < 0.0)
			{
				PairFoundTime = Now;
				if (GShaderCompilingManager)
				{
					GShaderCompilingManager->FinishAllCompilation();
				}

				if (bHost)
				{
					// 서버 권한: 두 캐릭터에 같은 총을 주고, 클라 캐릭터를 내 정면 DistanceCm에 세운다.
					if (UClass* WeaponClass = LoadObject<UClass>(nullptr, TEXT("/Game/Valor/Gun/BP_Vandal.BP_Vandal_C")))
					{
						Local->GetCombatComponent()->GiveWeapon(WeaponClass, true, true);
						Other->GetCombatComponent()->GiveWeapon(WeaponClass, true, true);
					}

					const FVector Forward = FRotator(0.0f, Local->GetActorRotation().Yaw, 0.0f).Vector();
					Other->TeleportTo(Local->GetActorLocation() + (Forward * DistanceCm), (-Forward).Rotation());
				}
			}

			const double Elapsed = Now - PairFoundTime;

			// 상대의 보이는 머리를 겨눈다. 컨트롤 회전은 이동 패킷으로 서버에 가고, 서버가 다시 복제 피치로 다른 머신에 보낸다.
			// 클라는 호스트가 다 잰 뒤 두 발을 쏜다(예측 탄착·서버 명중 확인·부위 확인): 몸통 중심, 그다음 보이는 머리 윗부분.
			const bool bFirePhase = !bHost && Elapsed >= FireAimSeconds;
			const FVector MyView = GetViewLocation(*Local);
			const FVector AimTarget = bFirePhase ? GetFireTarget(*Other) : GetVisibleHeadLocation(*Other);
			PlayerController->SetControlRotation((AimTarget - MyView).Rotation());

			if (bFirePhase)
			{
				return UpdateFireCheck(*Local, Elapsed);
			}

			const double MeasureStart = bHost ? HostMeasureStartSeconds : ClientMeasureStartSeconds;
			const double MeasureEnd = MeasureStart + MeasureDurationSeconds;
			if (Elapsed >= MeasureStart && Elapsed < MeasureEnd && Now >= NextSampleTime)
			{
				Sample(*Local, *Other, MyView);
				NextSampleTime = Now + 0.2;
			}

			if (Elapsed >= MeasureEnd && !bReported)
			{
				bReported = true;
				FScreenshotRequest::RequestScreenshot(OutputDirectory / (bHost ? TEXT("Host.png") : TEXT("Client.png")), true, false);
				Report(*Local, *Other);
			}

			// 호스트는 클라가 사격 확인까지 마치고 나갈 때까지 기다린다(상대가 사라지면 위에서 끝난다).
			return bHost && Elapsed >= 90.0;
		}

	private:
		// 사격 확인 순서. 몸통(40 피해) 뒤 머리(160 피해, 실드 포함 150을 넘어 처치)라 머리는 마지막이어야 한다.
		// 머리 윗부분(보이는 머리 중심 + 6cm)은 뼈 판정 전 고정 상자에서 빗나가던 곳이다.
		struct FFireShot
		{
			const TCHAR* Label;
			EValorHitZone ExpectedZone;
		};
		static constexpr int32 NumFireShots = 2;
		static const FFireShot& GetFireShot(int32 Index)
		{
			static const FFireShot Shots[NumFireShots] = {{TEXT("body"), EValorHitZone::Body}, {TEXT("head-top"), EValorHitZone::Head}};
			return Shots[Index];
		}

		FVector GetFireTarget(const AValorCharacter& Other) const
		{
			return FireShotIndex == 0 ? Other.GetActorLocation() + FVector(0.0f, 0.0f, 20.0f) : GetVisibleHeadLocation(Other) + FVector(0.0f, 0.0f, 6.0f);
		}

		// 클라: 겨눈 뒤 0.5초 기다려(반동 회복·조준 복제) 한 발 쏘고, 예측 탄착이 캐릭터에 맞았는지, 서버 명중 확인(히트마커)이 왔는지,
		// 서버가 정한 부위가 기대와 같은지 본다.
		bool UpdateFireCheck(AValorCharacter& Local, double Elapsed)
		{
			UWorld* World = Local.GetWorld();
			const AValorPlayerController* ValorController = Cast<AValorPlayerController>(Local.GetController());
			if (FireWorldTime < 0.0)
			{
				if (Elapsed < FireAimSeconds + 0.5 + (FireShotIndex * 1.5))
				{
					return false;
				}

				FireWorldTime = World->GetTimeSeconds();
				Local.HandleFireInputPressed();
				Local.HandleFireInputReleased();
				return false;
			}

			const bool bConfirmed = ValorController && ValorController->GetLastHitConfirmTime() >= FireWorldTime;
			if (!bConfirmed && World->GetTimeSeconds() - FireWorldTime < 1.0)
			{
				return false;
			}

			const FFireShot& Shot = GetFireShot(FireShotIndex);
			const FValorShotEffects& Predicted = Local.GetCombatComponent()->GetLastLocalShotEffects();
			const bool bPredictedHit = Predicted.Impacts.Num() > 0 && Predicted.Impacts[0].bHitCharacter;
			const EValorHitZone Zone = bConfirmed ? ValorController->GetLastHitConfirm().HitZone : EValorHitZone::None;
			Test->AddInfo(FString::Printf(TEXT("TPNET Client fire %s: predictedHitCharacter=%d serverConfirm=%d zone=%d(expected %d) damage=%.0f"),
				Shot.Label, bPredictedHit ? 1 : 0, bConfirmed ? 1 : 0, static_cast<int32>(Zone), static_cast<int32>(Shot.ExpectedZone),
				bConfirmed ? ValorController->GetLastHitConfirm().Damage : 0.0f));
			if (!bPredictedHit || !bConfirmed || Zone != Shot.ExpectedZone)
			{
				Test->AddError(FString::Printf(TEXT("TPNET Client fire %s: 예측 탄착, 서버 명중 확인, 부위 중 하나가 기대와 다릅니다."), Shot.Label));
			}

			FireWorldTime = -1.0;
			return ++FireShotIndex >= NumFireShots;
		}

		void Sample(const AValorCharacter& Local, const AValorCharacter& Other, const FVector& MyView)
		{
			const AValorWeaponBase* OtherWeapon = Other.GetCombatComponent() ? Other.GetCombatComponent()->GetEquippedWeapon() : nullptr;
			const USceneComponent* ItemRoot = OtherWeapon ? OtherWeapon->GetRootComponent() : nullptr;
			const USceneComponent* Hand = ItemRoot ? ItemRoot->GetAttachParent() : nullptr;
			if (!ItemRoot || !Hand)
			{
				return;
			}

			const FVector Grip = ItemRoot->GetComponentLocation();
			const FQuat WeaponRotation = ItemRoot->GetComponentQuat();
			const FVector Muzzle = WeaponRotation.RotateVector(FVector::UpVector);
			const FVector ToView = MyView - Grip;
			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Muzzle | ToView.GetSafeNormal(), -1.0f, 1.0f)));

			// 총구 선이 내 카메라를 비켜 가는 거리(내 화면 기준 위/오른쪽, cm).
			const FVector Closest = Grip + (Muzzle * (ToView | Muzzle));
			FVector MyLocation;
			FRotator MyRotation;
			Local.GetWeaponViewPoint(MyLocation, MyRotation);
			const FRotationMatrix MyFrame(MyRotation);
			const FVector Miss = Closest - MyView;

			// 손 자세만의 총구(보정 전).
			const FQuat HandRotation = Hand->GetSocketQuaternion(ItemRoot->GetAttachSocketName());
			const FVector BaseMuzzle = (HandRotation * Other.GetThirdPersonWeaponRotation()).RotateVector(FVector::UpVector);
			const float BaseAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(BaseMuzzle | ToView.GetSafeNormal(), -1.0f, 1.0f)));

			// 기울기: 총의 위(리그 -Y)가 "총구 기준 수직면"에서 얼마나 돌아가 있나. 양수 = 총구 방향으로 볼 때 시계 방향.
			const FVector GunUp = WeaponRotation.RotateVector(-FVector::RightVector);
			const FVector LevelUp = (FVector::UpVector - (Muzzle * (FVector::UpVector | Muzzle))).GetSafeNormal();
			const float Cant = FMath::RadiansToDegrees(FMath::Atan2((LevelUp ^ GunUp) | Muzzle, LevelUp | GunUp));

			// 상대의 (이 머신에 복제된) 조준 방향과 내 눈 사이 각도. 머리를 겨누므로 머리-눈 거리만큼은 원래 벌어진다.
			const FVector OtherEye = Other.GetPawnViewLocation();
			const float AimError = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Other.GetBaseAimRotation().Vector() | (MyView - OtherEye).GetSafeNormal(), -1.0f, 1.0f)));

			++Stats.Count;
			Stats.AngleSum += Angle;
			Stats.AngleMax = FMath::Max(Stats.AngleMax, Angle);
			Stats.BaseAngleSum += BaseAngle;
			Stats.MissSum += FVector2D(Miss | MyFrame.GetUnitAxis(EAxis::Z), Miss | MyFrame.GetUnitAxis(EAxis::Y));
			Stats.CantSum += Cant;
			Stats.AimErrorSum += AimError;
			Stats.EyeHeightDiffSum += GetViewLocation(Other).Z - OtherEye.Z;
			Stats.HeadToEyeSum += FVector::Dist(GetVisibleHeadLocation(Other), GetViewLocation(Other));
			Stats.TickEnabledCount += OtherWeapon->IsActorTickEnabled() ? 1 : 0;
		}

		void Report(const AValorCharacter& Local, const AValorCharacter& Other)
		{
			const TCHAR* Role = bHost ? TEXT("Host") : TEXT("Client");
			if (Stats.Count == 0)
			{
				Test->AddError(FString::Printf(TEXT("TPNET %s: 상대 총을 재지 못했습니다(상대가 총을 들고 있지 않음)."), Role));
				return;
			}

			const float N = static_cast<float>(Stats.Count);
			Test->AddInfo(FString::Printf(
				TEXT("TPNET %s samples=%d dist=%.0fcm angle=%.2fdeg(max %.2f) base=%.2fdeg miss(up=%.1fcm right=%.1fcm) cant=%.1fdeg aimVsMyEye=%.2fdeg otherHeadToEye=%.1fcm cameraMinusPawnEyeZ=%.1fcm tick=%d/%d otherRole=%d"),
				Role, Stats.Count, FVector::Dist(GetViewLocation(Local), GetViewLocation(Other)),
				Stats.AngleSum / N, Stats.AngleMax, Stats.BaseAngleSum / N,
				Stats.MissSum.X / N, Stats.MissSum.Y / N, Stats.CantSum / N, Stats.AimErrorSum / N, Stats.HeadToEyeSum / N, Stats.EyeHeightDiffSum / N,
				Stats.TickEnabledCount, Stats.Count, static_cast<int32>(Other.GetLocalRole())));
		}

		FAutomationTestBase* Test = nullptr;
		bool bHost = false;
		FString OutputDirectory;
		FAimNetStats Stats;
		double StartTime = -1.0;
		double PairFoundTime = -1.0;
		double NextSampleTime = 0.0;
		bool bReported = false;
		double FireWorldTime = -1.0;
		int32 FireShotIndex = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorThirdPersonAimNetHostTool, "Valor.Tools.ThirdPersonAimNet.Host", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorThirdPersonAimNetHostTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorAimNetTool::FValorThirdPersonAimNetCommand(this, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorThirdPersonAimNetClientTool, "Valor.Tools.ThirdPersonAimNet.Client", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorThirdPersonAimNetClientTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorAimNetTool::FValorThirdPersonAimNetCommand(this, false));
	return true;
}

#endif

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ValorCombatComponent.h"
#include "Components/ValorLagCompensationComponent.h"
#include "Components/ValorViewmodelComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#include "ValorCharacter.h"
#include "Weapons/ValorWeaponBase.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 판정 도형 캡처
// 실제 게임(-game, 단독 실행)에서 내 앞에 캐릭터를 세우고, 판정 도형(valor.DrawHitboxes)을 겹쳐 그린 화면을 PNG로 저장한다.
// 정면·측면·앉기 자세와 머리 확대 화면을 찍고, 수평 탄을 위에서 아래로 쏴 높이별 부위(H 머리, B 몸, L 다리, - 빗나감)를 로그로 남긴다.
// 저장 위치: Saved/HitboxCapture/*.png. 로그는 HITBOX로 시작한다.
// 실행(에디터를 닫을 필요 없음):
//   UnrealEditor.exe Valor.uproject /Game/Maps/ThridPersonExampleMap -game -windowed -ResX=1280 -ResY=720
//     -ExecCmds="Automation RunTests Valor.Tools.HitboxCapture; Quit"
// =====================================================================================================

namespace ValorHitboxTool
{
	AValorCharacter* FindLocalValorCharacter()
	{
		if (!GEngine)
		{
			return nullptr;
		}

		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			UWorld* World = WorldContext.World();
			if (!World || (WorldContext.WorldType != EWorldType::Game && WorldContext.WorldType != EWorldType::PIE))
			{
				continue;
			}

			if (APlayerController* PlayerController = GEngine->GetFirstLocalPlayerController(World))
			{
				return Cast<AValorCharacter>(PlayerController->GetPawn());
			}
		}

		return nullptr;
	}

	TCHAR ZoneLetter(EValorHitZone Zone)
	{
		switch (Zone)
		{
		case EValorHitZone::Head: return TEXT('H');
		case EValorHitZone::Body: return TEXT('B');
		case EValorHitZone::Leg: return TEXT('L');
		default: return TEXT('-');
		}
	}

	const TCHAR* ShapeTypeName(EValorHitShapeType Type)
	{
		switch (Type)
		{
		case EValorHitShapeType::Sphere: return TEXT("Sphere");
		case EValorHitShapeType::Capsule: return TEXT("Capsule");
		case EValorHitShapeType::Box: return TEXT("Box");
		default: return TEXT("?");
		}
	}

	void SetDrawHitboxes(int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("valor.DrawHitboxes")))
		{
			Variable->Set(Value, ECVF_SetByCode);
		}
	}

	class FValorHitboxCaptureCommand : public IAutomationLatentCommand
	{
	public:
		explicit FValorHitboxCaptureCommand(FAutomationTestBase* InTest)
			: Test(InTest)
		{
			OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("HitboxCapture"));
			IFileManager::Get().MakeDirectory(*OutputDirectory, true);
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (StartTime < 0.0)
			{
				StartTime = Now;
			}

			if (Now < WaitUntil)
			{
				return false;
			}

			AValorCharacter* Character = FindLocalValorCharacter();
			APlayerController* PlayerController = Character ? Character->GetController<APlayerController>() : nullptr;
			if (!PlayerController)
			{
				if (Now - StartTime > 30.0)
				{
					Test->AddError(TEXT("로컬 Valor 캐릭터를 30초 안에 찾지 못했습니다(맵/게임 모드 확인)."));
					return true;
				}
				return false;
			}

			switch (Phase)
			{
			case EPhase::Warmup:
				if (GShaderCompilingManager)
				{
					GShaderCompilingManager->FinishAllCompilation();
				}
				Phase = EPhase::Setup;
				Wait(2.0);
				return false;

			case EPhase::Setup:
			{
				// 1인칭 팔·총이 화면을 가리지 않게 끈다(로컬 연출만).
				if (UValorViewmodelComponent* Viewmodel = Character->GetViewmodelComponent())
				{
					Viewmodel->SetFirstPersonActive(false);
					Viewmodel->SetVisibility(false, true);
				}

				// 정면을 수평으로 보게 하고, 그 앞 250cm에 나를 바라보는 캐릭터를 세운다(온몸이 화면에 들어오는 거리).
				const FRotator FlatRotation(0.0f, PlayerController->GetControlRotation().Yaw, 0.0f);
				PlayerController->SetControlRotation(FlatRotation);
				const FVector DummyLocation = Character->GetActorLocation() + (FlatRotation.Vector() * 250.0f);
				FActorSpawnParameters SpawnParameters;
				SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				Dummy = Character->GetWorld()->SpawnActor<AValorCharacter>(Character->GetClass(), DummyLocation, FRotator(0.0f, FlatRotation.Yaw + 180.0f, 0.0f), SpawnParameters);
				if (!Dummy.IsValid())
				{
					Test->AddError(TEXT("확인용 캐릭터를 스폰하지 못했습니다."));
					return true;
				}

				// 컨트롤러 없이도 이동 처리(앉기 포함)를 돌린다.
				// 스폰 시점(PostInitializeComponents)엔 이 값이 꺼져 있어 이동 모드가 None으로 남으므로 기본 이동 모드도 직접 켠다.
				Dummy->GetCharacterMovement()->bRunPhysicsWithNoController = true;
				Dummy->GetCharacterMovement()->SetDefaultMovementMode();
				if (UClass* WeaponClass = LoadObject<UClass>(nullptr, TEXT("/Game/Valor/Gun/BP_Vandal.BP_Vandal_C")))
				{
					Dummy->GetCombatComponent()->GiveWeapon(WeaponClass, true, true);
				}

				SetDrawHitboxes(1);
				Phase = EPhase::Front;
				Wait(1.5);
				return false;
			}

			case EPhase::Front:
				LogShapes();
				Capture(TEXT("Front"));
				LogHorizontalScan(Character, TEXT("front"));
				LogHeadGrid(Character);
				Phase = EPhase::HeadZoomAim;
				return false;

			case EPhase::HeadZoomAim:
			{
				// 머리 확대: 머리를 바라보고 시야각을 좁힌다(카메라 매니저 잠금 FOV가 카메라 컴포넌트 FOV보다 우선한다).
				FVector CameraLocation;
				FRotator CameraRotation;
				Character->GetWeaponViewPoint(CameraLocation, CameraRotation);
				const FVector HeadLocation = Dummy->GetMesh()->GetSocketLocation(TEXT("head"));
				PlayerController->SetControlRotation((HeadLocation - CameraLocation).Rotation());
				if (PlayerController->PlayerCameraManager)
				{
					PlayerController->PlayerCameraManager->SetFOV(20.0f);
				}
				Phase = EPhase::HeadZoomShot;
				Wait(0.5);
				return false;
			}

			case EPhase::HeadZoomShot:
				Capture(TEXT("HeadZoom"));
				Phase = EPhase::SideTurn;
				Wait(0.3);
				return false;

			case EPhase::SideTurn:
			{
				if (PlayerController->PlayerCameraManager)
				{
					PlayerController->PlayerCameraManager->UnlockFOV();
				}
				PlayerController->SetControlRotation(FRotator(0.0f, PlayerController->GetControlRotation().Yaw, 0.0f));
				Dummy->SetActorRotation(Dummy->GetActorRotation() + FRotator(0.0f, 90.0f, 0.0f));
				Phase = EPhase::Side;
				Wait(1.0);
				return false;
			}

			case EPhase::Side:
				Capture(TEXT("Side"));
				LogHorizontalScan(Character, TEXT("side"));
				Dummy->Crouch();
				Phase = EPhase::Crouch;
				Wait(1.5);
				return false;

			case EPhase::Crouch:
				Capture(TEXT("CrouchSide"));
				LogHorizontalScan(Character, TEXT("crouch-side"));
				Phase = EPhase::Done;
				Wait(0.3);
				return false;

			case EPhase::Done:
			default:
				SetDrawHitboxes(0);
				Test->AddInfo(FString::Printf(TEXT("캡처 저장 위치: %s"), *OutputDirectory));
				return true;
			}
		}

	private:
		enum class EPhase : uint8
		{
			Warmup,
			Setup,
			Front,
			HeadZoomAim,
			HeadZoomShot,
			SideTurn,
			Side,
			Crouch,
			Done
		};

		void Wait(double Seconds)
		{
			WaitUntil = FPlatformTime::Seconds() + Seconds;
		}

		void Capture(const FString& ShotName)
		{
			FScreenshotRequest::RequestScreenshot(OutputDirectory / (ShotName + TEXT(".png")), true, false);
		}

		void LogShapes()
		{
			const UValorLagCompensationComponent* LagComp = Dummy->GetLagCompensationComponent();
			const TArray<FValorLagCompHitShape>& Shapes = LagComp->GetHitShapes();
			Test->AddInfo(FString::Printf(TEXT("HITBOX shapes=%d physicsAsset=%s"), Shapes.Num(), *GetNameSafe(Dummy->GetMesh()->GetPhysicsAsset())));
			for (const FValorLagCompHitShape& Shape : Shapes)
			{
				Test->AddInfo(FString::Printf(TEXT("HITBOX  %-12s %-7s %c r=%.1f halfLen=%.1f ext=%s local=%s"),
					*Shape.BoneName.ToString(), ShapeTypeName(Shape.ShapeType), ZoneLetter(Shape.HitZone), Shape.Radius, Shape.HalfLength,
					*Shape.HalfExtent.ToCompactString(), *Shape.LocalTransform.GetLocation().ToCompactString()));
			}
		}

		// 내 쪽에서 상대 중심을 향해 수평 탄을 높이 5cm 간격으로 쏜다(발 기준 높이). 실루엣 높이별 부위가 한 줄로 보인다.
		void LogHorizontalScan(const AValorCharacter* Character, const TCHAR* Label)
		{
			const UValorLagCompensationComponent* LagComp = Dummy->GetLagCompensationComponent();
			const float FootZ = Dummy->GetActorLocation().Z - Dummy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const FVector ToDummy = (Dummy->GetActorLocation() - Character->GetActorLocation()).GetSafeNormal2D();
			const float WorldTime = Dummy->GetWorld()->GetTimeSeconds();

			FString Line;
			for (int32 Height = 200; Height >= 0; Height -= 5)
			{
				const FVector Start = FVector(Character->GetActorLocation().X, Character->GetActorLocation().Y, FootZ + Height);
				FValorLagCompHitResult HitResult;
				const bool bHit = LagComp->ConfirmHitAtTime(Start, ToDummy, 1000.0f, WorldTime, HitResult);
				Line.AppendChar(bHit ? ZoneLetter(HitResult.HitZone) : TEXT('-'));
				if (Height % 50 == 0 && Height > 0)
				{
					Line.AppendChar(TEXT('|'));
				}
			}

			const float HeadTopZ = Dummy->GetMesh()->GetSocketLocation(TEXT("head")).Z - FootZ;
			Test->AddInfo(FString::Printf(TEXT("HITBOX scan %-11s (200cm->0cm, | every 50cm): %s  headBoneZ=%.0f"), Label, *Line, HeadTopZ));
		}

		// 카메라에서 머리 뼈 주변(머리 뼈 + 9cm 위가 중심)을 5cm 격자로 겨눈 탄의 부위.
		void LogHeadGrid(const AValorCharacter* Character)
		{
			FVector CameraLocation;
			FRotator CameraRotation;
			Character->GetWeaponViewPoint(CameraLocation, CameraRotation);
			const FVector HeadCenter = Dummy->GetMesh()->GetSocketLocation(TEXT("head")) + FVector(0.0f, 0.0f, 9.0f);
			const FVector Right = (HeadCenter - CameraLocation).GetSafeNormal2D() ^ FVector::UpVector;
			const UValorLagCompensationComponent* LagComp = Dummy->GetLagCompensationComponent();
			for (int32 OffsetZ = 20; OffsetZ >= -20; OffsetZ -= 5)
			{
				FString Row = FString::Printf(TEXT("HITBOX head z%+3d:"), OffsetZ);
				for (int32 OffsetY = -20; OffsetY <= 20; OffsetY += 5)
				{
					const FVector Target = HeadCenter + FVector(0.0f, 0.0f, OffsetZ) + (Right * OffsetY);
					FValorLagCompHitResult HitResult;
					const bool bHit = LagComp->ConfirmHitAtTime(CameraLocation, (Target - CameraLocation).GetSafeNormal(), 10000.0f, Dummy->GetWorld()->GetTimeSeconds(), HitResult);
					Row += FString::Printf(TEXT(" %c"), bHit ? ZoneLetter(HitResult.HitZone) : TEXT('-'));
				}
				Test->AddInfo(Row);
			}
		}

		FAutomationTestBase* Test = nullptr;
		FString OutputDirectory;
		TWeakObjectPtr<AValorCharacter> Dummy;
		EPhase Phase = EPhase::Warmup;
		double StartTime = -1.0;
		double WaitUntil = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorHitboxCaptureTool, "Valor.Tools.HitboxCapture", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorHitboxCaptureTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorHitboxTool::FValorHitboxCaptureCommand(this));
	return true;
}

#endif

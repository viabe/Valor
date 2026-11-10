#include "Components/ValorCombatComponent.h"
#include "Components/ValorViewmodelComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#include "ValorCharacter.h"
#include "Weapons/ValorWeaponBase.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 1인칭 뷰모델 캡처
// 실제 게임(-game, 단독 실행)에서 총을 하나씩 직접 지급하며 힙·조준 화면을 PNG로 저장한다. 데이터 에셋의 Viewmodel 값을 바꾼 뒤
// 구도·조준선 정렬을 한 번에 비교할 때 쓴다. 마지막에 내 앞에 캐릭터를 하나 세워 3인칭 손에 든 총도 찍는다.
// 저장 위치: Saved/ViewmodelCapture/<총>_hip.png, <총>_ads.png, Knife_hip.png, TP_<총>.png.
// 실행(에디터를 닫을 필요 없음):
//   UnrealEditor.exe Valor.uproject /Game/Maps/ThridPersonExampleMap -game -windowed -ResX=1280 -ResY=720
//     -ExecCmds="Automation RunTests Valor.Tools.ViewmodelCapture; Quit"
// ClientContext 전용이라 에디터의 "Valor." 테스트 묶음에는 들어가지 않는다(GiveWeapon은 단독 실행의 서버 권한이 필요하다).
// =====================================================================================================

namespace
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

	class FValorViewmodelCaptureCommand : public IAutomationLatentCommand
	{
	public:
		explicit FValorViewmodelCaptureCommand(FAutomationTestBase* InTest)
			: Test(InTest)
		{
			OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ViewmodelCapture"));
			IFileManager::Get().MakeDirectory(*OutputDirectory, true);

			// 밴달은 시작 무기라 먼저 찍고, 나머지는 같은 슬롯 교체로 하나씩 지급한다. 오퍼레이터 등 조준경 총은 조준 시 총을 숨기므로 힙만 찍는다.
			static const TCHAR* const WeaponNames[] =
			{
				TEXT("BP_Vandal"), TEXT("BP_Phantom"), TEXT("BP_Spectre"), TEXT("BP_STINGER"), TEXT("BP_BULLDOG"), TEXT("BP_GUARDIAN"),
				TEXT("BP_JUDGE"), TEXT("BP_BUCKY"), TEXT("BP_MARSHALL"), TEXT("BP_OUTLAW"), TEXT("BP_Operator"), TEXT("BP_ODIN"), TEXT("BP_ARES"), TEXT("BP_WARDEN")
			};
			for (const TCHAR* WeaponName : WeaponNames)
			{
				Weapons.Add(WeaponName);
			}
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
			UValorCombatComponent* CombatComponent = Character ? Character->GetCombatComponent() : nullptr;
			// 맵의 게임 모드에 따라 시작 무기가 없을 수 있으므로(칼만 든 채 시작) 캐릭터만 기다리고 총은 직접 지급한다.
			if (!CombatComponent)
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
				// 첫 화면은 셰이더 컴파일 중이라 기본 머티리얼로 보일 수 있다. 다 끝낸 뒤 찍는다.
				if (GShaderCompilingManager)
				{
					GShaderCompilingManager->FinishAllCompilation();
				}
				Phase = EPhase::EquipWeapon;
				Wait(2.0);
				return false;

			case EPhase::EquipWeapon:
			{
				if (WeaponIndex >= Weapons.Num())
				{
					Character->HandleEquipSlotInput(EValorWeaponSlot::Melee);
					Phase = EPhase::KnifeShot;
					Wait(1.5);
					return false;
				}

				const FString& WeaponName = Weapons[WeaponIndex];
				UClass* WeaponClass = LoadObject<UClass>(nullptr, *FString::Printf(TEXT("/Game/Valor/Gun/%s.%s_C"), *WeaponName, *WeaponName));
				if (!WeaponClass || !WeaponClass->IsChildOf(AValorWeaponBase::StaticClass()))
				{
					Test->AddWarning(FString::Printf(TEXT("%s 클래스를 찾지 못해 건너뜁니다."), *WeaponName));
					++WeaponIndex;
					return false;
				}

				// 같은 슬롯의 이전 총은 교체 규칙대로 떨어진다(장착 시간 없이 바로 든다).
				CombatComponent->GiveWeapon(WeaponClass, true, true);

				if (GShaderCompilingManager)
				{
					GShaderCompilingManager->FinishAllCompilation();
				}
				Phase = EPhase::HipShot;
				Wait(1.2);
				return false;
			}

			case EPhase::HipShot:
			{
				Capture(Weapons[WeaponIndex] + TEXT("_hip"));
				AValorWeaponBase* Weapon = CombatComponent->GetEquippedWeapon();
				if (Weapon && Weapon->GetMaxZoomLevel() > 0 && !Weapon->UsesScopeOverlay())
				{
					Character->HandleADSInputPressed();
					Phase = EPhase::ADSShot;
					Wait(1.0);
					return false;
				}

				++WeaponIndex;
				Phase = EPhase::EquipWeapon;
				Wait(0.3);
				return false;
			}

			case EPhase::ADSShot:
				Capture(Weapons[WeaponIndex] + TEXT("_ads"));
				++WeaponIndex;
				Phase = EPhase::EquipWeapon;
				Wait(0.3);
				return false;

			case EPhase::KnifeShot:
				Capture(TEXT("Knife_hip"));
				Phase = EPhase::ThirdPersonSetup;
				Wait(0.3);
				return false;

			case EPhase::ThirdPersonSetup:
			{
				// 3인칭 확인: 내 앞에 같은 캐릭터 클래스를 하나 세운다. 컨트롤러가 없으므로 총은 3인칭 손 소켓에 붙는다.
				FVector ViewLocation;
				FRotator ViewRotation;
				Character->GetActorEyesViewPoint(ViewLocation, ViewRotation);
				const FRotator FlatRotation(0.0f, ViewRotation.Yaw, 0.0f);
				const FVector DummyLocation = Character->GetActorLocation() + (FlatRotation.Vector() * 170.0f) + (FlatRotation.RotateVector(FVector::RightVector) * -25.0f);
				FActorSpawnParameters SpawnParameters;
				SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				// 내 눈을 정면으로 바라보게 세운다(조준 피치 0, 눈높이 같음). 3인칭 총구가 정확히 나를 향하는지 볼 수 있다.
				const float FacingYaw = (ViewLocation - DummyLocation).GetSafeNormal2D().Rotation().Yaw;
				Dummy = Character->GetWorld()->SpawnActor<AValorCharacter>(Character->GetClass(), DummyLocation, FRotator(0.0f, FacingYaw, 0.0f), SpawnParameters);
				if (!Dummy.IsValid())
				{
					Test->AddWarning(TEXT("3인칭 확인용 캐릭터를 스폰하지 못했습니다."));
					Phase = EPhase::Done;
					return false;
				}

				// 1인칭 칼·팔이 화면을 가리지 않게 내 쪽 1인칭 연출을 끈다(로컬 연출만).
				if (UValorViewmodelComponent* Viewmodel = Character->GetViewmodelComponent())
				{
					Viewmodel->SetFirstPersonActive(false);
					Viewmodel->SetVisibility(false, true);
				}

				ThirdPersonIndex = 0;
				Phase = EPhase::ThirdPersonEquip;
				Wait(1.0);
				return false;
			}

			case EPhase::ThirdPersonEquip:
			{
				static const TCHAR* const ThirdPersonWeapons[] = {TEXT("BP_Vandal"), TEXT("BP_Phantom"), TEXT("BP_ODIN"), TEXT("BP_JUDGE"), TEXT("BP_Spectre")};
				UValorCombatComponent* DummyCombat = Dummy.IsValid() ? Dummy->GetCombatComponent() : nullptr;
				if (!DummyCombat || ThirdPersonIndex >= UE_ARRAY_COUNT(ThirdPersonWeapons))
				{
					Phase = EPhase::Done;
					return false;
				}

				ThirdPersonName = ThirdPersonWeapons[ThirdPersonIndex++];
				if (UClass* WeaponClass = LoadObject<UClass>(nullptr, *FString::Printf(TEXT("/Game/Valor/Gun/%s.%s_C"), *ThirdPersonName, *ThirdPersonName)))
				{
					DummyCombat->GiveWeapon(WeaponClass, true, true);
				}
				Phase = EPhase::ThirdPersonShot;
				Wait(1.0);
				return false;
			}

			case EPhase::ThirdPersonShot:
				Capture(TEXT("TP_") + ThirdPersonName);
				if (Dummy.IsValid() && Dummy->GetMesh())
				{
					// 손 소켓 축을 캐릭터 기준으로 기록한다(총 리그 Z = 총구가 소켓 Z를 따른다). 3인칭 총 방향 조사용.
					const FTransform SocketWorld = Dummy->GetMesh()->GetSocketTransform(Dummy->GetWeaponAttachSocketName(), RTS_World);
					const FTransform SocketInActor = SocketWorld.GetRelativeTransform(Dummy->GetActorTransform());
					const FQuat SocketRotation = SocketInActor.GetRotation();
					Test->AddInfo(FString::Printf(TEXT("TP %s socketX=%s socketY=%s socketZ=%s loc=%s"), *ThirdPersonName,
						*SocketRotation.GetAxisX().ToString(), *SocketRotation.GetAxisY().ToString(), *SocketRotation.GetAxisZ().ToString(), *SocketInActor.GetLocation().ToString()));

					// 3인칭 조준 정렬 확인: 총구(리그 Z)가 내 카메라를 얼마나 벗어나 있나(보정 전 = 손 자세만, 보정 후 = 실제).
					const AValorWeaponBase* DummyWeapon = Dummy->GetCombatComponent() ? Dummy->GetCombatComponent()->GetEquippedWeapon() : nullptr;
					if (DummyWeapon && DummyWeapon->GetRootComponent())
					{
						FVector CameraLocation;
						FRotator CameraRotation;
						Character->GetWeaponViewPoint(CameraLocation, CameraRotation);
						const FVector GripLocation = DummyWeapon->GetRootComponent()->GetComponentLocation();
						const FVector ToCamera = (CameraLocation - GripLocation).GetSafeNormal();
						const FVector CorrectedMuzzle = DummyWeapon->GetRootComponent()->GetComponentQuat().RotateVector(FVector::UpVector);
						const FVector BaseMuzzle = (SocketWorld.GetRotation() * Dummy->GetThirdPersonWeaponRotation()).RotateVector(FVector::UpVector);
						Test->AddInfo(FString::Printf(TEXT("TPAIM %s base=%.1fdeg corrected=%.1fdeg"), *ThirdPersonName,
							FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(BaseMuzzle | ToCamera, -1.0f, 1.0f))),
							FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(CorrectedMuzzle | ToCamera, -1.0f, 1.0f)))));
					}
				}
				Phase = EPhase::ThirdPersonEquip;
				Wait(0.3);
				return false;

			case EPhase::Done:
			default:
				Test->AddInfo(FString::Printf(TEXT("캡처 저장 위치: %s"), *OutputDirectory));
				return true;
			}
		}

	private:
		enum class EPhase : uint8
		{
			Warmup,
			EquipWeapon,
			HipShot,
			ADSShot,
			KnifeShot,
			ThirdPersonSetup,
			ThirdPersonEquip,
			ThirdPersonShot,
			Done
		};

		void Wait(double Seconds)
		{
			WaitUntil = FPlatformTime::Seconds() + Seconds;
		}

		void Capture(const FString& ShotName)
		{
			// HUD(크로스헤어)까지 포함해야 조준선이 화면 중앙에 맞는지 볼 수 있다.
			FScreenshotRequest::RequestScreenshot(OutputDirectory / (ShotName + TEXT(".png")), true, false);
		}

		FAutomationTestBase* Test = nullptr;
		FString OutputDirectory;
		TArray<FString> Weapons;
		int32 WeaponIndex = 0;
		TWeakObjectPtr<AValorCharacter> Dummy;
		int32 ThirdPersonIndex = 0;
		FString ThirdPersonName;
		EPhase Phase = EPhase::Warmup;
		double StartTime = -1.0;
		double WaitUntil = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorViewmodelCaptureTool, "Valor.Tools.ViewmodelCapture", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorViewmodelCaptureTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FValorViewmodelCaptureCommand(this));
	return true;
}

#endif

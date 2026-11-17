#include "Components/ValorCombatComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProperties.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/ValorPlayerController.h"
#include "Settings/ValorUserSettings.h"
#include "UI/ValorHUD.h"
#include "UnrealClient.h"
#include "ValorCharacter.h"
#include "Weapons/ValorWeaponBase.h"

#if WITH_DEV_AUTOMATION_TESTS

// =====================================================================================================
// 개발 도구: 설정(감도·조준 방식·설정 창·조준선·저장) 확인. 데스매치 혼자(밴달 지급).
//  1) 예전 시점 경로(입력 매핑 값)로 마우스 100카운트 → 몇 도인지 잰다(예전 느낌 = 발로란트 감도 몇인지).
//  2) 새 경로: ValorSens 0.5 → 100카운트 = 3.5도(좌우·위), 반전이면 아래로.
//  3) 조준 방식: 누르기(누르는 동안만) / 토글. 조준 중 감도 = 줌 비율(밴달 0.8) × 조준 감도 배율.
//  4) F10으로 설정 창: 커서·시점 멈춤·사격 막힘, 클릭 영역으로 값 바꾸기, 두 탭 캡처(Saved/SettingsFlow), Esc로 닫기.
//  5) 저장 파일에 값이 쓰였는지, 새 객체가 같은 값을 읽는지.
//  끝나면(실패해도) 처음 설정으로 되돌려 저장한다(사용자 설정을 남기지 않는다). 로그는 SETTINGS로 시작한다.
// 실행:
//   UnrealEditor.exe Valor.uproject "/Game/Maps/ThridPersonExampleMap?game=DM" -game -windowed -ResX=1280 -ResY=720
//     -ExecCmds="Automation RunTests Valor.Tools.SettingsFlow; Quit"
// =====================================================================================================

namespace ValorSettingsFlowTool
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

	class FValorSettingsFlowCommand : public IAutomationLatentCommand
	{
	public:
		explicit FValorSettingsFlowCommand(FAutomationTestBase* InTest)
			: Test(InTest)
		{
			OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SettingsFlow"));
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
				Test->AddError(FString::Printf(TEXT("SETTINGS 90초 안에 끝나지 않았습니다(단계 %d)."), Step));
				Restore();
				return true;
			}
			if (Now < WaitUntil)
			{
				return false;
			}

			UWorld* World = FindGameWorld();
			AValorPlayerController* Controller = World ? Cast<AValorPlayerController>(GEngine->GetFirstLocalPlayerController(World)) : nullptr;
			AValorCharacter* Me = Controller ? Cast<AValorCharacter>(Controller->GetPawn()) : nullptr;
			UValorCombatComponent* Combat = Me ? Me->GetCombatComponent() : nullptr;
			AValorHUD* HUD = Controller ? Cast<AValorHUD>(Controller->GetHUD()) : nullptr;
			if (!Controller || !Me || !Combat || !HUD || !Me->IsAlive())
			{
				return false;
			}
			UValorUserSettings& Settings = UValorUserSettings::Get();

			switch (Step)
			{
			case 0:
				if (!Combat->GetEquippedWeapon() || !Combat->IsEquipReady())
				{
					return false;
				}
				SavedControls = Settings.GetControls();
				SavedCrosshair = Settings.GetCrosshair();
				bSnapshotTaken = true;
				bSectionExisted = GConfig->DoesSectionExist(SettingsSection, *GGameUserSettingsIni);
				Settings.ResetControls();
				Settings.ResetCrosshair();
				Settings.Save();
				Test->AddInfo(FString::Printf(TEXT("SETTINGS weapon=%s file=%s"), *GetNameSafe(Combat->GetEquippedWeapon()), *UValorUserSettings::GetSaveFilePath()));

				// 1) 예전 경로.
				SetRawLook(false);
				InjectMouse(*Controller, 100.0, 0.0);
				Step = 1;
				Wait(0.3);
				return false;

			case 1:
				LegacyDegreesPerCount = MeasureYaw(*Controller) / 100.0;
				Test->AddInfo(FString::Printf(TEXT("SETTINGS legacy look: %.4f deg/count = Valorant sensitivity %.3f"), LegacyDegreesPerCount, LegacyDegreesPerCount / ValorSettingsRules::YawDegreesPerCount));
				SetRawLook(true);
				Controller->ConsoleCommand(TEXT("ValorSens 0.5"));
				Expect(FMath::IsNearlyEqual(Settings.GetControls().MouseSensitivity, 0.5f), TEXT("ValorSens 0.5 적용"));
				InjectMouse(*Controller, 100.0, 0.0);
				Step = 2;
				Wait(0.3);
				return false;

			case 2:
			{
				const double Yaw = MeasureYaw(*Controller);
				Test->AddInfo(FString::Printf(TEXT("SETTINGS raw look yaw: %.3f deg (expect 3.5)"), Yaw));
				Expect(FMath::IsNearlyEqual(Yaw, 3.5, 0.02), TEXT("감도 0.5: 100카운트 = 3.5도"));
				InjectMouse(*Controller, 0.0, 100.0);
				Step = 3;
				Wait(0.3);
				return false;
			}

			case 3:
			{
				const double Pitch = MeasurePitch(*Controller);
				Test->AddInfo(FString::Printf(TEXT("SETTINGS raw look pitch(up): %.3f deg (expect 3.5)"), Pitch));
				Expect(FMath::IsNearlyEqual(Pitch, 3.5, 0.02), TEXT("앞으로 밀면 위로 3.5도"));
				Settings.EditControls().bInvertMouse = true;
				InjectMouse(*Controller, 0.0, 100.0);
				Step = 4;
				Wait(0.3);
				return false;
			}

			case 4:
			{
				const double Pitch = MeasurePitch(*Controller);
				Test->AddInfo(FString::Printf(TEXT("SETTINGS inverted pitch: %.3f deg (expect -3.5)"), Pitch));
				Expect(FMath::IsNearlyEqual(Pitch, -3.5, 0.02), TEXT("반전: 앞으로 밀면 아래로"));
				Settings.EditControls().bInvertMouse = false;

				// 3) 누르기 방식: 누르는 동안만 조준.
				Settings.EditControls().bToggleADS = false;
				PressKey(*Controller, EKeys::RightMouseButton, IE_Pressed);
				Step = 5;
				Wait(0.4);
				return false;
			}

			case 5:
				Test->AddInfo(FString::Printf(TEXT("SETTINGS hold ADS: zoomed=%d scale=%.3f"), Combat->IsLocallyZoomed() ? 1 : 0, Controller->GetCurrentLookSensitivityScale()));
				Expect(Combat->IsLocallyZoomed(), TEXT("누르기: 누르고 있는 동안 조준"));
				Expect(FMath::IsNearlyEqual(Controller->GetCurrentLookSensitivityScale(), 0.8f, 0.01f), TEXT("밴달 조준 감도 배율 0.8"));
				InjectMouse(*Controller, 100.0, 0.0);
				Step = 6;
				Wait(0.3);
				return false;

			case 6:
			{
				const double Yaw = MeasureYaw(*Controller);
				Test->AddInfo(FString::Printf(TEXT("SETTINGS scoped yaw: %.3f deg (expect 2.8)"), Yaw));
				Expect(FMath::IsNearlyEqual(Yaw, 2.8, 0.02), TEXT("조준 중 100카운트 = 3.5 × 0.8"));
				PressKey(*Controller, EKeys::RightMouseButton, IE_Released);
				Step = 7;
				Wait(0.3);
				return false;
			}

			case 7:
				Expect(!Combat->IsLocallyZoomed(), TEXT("누르기: 떼면 조준 해제"));
				Settings.EditControls().bToggleADS = true;
				PressKey(*Controller, EKeys::RightMouseButton, IE_Pressed);
				Step = 8;
				Wait(0.15);
				return false;

			case 8:
				PressKey(*Controller, EKeys::RightMouseButton, IE_Released);
				Step = 9;
				Wait(0.3);
				return false;

			case 9:
				Expect(Combat->IsLocallyZoomed(), TEXT("토글: 눌렀다 떼도 조준 유지"));
				PressKey(*Controller, EKeys::RightMouseButton, IE_Pressed);
				Step = 10;
				Wait(0.15);
				return false;

			case 10:
				PressKey(*Controller, EKeys::RightMouseButton, IE_Released);
				Step = 11;
				Wait(0.3);
				return false;

			case 11:
				Expect(!Combat->IsLocallyZoomed(), TEXT("토글: 한 번 더 누르면 해제"));

				// 4) 설정 창(실제 F10 키).
				PressKey(*Controller, EKeys::F10, IE_Pressed);
				Step = 12;
				Wait(0.1);
				return false;

			case 12:
				PressKey(*Controller, EKeys::F10, IE_Released);
				Step = 13;
				Wait(0.3);
				return false;

			case 13:
				Test->AddInfo(FString::Printf(TEXT("SETTINGS menu open=%d cursor=%d lookIgnored=%d"), Controller->IsSettingsOpen() ? 1 : 0, Controller->bShowMouseCursor ? 1 : 0, Controller->IsLookInputIgnored() ? 1 : 0));
				Expect(Controller->IsSettingsOpen() && Controller->bShowMouseCursor, TEXT("F10으로 설정 창이 열리고 커서가 보임"));
				AmmoBefore = Combat->GetDisplayedMagazineAmmo();
				InjectMouse(*Controller, 100.0, 0.0);
				// 사격 입력은 캐릭터 입력 함수로 넣는다(실제 왼쪽 클릭은 커서 위치의 설정 창 칸을 누를 수 있다).
				Me->HandleFireInputPressed();
				Step = 14;
				Wait(0.3);
				return false;

			case 14:
			{
				Me->HandleFireInputReleased();
				const double Yaw = MeasureYaw(*Controller);
				Test->AddInfo(FString::Printf(TEXT("SETTINGS while open: yaw=%.3f ammo %d -> %d"), Yaw, AmmoBefore, Combat->GetDisplayedMagazineAmmo()));
				Expect(FMath::IsNearlyZero(Yaw, 0.01), TEXT("창이 열려 있으면 시점이 돌지 않음"));
				Expect(Combat->GetDisplayedMagazineAmmo() == AmmoBefore, TEXT("창이 열려 있으면 총을 쏘지 않음"));

				// 클릭 영역으로 바꾸기(조작 탭).
				HUD->NotifyHitBoxClick(TEXT("Set_Sens_Plus"));
				HUD->NotifyHitBoxClick(TEXT("Set_ADS_Off"));
				HUD->NotifyHitBoxClick(TEXT("Set_ScopedSens_Minus"));
				Expect(FMath::IsNearlyEqual(Settings.GetControls().MouseSensitivity, 0.51f, 0.0001f), TEXT("감도 + 버튼: 0.51"));
				Expect(!Settings.GetControls().bToggleADS, TEXT("조준 방식 HOLD 버튼"));
				Expect(FMath::IsNearlyEqual(Settings.GetControls().ScopedSensitivityMultiplier, 0.99f, 0.0001f), TEXT("조준 배율 - 버튼: 0.99"));
				Step = 15;
				Wait(0.3);
				return false;
			}

			case 15:
				FScreenshotRequest::RequestScreenshot(OutputDirectory / TEXT("Settings_Controls.png"), true, false);
				Step = 16;
				Wait(0.5);
				return false;

			case 16:
				HUD->NotifyHitBoxClick(TEXT("Set_Tab_1"));
				HUD->NotifyHitBoxClick(TEXT("Set_Color_6"));
				HUD->NotifyHitBoxClick(TEXT("Set_InnerLength_Plus"));
				HUD->NotifyHitBoxClick(TEXT("Set_InnerLength_Plus"));
				HUD->NotifyHitBoxClick(TEXT("Set_CenterDot_On"));
				HUD->NotifyHitBoxClick(TEXT("Set_OuterShow_Off"));
				Expect(Controller->SettingsTab == 1, TEXT("조준선 탭"));
				Expect(Settings.GetCrosshair().Color == ValorSettingsRules::GetCrosshairColorPresets()[6], TEXT("색 견본 7번째(분홍)"));
				Expect(FMath::IsNearlyEqual(Settings.GetCrosshair().Inner.Length, 8.0f), TEXT("안쪽 선 길이 6 → 8"));
				Expect(Settings.GetCrosshair().bCenterDot && !Settings.GetCrosshair().Outer.bShow, TEXT("가운데 점 켬, 바깥 선 끔"));
				Step = 17;
				Wait(0.3);
				return false;

			case 17:
				FScreenshotRequest::RequestScreenshot(OutputDirectory / TEXT("Settings_Crosshair.png"), true, false);
				Step = 18;
				Wait(0.5);
				return false;

			case 18:
			{
				// 5) 저장: 파일에 값이 쓰였고, 새 객체가 같은 값을 읽는다.
				FString FileText;
				const FString FilePath = FindSaveFile();
				const bool bRead = FFileHelper::LoadFileToString(FileText, *FilePath);
				const bool bHasSection = FileText.Contains(TEXT("[/Script/Valor.ValorUserSettings]"));
				const bool bHasSens = FileText.Contains(TEXT("MouseSensitivity=0.51"));
				Test->AddInfo(FString::Printf(TEXT("SETTINGS file=%s read=%d section=%d sens=%d"), *FilePath, bRead ? 1 : 0, bHasSection ? 1 : 0, bHasSens ? 1 : 0));
				Expect(bRead && bHasSection && bHasSens, TEXT("저장 파일에 설정 구역과 감도 0.51"));

				UValorUserSettings* Fresh = NewObject<UValorUserSettings>(GetTransientPackage());
				Fresh->LoadConfig();
				Expect(FMath::IsNearlyEqual(Fresh->GetControls().MouseSensitivity, 0.51f, 0.0001f) && Fresh->GetCrosshair().Color == Settings.GetCrosshair().Color
					&& FMath::IsNearlyEqual(Fresh->GetCrosshair().Inner.Length, 8.0f), TEXT("새로 읽은 설정이 같음"));

				// Esc로 닫는다.
				PressKey(*Controller, EKeys::Escape, IE_Pressed);
				Step = 19;
				Wait(0.1);
				return false;
			}

			case 19:
				PressKey(*Controller, EKeys::Escape, IE_Released);
				Step = 20;
				Wait(0.3);
				return false;

			case 20:
				Expect(!Controller->IsSettingsOpen() && !Controller->bShowMouseCursor && !Controller->IsLookInputIgnored(), TEXT("Esc로 닫힘, 커서·시점 돌아옴"));
				FScreenshotRequest::RequestScreenshot(OutputDirectory / TEXT("Crosshair_Game.png"), true, false);
				Step = 21;
				Wait(0.5);
				return false;

			default:
				Restore();
				return true;
			}
		}

	private:
		void Expect(bool bCondition, const TCHAR* What)
		{
			if (!bCondition)
			{
				Test->AddError(FString::Printf(TEXT("SETTINGS 실패: %s"), What));
			}
		}

		void Wait(double Seconds)
		{
			WaitUntil = FPlatformTime::Seconds() + Seconds;
		}

		static void SetRawLook(bool bRaw)
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Valor.Input.RawMouseLook")))
			{
				Variable->Set(bRaw ? 1 : 0, ECVF_SetByCode);
			}
		}

		// 실제 마우스와 같은 경로(뷰포트 → 컨트롤러 InputKey)로 카운트를 넣는다. 다음 입력 처리 프레임에 한 번 쓰인다.
		void InjectMouse(AValorPlayerController& Controller, double CountsX, double CountsY)
		{
			StartRotation = Controller.GetControlRotation();
			if (CountsX != 0.0)
			{
				Controller.InputKey(FInputKeyParams(EKeys::MouseX, CountsX, 1.0f / 60.0f, 1));
			}
			if (CountsY != 0.0)
			{
				Controller.InputKey(FInputKeyParams(EKeys::MouseY, CountsY, 1.0f / 60.0f, 1));
			}
		}

		double MeasureYaw(const AValorPlayerController& Controller) const
		{
			return FRotator::NormalizeAxis(Controller.GetControlRotation().Yaw - StartRotation.Yaw);
		}

		double MeasurePitch(const AValorPlayerController& Controller) const
		{
			return FRotator::NormalizeAxis(Controller.GetControlRotation().Pitch - StartRotation.Pitch);
		}

		static void PressKey(AValorPlayerController& Controller, const FKey& Key, EInputEvent Event)
		{
			Controller.InputKey(FInputKeyParams(Key, Event, Event == IE_Pressed ? 1.0 : 0.0, false));
		}

		static FString FindSaveFile()
		{
			// GGameUserSettingsIni가 경로면 그대로, 아니면 저장 폴더/플랫폼/GameUserSettings.ini.
			FString Path = UValorUserSettings::GetSaveFilePath();
			if (!FPaths::FileExists(Path))
			{
				Path = FPaths::Combine(FPaths::GeneratedConfigDir(), ANSI_TO_TCHAR(FPlatformProperties::PlatformName()), TEXT("GameUserSettings.ini"));
			}
			return FPaths::ConvertRelativePathToFull(Path);
		}

		// 처음 설정으로 되돌려 저장(사용자 설정을 남기지 않는다).
		void Restore()
		{
			SetRawLook(true);
			if (!bSnapshotTaken)
			{
				return;
			}
			UValorUserSettings& Settings = UValorUserSettings::Get();
			Settings.EditControls() = SavedControls;
			Settings.EditCrosshair() = SavedCrosshair;
			if (bSectionExisted)
			{
				Settings.Save();
			}
			else
			{
				// 처음에 설정 구역이 없었다(기본값 사용 중): 구역을 지워 기본값이 바뀌어도 그대로 따라가게 한다.
				GConfig->EmptySection(SettingsSection, *GGameUserSettingsIni);
				GConfig->Flush(false, *GGameUserSettingsIni);
			}
			bSnapshotTaken = false;
		}

		FAutomationTestBase* Test = nullptr;
		FString OutputDirectory;
		int32 Step = 0;
		double StartTime = -1.0;
		double WaitUntil = 0.0;
		FRotator StartRotation = FRotator::ZeroRotator;
		double LegacyDegreesPerCount = 0.0;
		int32 AmmoBefore = 0;
		bool bSnapshotTaken = false;
		bool bSectionExisted = false;
		static constexpr const TCHAR* SettingsSection = TEXT("/Script/Valor.ValorUserSettings");
		FValorControlSettings SavedControls;
		FValorCrosshairSettings SavedCrosshair;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorSettingsFlowTool, "Valor.Tools.SettingsFlow", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FValorSettingsFlowTool::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ValorSettingsFlowTool::FValorSettingsFlowCommand(this));
	return true;
}

#endif

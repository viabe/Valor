#include "Settings/ValorSettingsTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// 설정 규칙: 감도(발로란트 숫자 → 도), 조준 감도 줌 비율, 막대 값 맞추기, 저장값 검사, 색 입력.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorSettingsRulesTest, "Valor.Settings.Rules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorSettingsRulesTest::RunTest(const FString& Parameters)
{
	auto Near = [](double A, double B, double Tolerance = 0.0005) { return FMath::IsNearlyEqual(A, B, Tolerance); };

	// 감도: 1이면 100카운트 = 7도. 0.4면 2.8도. 오른쪽·앞으로 민 것이 +.
	const FVector2D Sens1 = ValorSettingsRules::ComputeLookDegrees(FVector2D(100.0, 100.0), 1.0f, 1.0f, false);
	TestTrue(TEXT("감도 1: 좌우 7도"), Near(Sens1.X, 7.0));
	TestTrue(TEXT("감도 1: 위로 7도"), Near(Sens1.Y, 7.0));
	const FVector2D Sens04 = ValorSettingsRules::ComputeLookDegrees(FVector2D(-100.0, 50.0), 0.4f, 1.0f, false);
	TestTrue(TEXT("감도 0.4: 왼쪽 2.8도"), Near(Sens04.X, -2.8));
	TestTrue(TEXT("감도 0.4: 위로 1.4도"), Near(Sens04.Y, 1.4));
	const FVector2D Inverted = ValorSettingsRules::ComputeLookDegrees(FVector2D(0.0, 100.0), 1.0f, 1.0f, true);
	TestTrue(TEXT("마우스 반전: 앞으로 밀면 아래"), Near(Inverted.Y, -7.0));

	// 조준: 밴달 1.25배 → 줌 비율 0.8. 오퍼레이터 2.5배 → 0.4. 조준 감도 배율과 곱한다.
	const float Hip = 103.0f;
	auto ZoomFov = [Hip](float Zoom) { return FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Hip * 0.5f)) / Zoom)); };
	TestTrue(TEXT("밴달 줌 비율 0.8"), Near(ValorSettingsRules::ComputeZoomSensitivityRatio(Hip, ZoomFov(1.25f)), 0.8, 0.001));
	TestTrue(TEXT("오퍼레이터 1단 0.4"), Near(ValorSettingsRules::ComputeZoomSensitivityRatio(Hip, ZoomFov(2.5f)), 0.4, 0.001));
	TestTrue(TEXT("줌 아님 = 1"), Near(ValorSettingsRules::ComputeZoomSensitivityRatio(Hip, Hip), 1.0));
	const FVector2D Scoped = ValorSettingsRules::ComputeLookDegrees(FVector2D(100.0, 0.0), 0.4f, 0.8f * 1.0f, false);
	TestTrue(TEXT("감도 0.4 + 밴달 조준: 2.24도"), Near(Scoped.X, 2.24));

	// 막대 값: 단위에 맞춰 저장(0.1 + 0.01 같은 오차가 남지 않음), 범위 밖은 자른다.
	TestTrue(TEXT("0.40 + 0.01 = 0.41"), Near(ValorSettingsRules::SnapToStep(0.40f + 0.01f, 0.001f, 10.0f, 0.001f), 0.41, 1e-6));
	TestTrue(TEXT("반올림 0.4006 → 0.401"), Near(ValorSettingsRules::SnapToStep(0.4006f, 0.001f, 10.0f, 0.001f), 0.401, 1e-6));
	TestTrue(TEXT("최소 아래 → 최소"), Near(ValorSettingsRules::SnapToStep(-1.0f, 0.001f, 10.0f, 0.001f), 0.001, 1e-6));
	TestTrue(TEXT("막대 가운데 = 범위 가운데"), Near(ValorSettingsRules::FractionToValue(0.5f, 0.0f, 20.0f, 1.0f), 10.0));
	TestTrue(TEXT("막대 밖으로 끌어도 끝값"), Near(ValorSettingsRules::FractionToValue(1.7f, 0.0f, 20.0f, 1.0f), 20.0));
	TestTrue(TEXT("값 → 막대 위치"), Near(ValorSettingsRules::ValueToFraction(0.5f, 0.0f, 2.0f), 0.25));

	// 저장값 검사: 깨진 값(NaN)은 기본값, 범위 밖은 자른다.
	FValorControlSettings Controls;
	Controls.MouseSensitivity = NAN;
	Controls.ScopedSensitivityMultiplier = 99.0f;
	ValorSettingsRules::Sanitize(Controls);
	TestTrue(TEXT("NaN 감도 → 기본값"), Near(Controls.MouseSensitivity, FValorControlSettings().MouseSensitivity));
	TestTrue(TEXT("조준 배율 상한"), Near(Controls.ScopedSensitivityMultiplier, ValorSettingsRules::MaxScopedMultiplier));

	FValorCrosshairSettings Crosshair;
	Crosshair.Inner.Opacity = 3.0f;
	Crosshair.Outer.Length = -5.0f;
	Crosshair.OutlineThickness = 0.0f;
	Crosshair.Color.A = 10;
	ValorSettingsRules::Sanitize(Crosshair);
	TestTrue(TEXT("투명도 1까지"), Near(Crosshair.Inner.Opacity, 1.0));
	TestTrue(TEXT("길이 0부터"), Near(Crosshair.Outer.Length, 0.0));
	TestTrue(TEXT("외곽선 두께 1부터"), Near(Crosshair.OutlineThickness, 1.0));
	TestEqual(TEXT("색은 항상 불투명"), static_cast<int32>(Crosshair.Color.A), 255);

	// 기본 조준선 = 예전 HUD 값(설정을 처음 쓰는 사람의 화면이 그대로).
	const FValorCrosshairSettings Defaults;
	TestTrue(TEXT("안쪽 선 기본 6/2/3/0.8"), Near(Defaults.Inner.Length, 6.0) && Near(Defaults.Inner.Thickness, 2.0) && Near(Defaults.Inner.Offset, 3.0) && Near(Defaults.Inner.Opacity, 0.8));
	TestTrue(TEXT("바깥 선 기본 2/2/10/0.35, 움직임 오차 켬"), Near(Defaults.Outer.Length, 2.0) && Near(Defaults.Outer.Offset, 10.0) && Near(Defaults.Outer.Opacity, 0.35) && Defaults.Outer.bMovementError);
	TestFalse(TEXT("안쪽 선 움직임 오차 끔"), Defaults.Inner.bMovementError);

	// 색 입력.
	FColor Parsed;
	TestTrue(TEXT("00FF00"), ValorSettingsRules::TryParseHexColor(TEXT("00FF00"), Parsed) && Parsed == FColor(0, 255, 0));
	TestTrue(TEXT("#ff00ff"), ValorSettingsRules::TryParseHexColor(TEXT("#ff00ff"), Parsed) && Parsed == FColor(255, 0, 255));
	TestFalse(TEXT("자릿수 틀림"), ValorSettingsRules::TryParseHexColor(TEXT("FFF"), Parsed));
	TestFalse(TEXT("16진수 아님"), ValorSettingsRules::TryParseHexColor(TEXT("GG0000"), Parsed));
	TestEqual(TEXT("색 견본 8개"), ValorSettingsRules::GetCrosshairColorPresets().Num(), 8);
	return true;
}

#endif

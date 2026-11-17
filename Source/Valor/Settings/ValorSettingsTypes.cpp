#include "Settings/ValorSettingsTypes.h"

FValorCrosshairSettings::FValorCrosshairSettings()
{
	// 바깥 선 기본값: 짧고 흐리게, 움직임·연사 모두 벌어진다(안쪽 선은 구조체 기본값 그대로).
	Outer.Opacity = 0.35f;
	Outer.Length = 2.0f;
	Outer.Thickness = 2.0f;
	Outer.Offset = 10.0f;
	Outer.bMovementError = true;
	Outer.bFiringError = true;
}

namespace ValorSettingsRules
{
	namespace
	{
		// NaN·무한대는 기본값으로, 나머지는 범위로 자른다.
		float SanitizeFloat(float Value, float Min, float Max, float Fallback)
		{
			return FMath::IsFinite(Value) ? FMath::Clamp(Value, Min, Max) : Fallback;
		}

		void SanitizeLines(FValorCrosshairLineSettings& Lines, const FValorCrosshairLineSettings& Defaults)
		{
			Lines.Opacity = SanitizeFloat(Lines.Opacity, 0.0f, 1.0f, Defaults.Opacity);
			Lines.Length = SanitizeFloat(Lines.Length, 0.0f, 20.0f, Defaults.Length);
			Lines.Thickness = SanitizeFloat(Lines.Thickness, 0.0f, 10.0f, Defaults.Thickness);
			Lines.Offset = SanitizeFloat(Lines.Offset, 0.0f, 40.0f, Defaults.Offset);
		}
	}

	FVector2D ComputeLookDegrees(const FVector2D& MouseCounts, float Sensitivity, float ZoomSensitivityScale, bool bInvertY)
	{
		const float DegreesPerCount = YawDegreesPerCount * FMath::Max(Sensitivity, 0.0f) * FMath::Max(ZoomSensitivityScale, 0.0f);
		return FVector2D(MouseCounts.X * DegreesPerCount, MouseCounts.Y * DegreesPerCount * (bInvertY ? -1.0f : 1.0f));
	}

	float ComputeZoomSensitivityRatio(float HipFieldOfView, float ZoomFieldOfView)
	{
		const float HipTan = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(HipFieldOfView, 5.0f, 170.0f) * 0.5f));
		const float ZoomTan = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(ZoomFieldOfView, 1.0f, 170.0f) * 0.5f));
		return HipTan > KINDA_SMALL_NUMBER ? FMath::Clamp(ZoomTan / HipTan, 0.01f, 1.0f) : 1.0f;
	}

	float SnapToStep(float Value, float Min, float Max, float Step)
	{
		if (!FMath::IsFinite(Value))
		{
			return Min;
		}
		float Snapped = Value;
		if (Step > 0.0f)
		{
			// 0.001 단위면 소수 셋째 자리에서 반올림해 저장 파일에 0.41000003 같은 값이 남지 않게 한다.
			Snapped = static_cast<float>(FMath::RoundToDouble(static_cast<double>(Value) / Step) * Step);
		}
		return FMath::Clamp(Snapped, Min, Max);
	}

	float FractionToValue(float Fraction, float Min, float Max, float Step)
	{
		return SnapToStep(FMath::Lerp(Min, Max, FMath::Clamp(Fraction, 0.0f, 1.0f)), Min, Max, Step);
	}

	float ValueToFraction(float Value, float Min, float Max)
	{
		return Max > Min ? FMath::Clamp((Value - Min) / (Max - Min), 0.0f, 1.0f) : 0.0f;
	}

	void Sanitize(FValorControlSettings& Settings)
	{
		const FValorControlSettings Defaults;
		Settings.MouseSensitivity = SanitizeFloat(Settings.MouseSensitivity, MinSensitivity, MaxSensitivity, Defaults.MouseSensitivity);
		Settings.ScopedSensitivityMultiplier = SanitizeFloat(Settings.ScopedSensitivityMultiplier, MinScopedMultiplier, MaxScopedMultiplier, Defaults.ScopedSensitivityMultiplier);
	}

	void Sanitize(FValorCrosshairSettings& Settings)
	{
		const FValorCrosshairSettings Defaults;
		Settings.Color.A = 255;
		Settings.OutlineOpacity = SanitizeFloat(Settings.OutlineOpacity, 0.0f, 1.0f, Defaults.OutlineOpacity);
		Settings.OutlineThickness = SanitizeFloat(Settings.OutlineThickness, 1.0f, 6.0f, Defaults.OutlineThickness);
		Settings.CenterDotOpacity = SanitizeFloat(Settings.CenterDotOpacity, 0.0f, 1.0f, Defaults.CenterDotOpacity);
		Settings.CenterDotThickness = SanitizeFloat(Settings.CenterDotThickness, 1.0f, 6.0f, Defaults.CenterDotThickness);
		SanitizeLines(Settings.Inner, Defaults.Inner);
		SanitizeLines(Settings.Outer, Defaults.Outer);
	}

	bool TryParseHexColor(const FString& Text, FColor& OutColor)
	{
		FString Hex = Text.TrimStartAndEnd();
		Hex.RemoveFromStart(TEXT("#"));
		if (Hex.Len() != 6)
		{
			return false;
		}
		for (const TCHAR Character : Hex)
		{
			if (!FChar::IsHexDigit(Character))
			{
				return false;
			}
		}
		OutColor = FColor::FromHex(Hex);
		OutColor.A = 255;
		return true;
	}

	const TArray<FColor>& GetCrosshairColorPresets()
	{
		static const TArray<FColor> Presets = {
			FColor(255, 255, 255),
			FColor(0, 255, 0),
			FColor(127, 255, 0),
			FColor(223, 255, 0),
			FColor(255, 255, 0),
			FColor(0, 255, 255),
			FColor(255, 0, 255),
			FColor(255, 0, 0)};
		return Presets;
	}
}

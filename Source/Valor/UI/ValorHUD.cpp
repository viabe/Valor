#include "UI/ValorHUD.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/ValorCameraComponent.h"
#include "Components/ValorCombatComponent.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "ValorCharacter.h"
#include "Weapons/ValorWeaponBase.h"
#include "Weapons/ValorWeaponTypes.h"

namespace
{
	// 반동 튜닝/검증용 표시. 켜면 "다음 탄의 반동 중심(빨간 점)과 탄퍼짐 원"이 크로스헤어 기준으로 그려져
	// 힙파이어에서 탄이 크로스헤어 위로 올라가는 양을 눈으로 확인할 수 있다.
	TAutoConsoleVariable<int32> CVarValorRecoilDebug(
		TEXT("Valor.Debug.Recoil"),
		0,
		TEXT("1이면 스프레이 진행도/반동/탄퍼짐 수치와 다음 탄의 반동 위치(빨간 점)·탄퍼짐 원을 화면에 표시한다."),
		ECVF_Cheat);

	struct FCrosshairRect
	{
		float X;
		float Y;
		float Width;
		float Height;
	};
}

void AValorHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas || !PlayerOwner)
	{
		return;
	}

	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	const float UIScale = Canvas->ClipY / 1080.0f;

	// 현재 카메라 수평 FOV로 초점거리(픽셀)를 구한다. ADS 줌 중에는 같은 각도가 화면에서 더 크게 보인다.
	const float HorizontalFOV = PlayerOwner->PlayerCameraManager ? PlayerOwner->PlayerCameraManager->GetFOVAngle() : 103.0f;
	const float FocalLengthPixels = (Canvas->ClipX * 0.5f) / FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(HorizontalFOV, 5.0f, 170.0f) * 0.5f));

	const AValorCharacter* Character = Cast<AValorCharacter>(PlayerOwner->GetPawn());
	const UValorCombatComponent* CombatComponent = Character ? Character->GetCombatComponent() : nullptr;

	FValorSprayEvaluation Spray;
	FValorShooterStance Stance;
	const bool bHasWeapon = CombatComponent && CombatComponent->GetCurrentSprayEvaluation(Spray, Stance);

	// 저격총 조준 중: 조준경이 크로스헤어를 대신한다(일반 크로스헤어/ADS 점은 그리지 않는다).
	if (bHasWeapon && CombatComponent->IsScopeOverlayActive() && CombatComponent->GetEquippedWeapon())
	{
		DrawScopeOverlay(*CombatComponent->GetEquippedWeapon(), CenterX, CenterY, UIScale);
	}
	else if (bHasWeapon && Stance.bIsADS)
	{
		// ADS: 조준경 점만 표시. 카메라가 반동을 따라가므로(CameraRecoilFollowRatio=1) 이 점이 곧 탄이 향하는 곳이다.
		const float DotSize = ADSDotSize * UIScale;
		if (bShowOutline)
		{
			const float Outline = OutlineThickness * UIScale;
			DrawRect2D(FLinearColor::Black, CenterX - (DotSize * 0.5f) - Outline, CenterY - (DotSize * 0.5f) - Outline, DotSize + (Outline * 2.0f), DotSize + (Outline * 2.0f), OutlineOpacity);
		}

		DrawRect2D(CrosshairColor, CenterX - (DotSize * 0.5f), CenterY - (DotSize * 0.5f), DotSize, DotSize, 1.0f);
	}
	else
	{
		// 발로란트 크로스헤어 옵션과 같은 의미로 벌린다.
		//  - Firing Error: 연사로 늘어난 탄퍼짐(완전 회복 상태 대비 증가분).
		//  - Movement Error: 이동/점프로 늘어난 탄퍼짐.
		// 반동(탄이 위로 올라가는 양)은 선으로 표시하지 않는다 — 발로란트도 힙파이어 반동은 탄착/트레이서로만 보인다.
		const float FiringErrorPixels = bHasWeapon ? AngleToPixels(FMath::Max(0.0f, Spray.SprayErrorDegrees - Spray.RestErrorDegrees), FocalLengthPixels) * ErrorDisplayMultiplier : 0.0f;
		const float MovementErrorPixels = bHasWeapon ? AngleToPixels(Spray.MovementErrorDegrees, FocalLengthPixels) * ErrorDisplayMultiplier : 0.0f;

		const float InnerOffset = (InnerLineOffset * UIScale)
			+ (bInnerShowFiringError ? FiringErrorPixels : 0.0f)
			+ (bInnerShowMovementError ? MovementErrorPixels : 0.0f);
		const float OuterOffset = (OuterLineOffset * UIScale)
			+ (bOuterShowFiringError ? FiringErrorPixels : 0.0f)
			+ (bOuterShowMovementError ? MovementErrorPixels : 0.0f);

		DrawCrosshairLines(CenterX, CenterY, InnerLineLength * UIScale, InnerLineThickness * UIScale, InnerOffset, InnerLineOpacity);
		DrawCrosshairLines(CenterX, CenterY, OuterLineLength * UIScale, OuterLineThickness * UIScale, OuterOffset, OuterLineOpacity);
	}

	if (bHasWeapon && CVarValorRecoilDebug.GetValueOnGameThread() > 0)
	{
		// 카메라가 이미 움직인 양(패턴 추종 + 킥)을 빼야 "화면 중앙 대비 다음 탄이 실제로 떨어질 위치"가 된다.
		const UValorCameraComponent* CameraLogicComponent = Character->GetCameraLogicComponent();
		const FRotator AppliedViewOffset = CameraLogicComponent ? CameraLogicComponent->GetAppliedViewOffset() : FRotator::ZeroRotator;
		DrawRecoilDebug(CenterX, CenterY, FocalLengthPixels, Spray, Stance, AppliedViewOffset);
	}
}

void AValorHUD::DrawCrosshairLines(float CenterX, float CenterY, float Length, float Thickness, float Offset, float Opacity)
{
	if (Length <= 0.0f || Thickness <= 0.0f || Opacity <= 0.0f)
	{
		return;
	}

	const float HalfThickness = Thickness * 0.5f;
	const FCrosshairRect Lines[] =
	{
		{CenterX - HalfThickness, CenterY - Offset - Length, Thickness, Length},
		{CenterX - HalfThickness, CenterY + Offset, Thickness, Length},
		{CenterX - Offset - Length, CenterY - HalfThickness, Length, Thickness},
		{CenterX + Offset, CenterY - HalfThickness, Length, Thickness},
	};

	if (bShowOutline && OutlineThickness > 0.0f)
	{
		const float Outline = OutlineThickness * (Canvas->ClipY / 1080.0f);
		for (const FCrosshairRect& Line : Lines)
		{
			DrawRect2D(FLinearColor::Black, Line.X - Outline, Line.Y - Outline, Line.Width + (Outline * 2.0f), Line.Height + (Outline * 2.0f), OutlineOpacity * Opacity);
		}
	}

	for (const FCrosshairRect& Line : Lines)
	{
		DrawRect2D(CrosshairColor, Line.X, Line.Y, Line.Width, Line.Height, Opacity);
	}
}

void AValorHUD::DrawRect2D(const FLinearColor& Color, float X, float Y, float Width, float Height, float Opacity)
{
	FLinearColor DrawColor = Color;
	DrawColor.A *= FMath::Clamp(Opacity, 0.0f, 1.0f);

	// 픽셀 경계에 맞춰야 2px 선이 번지지 않고 또렷하게 보인다.
	DrawRect(DrawColor, FMath::RoundToFloat(X), FMath::RoundToFloat(Y), FMath::RoundToFloat(Width), FMath::RoundToFloat(Height));
}

void AValorHUD::DrawRecoilDebug(float CenterX, float CenterY, float FocalLengthPixels, const FValorSprayEvaluation& Spray, const FValorShooterStance& Stance, const FRotator& AppliedViewOffset)
{
	// 다음 탄의 반동 중심이 화면 어디에 찍히는지 = (탄의 반동 오프셋) - (카메라가 이미 움직인 양).
	// 힙파이어면 연사할수록 빨간 점이 크로스헤어 위로 올라가고(킥만큼은 덜), ADS면 점이 중앙 근처에 붙어 있다.
	const float ResidualPitch = Spray.RecoilPitchDegrees - static_cast<float>(AppliedViewOffset.Pitch);
	const float ResidualYaw = Spray.RecoilYawDegrees - static_cast<float>(AppliedViewOffset.Yaw);
	const float MarkerX = CenterX + AngleToPixels(ResidualYaw, FocalLengthPixels);
	const float MarkerY = CenterY - AngleToPixels(ResidualPitch, FocalLengthPixels);
	const float ErrorRadiusPixels = AngleToPixels(Spray.FiringErrorDegrees, FocalLengthPixels);

	constexpr int32 CircleSegments = 32;
	const FLinearColor CircleColor(1.0f, 0.35f, 0.35f, 0.8f);
	for (int32 SegmentIndex = 0; SegmentIndex < CircleSegments; ++SegmentIndex)
	{
		const float AngleA = UE_TWO_PI * static_cast<float>(SegmentIndex) / CircleSegments;
		const float AngleB = UE_TWO_PI * static_cast<float>(SegmentIndex + 1) / CircleSegments;
		DrawLine(
			MarkerX + (FMath::Cos(AngleA) * ErrorRadiusPixels), MarkerY + (FMath::Sin(AngleA) * ErrorRadiusPixels),
			MarkerX + (FMath::Cos(AngleB) * ErrorRadiusPixels), MarkerY + (FMath::Sin(AngleB) * ErrorRadiusPixels),
			CircleColor, 1.0f);
	}

	DrawRect2D(FLinearColor::Red, MarkerX - 2.0f, MarkerY - 2.0f, 4.0f, 4.0f, 1.0f);

	const FString DebugText = FString::Printf(
		TEXT("Spray %.2f | Recoil P %.2f Y %+.2f deg | Error %.2f deg (move %.2f) | %s%s%s"),
		Spray.SprayIndex,
		Spray.RecoilPitchDegrees,
		Spray.RecoilYawDegrees,
		Spray.FiringErrorDegrees,
		Spray.MovementErrorDegrees,
		Stance.bIsADS ? TEXT("ADS") : TEXT("HIP"),
		Stance.bIsCrouched ? TEXT(" CROUCH") : TEXT(""),
		Stance.bIsAirborne ? TEXT(" AIR") : TEXT(""));

	DrawText(DebugText, FLinearColor::White, CenterX - 260.0f, CenterY + 140.0f, nullptr, 1.0f);
}

float AValorHUD::AngleToPixels(float AngleDegrees, float FocalLengthPixels)
{
	return FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(AngleDegrees, -80.0f, 80.0f))) * FocalLengthPixels;
}

void AValorHUD::DrawScopeOverlay(const AValorWeaponBase& Weapon, float CenterX, float CenterY, float UIScale)
{
	// 1) 머티리얼 조준경: 아트에서 만든 조준경(마스크·눈금·흠집 등)을 화면 전체에 그린다.
	if (UMaterialInterface* ScopeMaterial = Weapon.GetScopeOverlayMaterial())
	{
		DrawMaterial(ScopeMaterial, 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY, 0.0f, 0.0f, 1.0f, 1.0f);
		return;
	}

	// 2) 기본 조준경(에셋 없이): 화면 중앙 원 밖을 검은 고리(삼각형 띠)로 덮는다.
	//    바깥 반지름은 화면 대각선보다 크게 잡아 모서리까지 빈틈 없이 덮는다.
	const float InnerRadius = Canvas->ClipY * ScopeRadiusRatio;
	const float OuterRadius = FMath::Sqrt(FMath::Square(Canvas->ClipX) + FMath::Square(Canvas->ClipY));
	const int32 Segments = FMath::Clamp(ScopeCircleSegments, 16, 256);
	const FLinearColor MaskColor = FLinearColor::Black;

	TArray<FCanvasUVTri> MaskTriangles;
	MaskTriangles.Reserve(Segments * 2);
	for (int32 SegmentIndex = 0; SegmentIndex < Segments; ++SegmentIndex)
	{
		const float AngleA = UE_TWO_PI * static_cast<float>(SegmentIndex) / Segments;
		const float AngleB = UE_TWO_PI * static_cast<float>(SegmentIndex + 1) / Segments;
		const FVector2D DirectionA(FMath::Cos(AngleA), FMath::Sin(AngleA));
		const FVector2D DirectionB(FMath::Cos(AngleB), FMath::Sin(AngleB));
		const FVector2D Center(CenterX, CenterY);
		const FVector2D InnerA = Center + DirectionA * InnerRadius;
		const FVector2D InnerB = Center + DirectionB * InnerRadius;
		const FVector2D OuterA = Center + DirectionA * OuterRadius;
		const FVector2D OuterB = Center + DirectionB * OuterRadius;

		auto AddTriangle = [&MaskTriangles, &MaskColor](const FVector2D& A, const FVector2D& B, const FVector2D& C)
		{
			FCanvasUVTri& Triangle = MaskTriangles.AddDefaulted_GetRef();
			Triangle.V0_Pos = A;
			Triangle.V1_Pos = B;
			Triangle.V2_Pos = C;
			Triangle.V0_Color = MaskColor;
			Triangle.V1_Color = MaskColor;
			Triangle.V2_Color = MaskColor;
		};
		AddTriangle(InnerA, OuterA, OuterB);
		AddTriangle(InnerA, OuterB, InnerB);
	}
	Canvas->K2_DrawTriangle(nullptr, MaskTriangles);

	// 3) 조준선: 원 가장자리에서 중앙 쪽으로 가는 네 줄(가운데는 비워 탄착 지점을 가리지 않는다) + 정중앙 점.
	const float Thickness = FMath::Max(1.0f, ScopeReticleThickness * UIScale);
	const float HalfThickness = Thickness * 0.5f;
	const float Gap = ScopeReticleCenterGap * UIScale;
	const float LineLength = FMath::Max(InnerRadius - Gap, 0.0f);
	DrawRect2D(ScopeReticleColor, CenterX - InnerRadius, CenterY - HalfThickness, LineLength, Thickness, 1.0f);
	DrawRect2D(ScopeReticleColor, CenterX + Gap, CenterY - HalfThickness, LineLength, Thickness, 1.0f);
	DrawRect2D(ScopeReticleColor, CenterX - HalfThickness, CenterY - InnerRadius, Thickness, LineLength, 1.0f);
	DrawRect2D(ScopeReticleColor, CenterX - HalfThickness, CenterY + Gap, Thickness, LineLength, 1.0f);

	const float DotSize = FMath::Max(1.0f, ScopeCenterDotSize * UIScale);
	DrawRect2D(CrosshairColor, CenterX - (DotSize * 0.5f), CenterY - (DotSize * 0.5f), DotSize, DotSize, 1.0f);
}

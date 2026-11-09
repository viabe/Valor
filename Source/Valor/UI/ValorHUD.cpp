#include "UI/ValorHUD.h"

#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "Camera/PlayerCameraManager.h"
#include "CanvasItem.h"
#include "Components/ValorCameraComponent.h"
#include "Components/ValorCombatComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "GlobalRenderResources.h"
#include "HAL/IConsoleManager.h"
#include "Player/ValorPlayerController.h"
#include "Player/ValorPlayerState.h"
#include "ValorCharacter.h"
#include "ValorGameState.h"
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

	TAutoConsoleVariable<int32> CVarValorHitMarker(
		TEXT("Valor.HUD.HitMarker"),
		1,
		TEXT("1이면 서버가 확정한 명중을 크로스헤어 주변 X 표시로 보여 준다(몸 흰색 / 머리 노란색 / 처치 빨간색). 발로란트 기본 HUD에는 없는 개발용 표시."),
		ECVF_Default);

	// 무기 클래스 → 표시 이름(데이터 에셋의 DisplayName). 킬피드/사망 화면은 클래스만 받으므로 여기서 이름을 찾는다.
	FString GetWeaponDisplayName(const TSubclassOf<AValorWeaponBase>& WeaponClass)
	{
		const AValorWeaponBase* WeaponDefaults = WeaponClass ? WeaponClass->GetDefaultObject<AValorWeaponBase>() : nullptr;
		return WeaponDefaults ? WeaponDefaults->GetWeaponConfig().DisplayName.ToString() : FString();
	}

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

	// 죽은 동안(부활 전까지 시체에 빙의된 상태)은 사망 화면이 크로스헤어·체력/탄약 표시를 대신한다.
	const bool bLocalDead = Character && !Character->IsAlive();

	FValorSprayEvaluation Spray;
	FValorShooterStance Stance;
	const bool bHasWeapon = !bLocalDead && CombatComponent && CombatComponent->GetCurrentSprayEvaluation(Spray, Stance);

	if (bLocalDead)
	{
		DrawDeathOverlay(CenterX, CenterY, UIScale);
	}
	// 저격총 조준 중: 조준경이 크로스헤어를 대신한다(일반 크로스헤어/ADS 점은 그리지 않는다).
	else if (bHasWeapon && CombatComponent->IsScopeOverlayActive() && CombatComponent->GetEquippedWeapon())
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

	// 전투 피드백. 피격 방향은 죽는 순간에도(마지막 피격) 보이도록 사망 여부와 관계없이 그린다.
	DrawDamageIndicators(CenterX, CenterY, UIScale);
	if (!bLocalDead)
	{
		DrawHitMarker(CenterX, CenterY, UIScale);
		if (Character)
		{
			DrawVitals(*Character, CombatComponent, CenterX, UIScale);
		}
	}

	DrawKillFeed(UIScale);
	DrawScore(CenterX, UIScale);
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

float AValorHUD::DrawShadowedText(const FString& Text, const FLinearColor& Color, float X, float Y, UFont* Font, float Scale, float AlignX)
{
	if (Text.IsEmpty() || !Canvas)
	{
		return 0.0f;
	}

	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	GetTextSize(Text, TextWidth, TextHeight, Font, Scale);

	const float DrawX = FMath::RoundToFloat(X - (TextWidth * AlignX));
	const float DrawY = FMath::RoundToFloat(Y);
	const float ShadowOffset = FMath::Max(1.0f, FMath::RoundToFloat(Scale));
	DrawText(Text, FLinearColor(0.0f, 0.0f, 0.0f, Color.A * 0.75f), DrawX + ShadowOffset, DrawY + ShadowOffset, Font, Scale);
	DrawText(Text, Color, DrawX, DrawY, Font, Scale);
	return TextHeight;
}

void AValorHUD::DrawHitMarker(float CenterX, float CenterY, float UIScale)
{
	const AValorPlayerController* ValorController = Cast<AValorPlayerController>(PlayerOwner);
	const UWorld* World = GetWorld();
	if (!ValorController || !World || CVarValorHitMarker.GetValueOnGameThread() <= 0)
	{
		return;
	}

	// 서버가 확정한 명중만 표시한다(클라 예측 탄흔은 "맞았을 것"이고 이 표시는 "맞았다"). RTT만큼 늦게 뜨는 것이 정상이다.
	const FValorHitConfirm& HitConfirm = ValorController->GetLastHitConfirm();
	const float Duration = HitConfirm.bKilled ? KillHitMarkerDuration : HitMarkerDuration;
	const float Age = static_cast<float>(World->GetTimeSeconds() - ValorController->GetLastHitConfirmTime());
	if (Age < 0.0f || Age > Duration)
	{
		return;
	}

	FLinearColor Color = HitConfirm.bKilled ? KillHitColor : (HitConfirm.HitZone == EValorHitZone::Head ? HeadHitColor : BodyHitColor);
	Color.A *= 1.0f - (Age / Duration);

	const float Gap = HitMarkerGap * UIScale;
	const float Length = HitMarkerLength * UIScale * (HitConfirm.bKilled ? 1.4f : 1.0f);
	const float Thickness = FMath::Max(1.5f, 2.0f * UIScale);
	const float Diagonal = UE_INV_SQRT_2;
	const FVector2D Directions[] = {FVector2D(-1.0, -1.0), FVector2D(1.0, -1.0), FVector2D(-1.0, 1.0), FVector2D(1.0, 1.0)};
	for (const FVector2D& Direction : Directions)
	{
		const FVector2D Start = FVector2D(CenterX, CenterY) + Direction * (Gap * Diagonal);
		const FVector2D End = FVector2D(CenterX, CenterY) + Direction * ((Gap + Length) * Diagonal);
		DrawLine(Start.X, Start.Y, End.X, End.Y, Color, Thickness);
	}
}

void AValorHUD::DrawDamageIndicators(float CenterX, float CenterY, float UIScale)
{
	const AValorPlayerController* ValorController = Cast<AValorPlayerController>(PlayerOwner);
	const UWorld* World = GetWorld();
	if (!ValorController || !World || ValorController->GetDamageIndicators().Num() == 0)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerOwner->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const double Now = World->GetTimeSeconds();
	const float InnerRadius = DamageIndicatorRadius * UIScale;
	const float OuterRadius = InnerRadius + (DamageIndicatorThickness * UIScale);
	constexpr int32 Segments = 10;

	// 화면 각도 a(0 = 정면 = 화면 위, 시계 방향 +)의 반지름 r 지점.
	auto ScreenPoint = [CenterX, CenterY](float AngleDegrees, float Radius)
	{
		const float Radians = FMath::DegreesToRadians(AngleDegrees);
		return FVector2D(CenterX + (FMath::Sin(Radians) * Radius), CenterY - (FMath::Cos(Radians) * Radius));
	};

	TArray<FCanvasUVTri> Triangles;
	for (const AValorPlayerController::FDamageIndicator& Indicator : ValorController->GetDamageIndicators())
	{
		const float Age = static_cast<float>(Now - Indicator.LocalTime);
		if (Age < 0.0f || Age > DamageIndicatorDuration)
		{
			continue;
		}

		// 수평면에서 "내가 보는 방향 대비 피해가 온 방향". 고개를 돌리면 표시도 같이 돈다(발로란트 피격 표시와 같은 동작).
		const FVector ToSource = Indicator.SourceLocation - ViewLocation;
		if (ToSource.SizeSquared2D() < 1.0)
		{
			continue;
		}

		const float SourceYaw = FMath::RadiansToDegrees(FMath::Atan2(ToSource.Y, ToSource.X));
		const float RelativeYaw = FMath::FindDeltaAngleDegrees(static_cast<float>(ViewRotation.Yaw), SourceYaw);

		FLinearColor Color = DamageIndicatorColor;
		Color.A *= 1.0f - (Age / DamageIndicatorDuration);

		for (int32 SegmentIndex = 0; SegmentIndex < Segments; ++SegmentIndex)
		{
			const float AngleA = RelativeYaw - (DamageIndicatorArcDegrees * 0.5f) + (DamageIndicatorArcDegrees * SegmentIndex / Segments);
			const float AngleB = AngleA + (DamageIndicatorArcDegrees / Segments);
			const FVector2D InnerA = ScreenPoint(AngleA, InnerRadius);
			const FVector2D InnerB = ScreenPoint(AngleB, InnerRadius);
			const FVector2D OuterA = ScreenPoint(AngleA, OuterRadius);
			const FVector2D OuterB = ScreenPoint(AngleB, OuterRadius);

			auto AddTriangle = [&Triangles, &Color](const FVector2D& A, const FVector2D& B, const FVector2D& C)
			{
				FCanvasUVTri& Triangle = Triangles.AddDefaulted_GetRef();
				Triangle.V0_Pos = A;
				Triangle.V1_Pos = B;
				Triangle.V2_Pos = C;
				Triangle.V0_Color = Color;
				Triangle.V1_Color = Color;
				Triangle.V2_Color = Color;
			};
			AddTriangle(InnerA, OuterA, OuterB);
			AddTriangle(InnerA, OuterB, InnerB);
		}
	}

	if (Triangles.Num() > 0)
	{
		// 반투명으로 그려야 시간이 지나며 흐려진다(UCanvas::K2_DrawTriangle은 불투명 블렌드라 직접 아이템을 만든다).
		FCanvasTriangleItem TriangleItem(FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, GWhiteTexture);
		TriangleItem.TriangleList = MoveTemp(Triangles);
		TriangleItem.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(TriangleItem);
	}
}

void AValorHUD::DrawVitals(const AValorCharacter& Character, const UValorCombatComponent* CombatComponent, float CenterX, float UIScale)
{
	const UValorCombatAttributeSet* Attributes = Character.GetCombatAttributeSet();
	if (!Attributes || !GEngine)
	{
		return;
	}

	UFont* LargeFont = GEngine->GetLargeFont();
	UFont* MediumFont = GEngine->GetMediumFont();
	UFont* SmallFont = GEngine->GetSmallFont();
	const float NumberScale = 2.0f * UIScale;
	const float BaseY = Canvas->ClipY - (110.0f * UIScale);
	const FLinearColor LabelColor(0.75f, 0.75f, 0.75f, 0.9f);

	// 체력(큰 숫자)과 실드(왼쪽 작은 숫자). 발로란트처럼 화면 아래 가운데 왼쪽에 둔다.
	const int32 Health = FMath::CeilToInt(Attributes->GetHealth());
	const FString HealthText = FString::FromInt(Health);
	const FLinearColor HealthColor = Health <= 30 ? FLinearColor(1.0f, 0.35f, 0.35f, 1.0f) : FLinearColor::White;
	const float HealthRightX = CenterX - (200.0f * UIScale);

	float HealthWidth = 0.0f;
	float HealthHeight = 0.0f;
	GetTextSize(HealthText, HealthWidth, HealthHeight, LargeFont, NumberScale);
	DrawShadowedText(HealthText, HealthColor, HealthRightX, BaseY, LargeFont, NumberScale, 1.0f);
	DrawShadowedText(TEXT("HP"), LabelColor, HealthRightX, BaseY + HealthHeight, SmallFont, UIScale, 1.0f);

	if (Attributes->GetMaxShield() > 0.0f)
	{
		const float ShieldRightX = HealthRightX - HealthWidth - (20.0f * UIScale);
		const FLinearColor ShieldColor(0.55f, 0.85f, 1.0f, 1.0f);
		const float ShieldScale = 1.3f * UIScale;
		DrawShadowedText(FString::FromInt(FMath::CeilToInt(Attributes->GetShield())), ShieldColor, ShieldRightX, BaseY + (HealthHeight * 0.3f), LargeFont, ShieldScale, 1.0f);
		DrawShadowedText(TEXT("SHIELD"), LabelColor, ShieldRightX, BaseY + HealthHeight, SmallFont, UIScale, 1.0f);
	}

	// 탄약(탄창 큰 숫자 / 예비 작은 숫자)과 무기 이름. 탄창은 소유 클라 예측값이라 쏘는 즉시 줄어든다.
	if (!CombatComponent || !CombatComponent->GetEquippedWeapon())
	{
		return;
	}

	const float AmmoLeftX = CenterX + (200.0f * UIScale);
	const int32 MagazineAmmo = CombatComponent->GetDisplayedMagazineAmmo();
	const FString MagazineText = FString::FromInt(MagazineAmmo);
	const FLinearColor MagazineColor = MagazineAmmo == 0 ? FLinearColor(1.0f, 0.35f, 0.35f, 1.0f) : FLinearColor::White;

	float MagazineWidth = 0.0f;
	float MagazineHeight = 0.0f;
	GetTextSize(MagazineText, MagazineWidth, MagazineHeight, LargeFont, NumberScale);
	DrawShadowedText(MagazineText, MagazineColor, AmmoLeftX, BaseY, LargeFont, NumberScale, 0.0f);
	DrawShadowedText(FString::Printf(TEXT("/ %d"), CombatComponent->GetDisplayedReserveAmmo()), LabelColor,
		AmmoLeftX + MagazineWidth + (10.0f * UIScale), BaseY + (MagazineHeight * 0.45f), MediumFont, 1.2f * UIScale, 0.0f);

	float LabelY = BaseY + MagazineHeight;
	LabelY += DrawShadowedText(CombatComponent->GetEquippedWeaponDisplayName().ToString().ToUpper(), LabelColor, AmmoLeftX, LabelY, SmallFont, UIScale, 0.0f);
	if (CombatComponent->IsReloading())
	{
		DrawShadowedText(TEXT("RELOADING"), FLinearColor(1.0f, 0.85f, 0.3f, 1.0f), AmmoLeftX, LabelY, SmallFont, UIScale, 0.0f);
	}
}

void AValorHUD::DrawKillFeed(float UIScale)
{
	const UWorld* World = GetWorld();
	const AValorGameState* ValorGameState = World ? World->GetGameState<AValorGameState>() : nullptr;
	if (!ValorGameState || !GEngine || ValorGameState->GetKillFeed().Num() == 0)
	{
		return;
	}

	// 내가 관련된 줄은 배경색으로 구분한다(처치 = 초록, 사망 = 빨강) — 발로란트 킬피드와 같은 읽는 법.
	const FString LocalName = PlayerOwner && PlayerOwner->PlayerState ? PlayerOwner->PlayerState->GetPlayerName() : FString();
	UFont* Font = GEngine->GetMediumFont();
	const float Padding = 6.0f * UIScale;
	const float RightX = Canvas->ClipX - (24.0f * UIScale);
	float Y = 80.0f * UIScale;
	const double Now = World->GetTimeSeconds();

	const TArray<AValorGameState::FKillFeedRecord>& KillFeed = ValorGameState->GetKillFeed();
	for (int32 RecordIndex = KillFeed.Num() - 1; RecordIndex >= 0; --RecordIndex)
	{
		const AValorGameState::FKillFeedRecord& Record = KillFeed[RecordIndex];
		const float Age = static_cast<float>(Now - Record.LocalTime);
		if (Age < 0.0f || Age > KillFeedDuration)
		{
			continue;
		}

		// 마지막 1초 동안 흐려지며 사라진다.
		const float Alpha = FMath::Clamp(KillFeedDuration - Age, 0.0f, 1.0f);
		const FValorKillFeedEntry& Entry = Record.Entry;

		FString WeaponTag = Record.WeaponDisplayName.ToUpper();
		if (Entry.bHeadshot)
		{
			WeaponTag += WeaponTag.IsEmpty() ? TEXT("HEADSHOT") : TEXT(" + HEADSHOT");
		}
		if (Entry.bWallbang)
		{
			WeaponTag += WeaponTag.IsEmpty() ? TEXT("WALLBANG") : TEXT(" + WALLBANG");
		}

		FString Line = Entry.KillerName.IsEmpty() ? FString() : Entry.KillerName + TEXT("   ");
		if (!WeaponTag.IsEmpty())
		{
			Line += FString::Printf(TEXT("[%s]   "), *WeaponTag);
		}
		Line += Entry.VictimName;

		FLinearColor Background(0.0f, 0.0f, 0.0f, 0.55f);
		if (!LocalName.IsEmpty() && Entry.KillerName == LocalName)
		{
			Background = FLinearColor(0.1f, 0.45f, 0.25f, 0.7f);
		}
		else if (!LocalName.IsEmpty() && Entry.VictimName == LocalName)
		{
			Background = FLinearColor(0.55f, 0.1f, 0.1f, 0.7f);
		}

		float LineWidth = 0.0f;
		float LineHeight = 0.0f;
		GetTextSize(Line, LineWidth, LineHeight, Font, UIScale);
		DrawRect2D(Background, RightX - LineWidth - (Padding * 2.0f), Y - Padding, LineWidth + (Padding * 2.0f), LineHeight + (Padding * 2.0f), Alpha);
		DrawShadowedText(Line, FLinearColor(1.0f, 1.0f, 1.0f, Alpha), RightX - Padding, Y, Font, UIScale, 1.0f);
		Y += LineHeight + (Padding * 2.0f) + (4.0f * UIScale);
	}
}

void AValorHUD::DrawScore(float CenterX, float UIScale)
{
	const AValorPlayerState* ValorPlayerState = PlayerOwner ? PlayerOwner->GetPlayerState<AValorPlayerState>() : nullptr;
	if (!ValorPlayerState || !GEngine)
	{
		return;
	}

	const FString ScoreText = FString::Printf(TEXT("K %d   D %d   HS %d"), ValorPlayerState->GetKills(), ValorPlayerState->GetDeaths(), ValorPlayerState->GetHeadshotKills());
	DrawShadowedText(ScoreText, FLinearColor(1.0f, 1.0f, 1.0f, 0.9f), CenterX, 24.0f * UIScale, GEngine->GetMediumFont(), UIScale, 0.5f);
}

void AValorHUD::DrawDeathOverlay(float CenterX, float CenterY, float UIScale)
{
	if (!GEngine)
	{
		return;
	}

	DrawRect2D(FLinearColor::Black, 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY, 0.35f);

	FString Title = TEXT("YOU DIED");
	FString Detail;
	float RemainingSeconds = -1.0f;

	const AValorPlayerController* ValorController = Cast<AValorPlayerController>(PlayerOwner);
	if (ValorController && ValorController->HasDeathInfo())
	{
		const FValorDeathInfo& DeathInfo = ValorController->GetLastDeathInfo();
		if (!DeathInfo.KillerName.IsEmpty())
		{
			Title = FString::Printf(TEXT("KILLED BY %s"), *DeathInfo.KillerName);
		}

		Detail = GetWeaponDisplayName(DeathInfo.WeaponClass).ToUpper();
		if (DeathInfo.bHeadshot)
		{
			Detail += Detail.IsEmpty() ? TEXT("HEADSHOT") : TEXT("  |  HEADSHOT");
		}

		// 부활 시각은 서버 시간으로 왔으므로 GameState의 서버 시간 추정치로 남은 시간을 계산한다(클라 월드 시간과 축이 다르다).
		if (const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr)
		{
			RemainingSeconds = FMath::Max(0.0f, DeathInfo.RespawnServerTime - static_cast<float>(GameState->GetServerWorldTimeSeconds()));
		}
	}

	float Y = CenterY - (140.0f * UIScale);
	Y += DrawShadowedText(Title, FLinearColor(1.0f, 0.35f, 0.35f, 1.0f), CenterX, Y, GEngine->GetLargeFont(), 1.6f * UIScale, 0.5f);
	Y += 6.0f * UIScale;
	Y += DrawShadowedText(Detail, FLinearColor::White, CenterX, Y, GEngine->GetMediumFont(), 1.2f * UIScale, 0.5f);
	if (RemainingSeconds >= 0.0f)
	{
		Y += 10.0f * UIScale;
		DrawShadowedText(FString::Printf(TEXT("RESPAWN IN %.1f"), RemainingSeconds), FLinearColor(0.85f, 0.85f, 0.85f, 1.0f), CenterX, Y, GEngine->GetMediumFont(), UIScale, 0.5f);
	}
}

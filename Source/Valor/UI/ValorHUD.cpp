#include "UI/ValorHUD.h"

#include "Components/ValorCharacterMovementComponent.h"

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
#include "AbilitySystemComponent.h"
#include "ValorMatchTypes.h"
#include "EngineUtils.h"
#include "World/ValorSpike.h"
#include "AbilitySystem/ValorAgentTypes.h"
#include "World/ValorSpikeSite.h"

namespace
{
	// 반동 튜닝/검증용 표시. 켜면 "다음 탄의 반동 중심(빨간 점)과 탄퍼짐 원"이 크로스헤어 기준으로 그려져
	// 힙파이어에서 탄이 크로스헤어 위로 올라가는 양을 눈으로 확인할 수 있다.
	TAutoConsoleVariable<int32> CVarValorRecoilDebug(
		TEXT("Valor.Debug.Recoil"),
		0,
		TEXT("1이면 스프레이 진행도/반동/탄퍼짐 수치와 다음 탄의 반동 위치(빨간 점)·탄퍼짐 원을 화면에 표시한다."),
		ECVF_Cheat);

	TAutoConsoleVariable<float> CVarValorHUDTextScale(
		TEXT("Valor.HUD.TextScale"),
		0.0f,
		TEXT("HUD 글자·패널 배율(1080p 기준 대비). 0이면 HUD 기본값(TextScale)을 쓴다."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarValorTagDebug(
		TEXT("Valor.HUD.TagDebug"),
		1,
		TEXT("개발 빌드: 1이면 피격 감속 중에 크로스헤어 아래에 지금 속도 비율(SLOWED)과 위치 보정 횟수를 보여 준다. 0이면 끈다."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarValorHitMarker(
		TEXT("Valor.HUD.HitMarker"),
		1,
		TEXT("1이면 서버가 확정한 명중을 크로스헤어 주변 X 표시로 보여 준다(몸 흰색 / 머리 노란색 / 처치 빨간색). 발로란트 기본 HUD에는 없는 개발용 표시."),
		ECVF_Default);

	// 무기 클래스(총·칼) → 표시 이름. 킬피드/사망 화면은 클래스만 받으므로 여기서 이름을 찾는다.
	FString GetWeaponDisplayName(const UClass* WeaponClass)
	{
		return ValorWeaponDisplay::GetDisplayName(WeaponClass);
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

	// 글자가 있는 HUD는 따로 키운다: 창이 작아도 MinTextUIScale 아래로 줄지 않고, 그 위에 글자 배율을 곱한다.
	const float TextScaleOverride = CVarValorHUDTextScale.GetValueOnGameThread();
	const float TextUIScale = FMath::Max(UIScale, MinTextUIScale) * (TextScaleOverride > 0.0f ? TextScaleOverride : TextScale);

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
		DrawDeathOverlay(CenterX, CenterY, TextUIScale);
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
			DrawVitals(*Character, CombatComponent, CenterX, TextUIScale);
			DrawTagDebug(*Character, CenterX, CenterY, TextUIScale);
		}
	}

	DrawKillFeed(TextUIScale);
	DrawScore(CenterX, TextUIScale);

	if (UsesRounds())
	{
		DrawRevealedEnemies(TextUIScale);
		DrawSpikeStatus(CenterX, CenterY, TextUIScale);
		DrawMatchStatus(CenterX, TextUIScale);
		DrawRoundBanner(CenterX, CenterY, TextUIScale);
		if (!bLocalDead)
		{
			DrawCredits(TextUIScale);
			DrawAbilityBar(CenterX, TextUIScale);
		}
		DrawBuyMenu(CenterX, TextUIScale);
		DrawAgentSelect(CenterX, TextUIScale);
	}

	// 섬광 눈부심은 모든 HUD 위에 덮는다(맨 마지막).
	DrawFlashOverlay();
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

void AValorHUD::DrawTagDebug(const AValorCharacter& Character, float CenterX, float CenterY, float UIScale)
{
#if !UE_BUILD_SHIPPING
	const UValorCharacterMovementComponent* Movement = Character.GetValorMovement();
	if (!Movement || CVarValorTagDebug.GetValueOnGameThread() <= 0)
	{
		return;
	}

	const AValorPlayerController* ValorController = Cast<AValorPlayerController>(PlayerOwner);
	const bool bLoopOn = ValorController && ValorController->IsDebugTagLoopRequested();
	const float Multiplier = Movement->GetTagSpeedMultiplier();
	if (Multiplier >= 0.999f && !bLoopOn)
	{
		return;
	}

	// 위치 보정 횟수는 원격 클라에서만 의미가 있다(서버·리슨 호스트는 보정을 받지 않는다).
	FString Line = Multiplier < 0.999f ? FString::Printf(TEXT("SLOWED  %d%%"), FMath::RoundToInt(Multiplier * 100.0f)) : FString(TEXT("TAG TEST ON"));
	if (!Character.HasAuthority())
	{
		Line += FString::Printf(TEXT("   |   CORRECTIONS %d"), Movement->GetClientCorrectionCount());
	}
	const FLinearColor Color = Multiplier < 0.999f ? FLinearColor(1.0f, 0.55f, 0.15f) : FLinearColor(0.75f, 0.75f, 0.75f);
	DrawShadowedText(Line, Color, CenterX, CenterY + (70.0f * UIScale), GEngine->GetSmallFont(), UIScale, 0.5f);
#endif
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

	// 작은 창(PIE 등)에서 1080p 기준 크기를 그대로 줄이면 3~4px로 크로스헤어 안에 묻혀 보이지 않는다. 1080p의 75% 밑으로는 줄이지 않는다.
	const float MarkerScale = FMath::Max(UIScale, 0.75f);
	const float Gap = HitMarkerGap * MarkerScale;
	const float Length = HitMarkerLength * MarkerScale * (HitConfirm.bKilled ? 1.4f : 1.0f);
	const float Thickness = FMath::Max(2.0f, 2.5f * MarkerScale);
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

	if (!CombatComponent)
	{
		return;
	}

	DrawWeaponSlots(*CombatComponent, UIScale);

	const float AmmoLeftX = CenterX + (200.0f * UIScale);
	const FLinearColor StatusColor(1.0f, 0.85f, 0.3f, 1.0f);

	// 칼: 탄약 대신 이름만 크게 보여 준다.
	if (!CombatComponent->GetEquippedWeapon())
	{
		const FString MeleeName = CombatComponent->GetSlotDisplayName(EValorWeaponSlot::Melee).ToString().ToUpper();
		const float NameHeight = DrawShadowedText(MeleeName, FLinearColor::White, AmmoLeftX, BaseY, MediumFont, 1.6f * UIScale, 0.0f);
		if (!CombatComponent->IsEquipReady())
		{
			DrawShadowedText(TEXT("EQUIPPING"), StatusColor, AmmoLeftX, BaseY + NameHeight, SmallFont, UIScale, 0.0f);
		}

		return;
	}

	// 탄약(탄창 큰 숫자 / 예비 작은 숫자)과 무기 이름. 탄창은 소유 클라 예측값이라 쏘는 즉시 줄어든다.
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
		DrawShadowedText(TEXT("RELOADING"), StatusColor, AmmoLeftX, LabelY, SmallFont, UIScale, 0.0f);
	}
	else if (!CombatComponent->IsEquipReady())
	{
		DrawShadowedText(TEXT("EQUIPPING"), StatusColor, AmmoLeftX, LabelY, SmallFont, UIScale, 0.0f);
	}
}

void AValorHUD::DrawWeaponSlots(const UValorCombatComponent& CombatComponent, float UIScale)
{
	if (!GEngine)
	{
		return;
	}

	struct FSlotRow
	{
		EValorWeaponSlot Slot;
		const TCHAR* Key;
	};

	static constexpr FSlotRow Rows[] =
	{
		{EValorWeaponSlot::Primary, TEXT("1")},
		{EValorWeaponSlot::Secondary, TEXT("2")},
		{EValorWeaponSlot::Melee, TEXT("3")},
	};

	UFont* Font = GEngine->GetSmallFont();
	const float RightX = Canvas->ClipX - (32.0f * UIScale);
	const float RowHeight = 22.0f * UIScale;
	const EValorWeaponSlot ActiveSlot = CombatComponent.GetActiveSlot();
	float Y = Canvas->ClipY - (190.0f * UIScale);

	for (const FSlotRow& Row : Rows)
	{
		const FText SlotName = CombatComponent.GetSlotDisplayName(Row.Slot);
		if (SlotName.IsEmpty())
		{
			continue;
		}

		const bool bActive = Row.Slot == ActiveSlot;
		const FLinearColor RowColor = bActive ? FLinearColor::White : FLinearColor(0.6f, 0.6f, 0.6f, 0.75f);
		const float RowTextScale = (bActive ? 1.15f : 1.0f) * UIScale;
		DrawShadowedText(FString::Printf(TEXT("%s   %s"), *SlotName.ToString().ToUpper(), Row.Key), RowColor, RightX, Y, Font, RowTextScale, 1.0f);

		// 장착 중: 든 슬롯 아래에 진행 막대(장착 시간 동안 채워진다). 다 차야 쏠 수 있다.
		if (bActive && !CombatComponent.IsEquipReady())
		{
			const float BarWidth = 90.0f * UIScale;
			const float BarHeight = 2.0f * UIScale;
			const float BarY = Y + RowHeight - (3.0f * UIScale);
			DrawRect2D(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), RightX - BarWidth, BarY, BarWidth, BarHeight, 1.0f);
			DrawRect2D(FLinearColor(1.0f, 0.85f, 0.3f, 1.0f), RightX - BarWidth, BarY, BarWidth * CombatComponent.GetEquipProgress(), BarHeight, 1.0f);
		}

		Y += RowHeight;
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
	// 경기 모드에서는 위 가운데를 점수판이 쓰므로 그 아래로 내린다.
	const float ScoreY = (UsesRounds() ? 112.0f : 24.0f) * UIScale;
	DrawShadowedText(ScoreText, FLinearColor(1.0f, 1.0f, 1.0f, 0.9f), CenterX, ScoreY, GEngine->GetMediumFont(), UIScale, 0.5f);
}

void AValorHUD::DrawDeathOverlay(float CenterX, float CenterY, float UIScale)
{
	if (!GEngine)
	{
		return;
	}

	// 관전 중: 화면을 어둡게 덮지 않고 아래쪽에 누구를 보는지만 띄운다.
	const AValorPlayerController* SpectatingController = Cast<AValorPlayerController>(PlayerOwner);
	if (SpectatingController && SpectatingController->IsSpectating())
	{
		const AValorCharacter* Target = SpectatingController->GetSpectateTarget();
		const UAbilitySystemComponent* TargetAbilitySystem = Target ? Target->GetAbilitySystemComponent() : nullptr;
		const float TargetHealth = TargetAbilitySystem ? TargetAbilitySystem->GetNumericAttribute(UValorCombatAttributeSet::GetHealthAttribute()) : 0.0f;
		const FString TargetName = Target && Target->GetPlayerState() ? Target->GetPlayerState()->GetPlayerName() : FString(TEXT("?"));
		const float BannerY = Canvas->ClipY - (115.0f * UIScale);
		DrawRect2D(FLinearColor::Black, CenterX - (260.0f * UIScale), BannerY - (8.0f * UIScale), 520.0f * UIScale, 70.0f * UIScale, 0.45f);
		float Y = BannerY;
		Y += DrawShadowedText(FString::Printf(TEXT("SPECTATING  %s  (%.0f HP)"), *TargetName, TargetHealth), FLinearColor(0.35f, 0.95f, 0.8f, 1.0f), CenterX, Y, GEngine->GetMediumFont(), 1.2f * UIScale, 0.5f);
		DrawShadowedText(TEXT("LEFT CLICK: NEXT   |   RIGHT CLICK: PREVIOUS"), FLinearColor(0.85f, 0.85f, 0.85f, 1.0f), CenterX, Y + (4.0f * UIScale), GEngine->GetSmallFont(), UIScale, 0.5f);
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
	const bool bWillRespawn = ValorController && ValorController->HasDeathInfo() && ValorController->GetLastDeathInfo().RespawnServerTime >= 0.0f;
	if (bWillRespawn && RemainingSeconds >= 0.0f)
	{
		Y += 10.0f * UIScale;
		DrawShadowedText(FString::Printf(TEXT("RESPAWN IN %.1f"), RemainingSeconds), FLinearColor(0.85f, 0.85f, 0.85f, 1.0f), CenterX, Y, GEngine->GetMediumFont(), UIScale, 0.5f);
	}
	else if (!bWillRespawn)
	{
		// 라운드제: 다음 라운드 시작에 다시 스폰된다.
		Y += 10.0f * UIScale;
		DrawShadowedText(TEXT("WAITING FOR NEXT ROUND"), FLinearColor(0.85f, 0.85f, 0.85f, 1.0f), CenterX, Y, GEngine->GetMediumFont(), UIScale, 0.5f);
	}
}

// ======================================================================================
// 경기 모드(라운드제) 화면
// ======================================================================================

namespace ValorMatchHUD
{
	const FLinearColor AllyColor(0.35f, 0.95f, 0.8f, 1.0f);
	const FLinearColor EnemyColor(1.0f, 0.35f, 0.35f, 1.0f);

	FString FormatTime(float Seconds)
	{
		const int32 Total = FMath::CeilToInt(FMath::Max(Seconds, 0.0f));
		return FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
	}

	const TCHAR* CategoryLabel(EValorWeaponCategory Category)
	{
		switch (Category)
		{
		case EValorWeaponCategory::Sidearm: return TEXT("SIDEARMS");
		case EValorWeaponCategory::SMG: return TEXT("SMGS");
		case EValorWeaponCategory::Shotgun: return TEXT("SHOTGUNS");
		case EValorWeaponCategory::Rifle: return TEXT("RIFLES");
		case EValorWeaponCategory::Sniper: return TEXT("SNIPERS");
		case EValorWeaponCategory::Heavy: return TEXT("HEAVIES");
		default: return TEXT("");
		}
	}
}

bool AValorHUD::UsesRounds() const
{
	const AValorGameState* ValorGameState = GetWorld() ? GetWorld()->GetGameState<AValorGameState>() : nullptr;
	return ValorGameState && ValorGameState->UsesRounds();
}

void AValorHUD::DrawMatchStatus(float CenterX, float UIScale)
{
	const AValorGameState* ValorGameState = GetWorld()->GetGameState<AValorGameState>();
	const AValorPlayerState* ValorPlayerState = PlayerOwner ? PlayerOwner->GetPlayerState<AValorPlayerState>() : nullptr;
	if (!ValorGameState || !GEngine)
	{
		return;
	}

	// 내 팀 점수는 왼쪽(아군 색), 상대는 오른쪽(적 색). 팀이 없으면(관전) TeamA를 왼쪽에 둔다.
	const EValorTeam LocalTeam = ValorPlayerState && ValorPlayerState->GetTeam() != EValorTeam::None ? ValorPlayerState->GetTeam() : EValorTeam::TeamA;
	const EValorTeam EnemyTeam = ValorTeams::GetOpposingTeam(LocalTeam);

	const float BoxWidth = 300.0f * UIScale;
	const float BoxHeight = 78.0f * UIScale;
	const float Top = 12.0f * UIScale;
	DrawRect2D(FLinearColor::Black, CenterX - (BoxWidth * 0.5f), Top, BoxWidth, BoxHeight, 0.45f);

	UFont* LargeFont = GEngine->GetLargeFont();
	UFont* SmallFont = GEngine->GetSmallFont();
	const float TimeRemaining = ValorGameState->GetPhaseTimeRemaining();
	const FString TimeText = TimeRemaining >= 0.0f ? ValorMatchHUD::FormatTime(TimeRemaining) : TEXT("--:--");
	const bool bTimeLow = (ValorGameState->GetRoundPhase() == EValorRoundPhase::SpikePlanted)
		|| (ValorGameState->GetRoundPhase() == EValorRoundPhase::Combat && TimeRemaining >= 0.0f && TimeRemaining <= 10.0f);
	const FLinearColor TimeColor = bTimeLow ? ValorMatchHUD::EnemyColor : FLinearColor::White;

	const float RowY = Top + (6.0f * UIScale);
	DrawShadowedText(FString::FromInt(ValorGameState->GetTeamScore(LocalTeam)), ValorMatchHUD::AllyColor, CenterX - (100.0f * UIScale), RowY, LargeFont, 1.3f * UIScale, 0.5f);
	DrawShadowedText(TimeText, TimeColor, CenterX, RowY, LargeFont, 1.1f * UIScale, 0.5f);
	DrawShadowedText(FString::FromInt(ValorGameState->GetTeamScore(EnemyTeam)), ValorMatchHUD::EnemyColor, CenterX + (100.0f * UIScale), RowY, LargeFont, 1.3f * UIScale, 0.5f);

	FString PhaseText;
	switch (ValorGameState->GetRoundPhase())
	{
	case EValorRoundPhase::WaitingToStart: PhaseText = TEXT("WAITING FOR PLAYERS"); break;
	case EValorRoundPhase::AgentSelect: PhaseText = TEXT("AGENT SELECT"); break;
	case EValorRoundPhase::BuyPhase: PhaseText = FString::Printf(TEXT("ROUND %d  |  BUY PHASE (B)"), ValorGameState->GetRoundNumber()); break;
	case EValorRoundPhase::Combat:
	case EValorRoundPhase::RoundEnd: PhaseText = FString::Printf(TEXT("ROUND %d"), ValorGameState->GetRoundNumber()); break;
	case EValorRoundPhase::SpikePlanted:
	{
		const AValorSpike* Spike = AValorSpike::FindSpike(GetWorld());
		PhaseText = FString::Printf(TEXT("ROUND %d  |  SPIKE PLANTED %s"), ValorGameState->GetRoundNumber(), Spike ? *Spike->GetPlantedSiteName().ToString() : TEXT(""));
		break;
	}
	case EValorRoundPhase::MatchEnd: PhaseText = TEXT("MATCH OVER"); break;
	default: break;
	}

	const EValorTeamSide LocalSide = ValorGameState->GetSideOfTeam(LocalTeam);
	if (ValorGameState->GetRoundNumber() > 0 && LocalSide != EValorTeamSide::None)
	{
		PhaseText += LocalSide == EValorTeamSide::Attack ? TEXT("  |  ATTACK") : TEXT("  |  DEFENSE");
	}

	DrawShadowedText(PhaseText, FLinearColor(0.9f, 0.9f, 0.9f, 1.0f), CenterX, Top + BoxHeight - (22.0f * UIScale), SmallFont, UIScale, 0.5f);
}

void AValorHUD::DrawCredits(float UIScale)
{
	const AValorPlayerState* ValorPlayerState = PlayerOwner ? PlayerOwner->GetPlayerState<AValorPlayerState>() : nullptr;
	if (!ValorPlayerState || !GEngine)
	{
		return;
	}

	DrawShadowedText(FString::Printf(TEXT("CREDITS  %d"), ValorPlayerState->GetCredits()), FLinearColor(0.95f, 0.9f, 0.6f, 1.0f),
		24.0f * UIScale, Canvas->ClipY - (150.0f * UIScale), GEngine->GetMediumFont(), UIScale, 0.0f);
}

void AValorHUD::DrawRoundBanner(float CenterX, float CenterY, float UIScale)
{
	const AValorGameState* ValorGameState = GetWorld()->GetGameState<AValorGameState>();
	const AValorPlayerState* ValorPlayerState = PlayerOwner ? PlayerOwner->GetPlayerState<AValorPlayerState>() : nullptr;
	if (!ValorGameState || !GEngine)
	{
		return;
	}

	const EValorRoundPhase Phase = ValorGameState->GetRoundPhase();
	if (Phase != EValorRoundPhase::RoundEnd && Phase != EValorRoundPhase::MatchEnd)
	{
		return;
	}

	const EValorTeam LocalTeam = ValorPlayerState ? ValorPlayerState->GetTeam() : EValorTeam::None;
	FString Title;
	FString Detail;
	FLinearColor TitleColor = FLinearColor::White;

	if (Phase == EValorRoundPhase::MatchEnd)
	{
		const EValorTeam MatchWinner = ValorGameState->GetMatchWinner();
		const bool bWon = LocalTeam != EValorTeam::None && MatchWinner == LocalTeam;
		if (LocalTeam == EValorTeam::None)
		{
			Title = MatchWinner == EValorTeam::TeamA ? TEXT("TEAM A WINS") : TEXT("TEAM B WINS");
		}
		else
		{
			Title = bWon ? TEXT("VICTORY") : TEXT("DEFEAT");
		}
		TitleColor = bWon ? ValorMatchHUD::AllyColor : ValorMatchHUD::EnemyColor;
		const EValorTeam ShownTeam = LocalTeam != EValorTeam::None ? LocalTeam : EValorTeam::TeamA;
		Detail = FString::Printf(TEXT("%d - %d   |   NEW MATCH IN %s"), ValorGameState->GetTeamScore(ShownTeam), ValorGameState->GetTeamScore(ValorTeams::GetOpposingTeam(ShownTeam)),
			*ValorMatchHUD::FormatTime(ValorGameState->GetPhaseTimeRemaining()));
	}
	else
	{
		const EValorTeam Winner = ValorGameState->GetLastRoundWinner();
		const bool bWon = LocalTeam != EValorTeam::None && Winner == LocalTeam;
		if (LocalTeam == EValorTeam::None)
		{
			Title = Winner == EValorTeam::TeamA ? TEXT("TEAM A WINS THE ROUND") : TEXT("TEAM B WINS THE ROUND");
		}
		else
		{
			Title = bWon ? TEXT("ROUND WON") : TEXT("ROUND LOST");
		}
		TitleColor = bWon ? ValorMatchHUD::AllyColor : ValorMatchHUD::EnemyColor;

		switch (ValorGameState->GetLastRoundEndReason())
		{
		case EValorRoundEndReason::Elimination: Detail = bWon ? TEXT("ENEMIES ELIMINATED") : TEXT("YOUR TEAM WAS ELIMINATED"); break;
		case EValorRoundEndReason::TimeExpired: Detail = TEXT("TIME EXPIRED"); break;
		case EValorRoundEndReason::SpikeDetonated: Detail = TEXT("SPIKE DETONATED"); break;
		case EValorRoundEndReason::SpikeDefused: Detail = TEXT("SPIKE DEFUSED"); break;
		default: break;
		}

		// 다음 라운드에 공수가 바뀌면 알린다(전반 끝, 연장전).
		const int32 Round = ValorGameState->GetRoundNumber();
		if (ValorMatchRules::GetAttackingTeam(Round + 1) != ValorMatchRules::GetAttackingTeam(Round))
		{
			Detail += TEXT("   |   SWITCHING SIDES");
		}
	}

	float Y = CenterY - (150.0f * UIScale);
	Y += DrawShadowedText(Title, TitleColor, CenterX, Y, GEngine->GetLargeFont(), 2.0f * UIScale, 0.5f);
	Y += 6.0f * UIScale;
	DrawShadowedText(Detail, FLinearColor::White, CenterX, Y, GEngine->GetMediumFont(), 1.1f * UIScale, 0.5f);
}

void AValorHUD::DrawBuyMenu(float CenterX, float RequestedUIScale)
{
	const AValorPlayerController* ValorController = Cast<AValorPlayerController>(PlayerOwner);
	const AValorGameState* ValorGameState = GetWorld()->GetGameState<AValorGameState>();
	const AValorPlayerState* ValorPlayerState = PlayerOwner ? PlayerOwner->GetPlayerState<AValorPlayerState>() : nullptr;
	const AValorCharacter* Character = PlayerOwner ? Cast<AValorCharacter>(PlayerOwner->GetPawn()) : nullptr;
	const UValorCombatComponent* CombatComponent = Character ? Character->GetCombatComponent() : nullptr;
	if (!ValorController || !ValorController->IsBuyMenuOpen() || !ValorGameState || !ValorPlayerState || !CombatComponent || !GEngine)
	{
		return;
	}

	DrawRect2D(FLinearColor::Black, 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY, 0.55f);

	// 열 수만큼 가로로 늘어서므로, 글자 배율이 커도 화면 너비(96%)를 넘지 않게 줄인다.
	// 실드 열 + (요원 스킬을 상점에서 살 수 있으면) 스킬 열.
	const UValorAgentDefinition* ShopAgent = ValorPlayerState->GetAgent();
	TArray<EValorAbilitySlot> ShopAbilitySlots;
	for (int32 SlotIndex = 0; ShopAgent && SlotIndex < ValorAbilityRules::SlotCount; ++SlotIndex)
	{
		if (ShopAgent->GetSlot(static_cast<EValorAbilitySlot>(SlotIndex)).Cost > 0)
		{
			ShopAbilitySlots.Add(static_cast<EValorAbilitySlot>(SlotIndex));
		}
	}
	const int32 ExtraColumns = 1 + (ShopAbilitySlots.Num() > 0 ? 1 : 0);
	int32 ColumnCountForFit = ExtraColumns;
	for (const EValorWeaponCategory Category : {EValorWeaponCategory::Sidearm, EValorWeaponCategory::SMG, EValorWeaponCategory::Shotgun, EValorWeaponCategory::Rifle, EValorWeaponCategory::Sniper, EValorWeaponCategory::Heavy})
	{
		ColumnCountForFit += ValorGameState->GetShopWeapons().ContainsByPredicate([Category](const FValorShopWeaponEntry& Entry) { return Entry.Category == Category; }) ? 1 : 0;
	}
	const float BaseMenuWidth = (ColumnCountForFit * (190.0f + 12.0f)) - 12.0f;
	const float UIScale = FMath::Min(RequestedUIScale, (Canvas->ClipX * 0.96f) / BaseMenuWidth);

	UFont* MediumFont = GEngine->GetMediumFont();
	UFont* SmallFont = GEngine->GetSmallFont();
	const int32 Credits = ValorPlayerState->GetCredits();
	// 위 가운데 점수판(높이 90, 점수판 배율) 바로 아래부터 그린다.
	const float HeaderY = (96.0f * RequestedUIScale) + (6.0f * UIScale);
	DrawShadowedText(FString::Printf(TEXT("BUY  |  CREDITS %d  |  %s LEFT  |  CLICK TO BUY, CLICK AGAIN TO SELL  |  B TO CLOSE"), Credits,
		*ValorMatchHUD::FormatTime(ValorGameState->GetPhaseTimeRemaining())), FLinearColor(0.95f, 0.9f, 0.6f, 1.0f), CenterX, HeaderY, MediumFont, UIScale, 0.5f);

	// 열: 상점에 있는 총 분류(발로란트 순서) + 실드.
	TArray<EValorWeaponCategory> Columns;
	for (const EValorWeaponCategory Category : {EValorWeaponCategory::Sidearm, EValorWeaponCategory::SMG, EValorWeaponCategory::Shotgun, EValorWeaponCategory::Rifle, EValorWeaponCategory::Sniper, EValorWeaponCategory::Heavy})
	{
		if (ValorGameState->GetShopWeapons().ContainsByPredicate([Category](const FValorShopWeaponEntry& Entry) { return Entry.Category == Category; }))
		{
			Columns.Add(Category);
		}
	}

	const float ColumnWidth = 190.0f * UIScale;
	const float ColumnGap = 12.0f * UIScale;
	const float ItemHeight = 58.0f * UIScale;
	const float ItemGap = 8.0f * UIScale;
	const int32 ColumnCount = Columns.Num() + ExtraColumns;
	const float StartX = CenterX - ((((ColumnWidth + ColumnGap) * ColumnCount) - ColumnGap) * 0.5f);
	const float StartY = HeaderY + (56.0f * UIScale);

	float MouseX = -1.0f;
	float MouseY = -1.0f;
	PlayerOwner->GetMousePosition(MouseX, MouseY);

	// 칸 하나: 배경(보유 = 아군 색, 못 사면 흐리게, 마우스 위 = 밝게), 이름, 값.
	auto DrawItem = [&](float X, float Y, const FString& Name, int32 Cost, bool bOwned, bool bPurchasedThisRound, FName HitBoxName)
	{
		const bool bAffordable = Credits >= Cost;
		const bool bHovered = MouseX >= X && MouseX <= X + ColumnWidth && MouseY >= Y && MouseY <= Y + ItemHeight;
		FLinearColor Background = bOwned ? FLinearColor(0.1f, 0.45f, 0.4f, 1.0f) : FLinearColor(0.18f, 0.18f, 0.2f, 1.0f);
		if (bHovered)
		{
			Background = Background * 1.6f;
		}
		DrawRect2D(Background, X, Y, ColumnWidth, ItemHeight, (bAffordable || bOwned) ? 0.9f : 0.5f);

		const FLinearColor NameColor = (bAffordable || bOwned) ? FLinearColor::White : FLinearColor(0.6f, 0.6f, 0.6f, 1.0f);
		DrawShadowedText(Name.ToUpper(), NameColor, X + (10.0f * UIScale), Y + (8.0f * UIScale), MediumFont, UIScale, 0.0f);

		FString CostText = FString::FromInt(Cost);
		if (bPurchasedThisRound)
		{
			CostText = TEXT("SELL  ") + CostText;
		}
		else if (bOwned)
		{
			CostText = TEXT("OWNED");
		}
		const FLinearColor CostColor = (bAffordable || bOwned) ? FLinearColor(0.95f, 0.9f, 0.6f, 1.0f) : ValorMatchHUD::EnemyColor;
		DrawShadowedText(CostText, CostColor, X + ColumnWidth - (10.0f * UIScale), Y + ItemHeight - (22.0f * UIScale), SmallFont, UIScale, 1.0f);

		AddHitBox(FVector2D(X, Y), FVector2D(ColumnWidth, ItemHeight), HitBoxName, true);
	};

	const TArray<FValorShopWeaponEntry>& ShopWeapons = ValorGameState->GetShopWeapons();
	for (int32 ColumnIndex = 0; ColumnIndex < Columns.Num(); ++ColumnIndex)
	{
		const float X = StartX + (ColumnIndex * (ColumnWidth + ColumnGap));
		DrawShadowedText(ValorMatchHUD::CategoryLabel(Columns[ColumnIndex]), FLinearColor(0.8f, 0.8f, 0.8f, 1.0f), X, StartY - (24.0f * UIScale), SmallFont, UIScale, 0.0f);

		float Y = StartY;
		for (int32 EntryIndex = 0; EntryIndex < ShopWeapons.Num(); ++EntryIndex)
		{
			const FValorShopWeaponEntry& Entry = ShopWeapons[EntryIndex];
			if (Entry.Category != Columns[ColumnIndex])
			{
				continue;
			}

			const EValorWeaponSlot Slot = Entry.Category == EValorWeaponCategory::Sidearm ? EValorWeaponSlot::Secondary : EValorWeaponSlot::Primary;
			const AValorWeaponBase* Held = CombatComponent->GetWeaponInSlot(Slot);
			const bool bOwned = Held && Held->GetClass() == Entry.WeaponClass;
			const bool bPurchased = bOwned && ValorPlayerState->HasPurchasedWeaponThisRound(Entry.WeaponClass);
			DrawItem(X, Y, Entry.DisplayName, Entry.Cost, bOwned, bPurchased, FName(*FString::Printf(TEXT("BuyWeapon_%d"), EntryIndex)));
			Y += ItemHeight + ItemGap;
		}
	}

	// 실드 열.
	const UAbilitySystemComponent* AbilitySystemComponent = Character->GetAbilitySystemComponent();
	const float CurrentShield = AbilitySystemComponent ? AbilitySystemComponent->GetNumericAttribute(UValorCombatAttributeSet::GetShieldAttribute()) : 0.0f;
	const float ShieldX = StartX + (Columns.Num() * (ColumnWidth + ColumnGap));
	DrawShadowedText(TEXT("SHIELDS"), FLinearColor(0.8f, 0.8f, 0.8f, 1.0f), ShieldX, StartY - (24.0f * UIScale), SmallFont, UIScale, 0.0f);
	const TArray<FValorShieldOffer>& Shields = ValorGameState->GetShopShields();
	for (int32 ShieldIndex = 0; ShieldIndex < Shields.Num(); ++ShieldIndex)
	{
		const bool bPurchased = ValorPlayerState->HasPurchasedShieldThisRound(ShieldIndex);
		const bool bOwned = bPurchased || CurrentShield >= Shields[ShieldIndex].ShieldAmount;
		DrawItem(ShieldX, StartY + (ShieldIndex * (ItemHeight + ItemGap)), FString::Printf(TEXT("%s %.0f"), *Shields[ShieldIndex].DisplayName, Shields[ShieldIndex].ShieldAmount),
			Shields[ShieldIndex].Cost, bOwned, bPurchased, FName(*FString::Printf(TEXT("BuyShield_%d"), ShieldIndex)));
	}

	// 스킬 열: 칸 키 + 이름 + 충전/최대. 가득 차면 보유, 이번 라운드에 산 충전이 있으면 되팔기.
	if (ShopAbilitySlots.Num() > 0)
	{
		static const TCHAR* const SlotKeys[] = {TEXT("C"), TEXT("Q"), TEXT("E"), TEXT("X")};
		const float AbilityX = ShieldX + ColumnWidth + ColumnGap;
		DrawShadowedText(TEXT("ABILITIES"), FLinearColor(0.8f, 0.8f, 0.8f, 1.0f), AbilityX, StartY - (24.0f * UIScale), SmallFont, UIScale, 0.0f);
		for (int32 RowIndex = 0; RowIndex < ShopAbilitySlots.Num(); ++RowIndex)
		{
			const EValorAbilitySlot Slot = ShopAbilitySlots[RowIndex];
			const FValorAbilitySlotConfig& Config = ShopAgent->GetSlot(Slot);
			const int32 Charges = ValorPlayerState->GetAbilityCharges(Slot);
			const bool bFull = Charges >= Config.MaxCharges;
			const FString Label = FString::Printf(TEXT("%s  %s  %d/%d"), SlotKeys[static_cast<int32>(Slot)], *Config.DisplayName.ToString(), Charges, Config.MaxCharges);
			DrawItem(AbilityX, StartY + (RowIndex * (ItemHeight + ItemGap)), Label, Config.Cost, bFull, ValorPlayerState->HasPurchasedAbilityThisRound(Slot),
				FName(*FString::Printf(TEXT("BuyAbility_%d"), static_cast<int32>(Slot))));
		}
	}
}

void AValorHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);

	AValorPlayerController* ValorController = Cast<AValorPlayerController>(PlayerOwner);
	const AValorGameState* ValorGameState = GetWorld() ? GetWorld()->GetGameState<AValorGameState>() : nullptr;
	const AValorPlayerState* ValorPlayerState = PlayerOwner ? PlayerOwner->GetPlayerState<AValorPlayerState>() : nullptr;
	if (!ValorController || !ValorGameState || !ValorPlayerState)
	{
		return;
	}

	// 요원 선택 창.
	if (ValorController->IsAgentSelectOpen())
	{
		const FString SelectName = BoxName.ToString();
		int32 AgentIndex = INDEX_NONE;
		if (SelectName == TEXT("LockIn"))
		{
			ValorController->ServerLockAgent();
		}
		else if (SelectName.StartsWith(TEXT("SelectAgent_")) && LexTryParseString(AgentIndex, *SelectName.RightChop(12)) && ValorGameState->GetAgentRoster().IsValidIndex(AgentIndex))
		{
			ValorController->ServerSelectAgent(ValorGameState->GetAgentRoster()[AgentIndex]);
		}
		return;
	}

	if (!ValorController->IsBuyMenuOpen())
	{
		return;
	}

	const FString Name = BoxName.ToString();
	int32 Index = INDEX_NONE;
	if (Name.StartsWith(TEXT("BuyWeapon_")) && LexTryParseString(Index, *Name.RightChop(10)) && ValorGameState->GetShopWeapons().IsValidIndex(Index))
	{
		const TSubclassOf<AValorWeaponBase> WeaponClass = ValorGameState->GetShopWeapons()[Index].WeaponClass;
		if (ValorPlayerState->HasPurchasedWeaponThisRound(WeaponClass))
		{
			ValorController->ServerSellWeapon(WeaponClass);
		}
		else
		{
			ValorController->ServerBuyWeapon(WeaponClass);
		}
	}
	else if (Name.StartsWith(TEXT("BuyAbility_")) && LexTryParseString(Index, *Name.RightChop(11)) && Index >= 0 && Index < ValorAbilityRules::SlotCount)
	{
		// 가득 찼고 이번 라운드에 산 게 있으면 되팔고, 아니면 산다.
		const EValorAbilitySlot Slot = static_cast<EValorAbilitySlot>(Index);
		const UValorAgentDefinition* Agent = ValorPlayerState->GetAgent();
		const bool bFull = Agent && ValorPlayerState->GetAbilityCharges(Slot) >= Agent->GetSlot(Slot).MaxCharges;
		if (bFull && ValorPlayerState->HasPurchasedAbilityThisRound(Slot))
		{
			ValorController->ServerSellAbility(static_cast<uint8>(Index));
		}
		else
		{
			ValorController->ServerBuyAbility(static_cast<uint8>(Index));
		}
	}
	else if (Name.StartsWith(TEXT("BuyShield_")) && LexTryParseString(Index, *Name.RightChop(10)) && ValorGameState->GetShopShields().IsValidIndex(Index))
	{
		if (ValorPlayerState->HasPurchasedShieldThisRound(Index))
		{
			ValorController->ServerSellShield(Index);
		}
		else
		{
			ValorController->ServerBuyShield(Index);
		}
	}
}

void AValorHUD::DrawSpikeStatus(float CenterX, float CenterY, float UIScale)
{
	const UWorld* World = GetWorld();
	const AValorGameState* ValorGameState = World ? World->GetGameState<AValorGameState>() : nullptr;
	const AValorPlayerState* ValorPlayerState = PlayerOwner ? PlayerOwner->GetPlayerState<AValorPlayerState>() : nullptr;
	const AValorCharacter* Character = PlayerOwner ? Cast<AValorCharacter>(PlayerOwner->GetPawn()) : nullptr;
	if (!ValorGameState || !GEngine)
	{
		return;
	}

	UFont* MediumFont = GEngine->GetMediumFont();
	UFont* SmallFont = GEngine->GetSmallFont();
	const FLinearColor SiteColor(1.0f, 0.8f, 0.2f, 0.9f);
	const FLinearColor SpikeColor(1.0f, 0.35f, 0.25f, 1.0f);
	const FVector ViewLocation = PlayerOwner->PlayerCameraManager ? PlayerOwner->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;

	// 지점 표시(맵에 모델이 없으니 화면에 글자로 보여 준다): 지점 이름 + 거리(m).
	for (TActorIterator<AValorSpikeSite> It(World); It; ++It)
	{
		const FVector Screen = Project(It->GetActorLocation());
		if (Screen.Z > 0.0f)
		{
			const float Meters = FVector::Dist(ViewLocation, It->GetActorLocation()) / 100.0f;
			float Y = Screen.Y;
			Y += DrawShadowedText(It->GetSiteName().ToString(), SiteColor, Screen.X, Y, MediumFont, 1.6f * UIScale, 0.5f);
			DrawShadowedText(FString::Printf(TEXT("%.0fm"), Meters), SiteColor, Screen.X, Y, SmallFont, UIScale, 0.5f);
		}
	}

	const AValorSpike* Spike = AValorSpike::FindSpike(World);
	if (!Spike)
	{
		return;
	}

	const EValorTeam LocalTeam = ValorPlayerState ? ValorPlayerState->GetTeam() : EValorTeam::None;
	const bool bLocalAttacker = LocalTeam != EValorTeam::None && LocalTeam == ValorGameState->GetAttackingTeam();
	const EValorSpikeState SpikeState = Spike->GetState();

	// 스파이크 위치 표시: 설치되면 모두에게, 떨어지면 공격 팀에게.
	if (SpikeState == EValorSpikeState::Planted || (SpikeState == EValorSpikeState::Dropped && bLocalAttacker))
	{
		const FVector Screen = Project(Spike->GetActorLocation() + FVector(0.0f, 0.0f, 40.0f));
		if (Screen.Z > 0.0f)
		{
			DrawShadowedText(SpikeState == EValorSpikeState::Planted ? TEXT("SPIKE") : TEXT("SPIKE (DROPPED)"), SpikeColor, Screen.X, Screen.Y, SmallFont, 1.2f * UIScale, 0.5f);
		}
	}

	if (!Character || !Character->IsAlive())
	{
		return;
	}

	// 안내 문구와 진행 막대(화면 아래 가운데).
	FString Prompt;
	float Progress = -1.0f;
	float Checkpoint = -1.0f;
	FString ProgressLabel;
	const bool bCanUse = Spike->CanBeUsedBy(*Character);
	if (Spike->GetCarrier() == Character)
	{
		DrawShadowedText(TEXT("SPIKE CARRIER"), SpikeColor, 24.0f * UIScale, Canvas->ClipY - (180.0f * UIScale), MediumFont, UIScale, 0.0f);
		if (Spike->GetPlantProgress() > 0.0f)
		{
			Progress = Spike->GetPlantProgress() / Spike->GetPlantSeconds();
			ProgressLabel = TEXT("PLANTING");
		}
		else
		{
			Prompt = bCanUse ? TEXT("HOLD 4 TO PLANT") : TEXT("GO TO A SITE TO PLANT (HOLD 4)");
		}
	}
	else if (SpikeState == EValorSpikeState::Planted && !bLocalAttacker)
	{
		if (Spike->GetDefuser() == Character)
		{
			Progress = Spike->GetDefuseProgress() / Spike->GetDefuseSeconds();
			Checkpoint = 0.5f;
			ProgressLabel = Spike->GetDefuseCheckpoint() > 0.0f ? TEXT("DEFUSING (HALF SAVED)") : TEXT("DEFUSING");
		}
		else if (bCanUse)
		{
			Prompt = TEXT("HOLD 4 TO DEFUSE");
		}
	}

	const float BarWidth = 360.0f * UIScale;
	const float BarHeight = 10.0f * UIScale;
	const float BarY = CenterY + (140.0f * UIScale);
	if (Progress >= 0.0f)
	{
		const float BarX = CenterX - (BarWidth * 0.5f);
		DrawShadowedText(ProgressLabel, FLinearColor::White, CenterX, BarY - (28.0f * UIScale), MediumFont, UIScale, 0.5f);
		DrawRect2D(FLinearColor::Black, BarX, BarY, BarWidth, BarHeight, 0.6f);
		DrawRect2D(SiteColor, BarX, BarY, BarWidth * FMath::Clamp(Progress, 0.0f, 1.0f), BarHeight, 0.95f);
		if (Checkpoint > 0.0f)
		{
			DrawRect2D(FLinearColor::White, BarX + (BarWidth * Checkpoint) - UIScale, BarY - (3.0f * UIScale), 2.0f * UIScale, BarHeight + (6.0f * UIScale), 0.9f);
		}
	}
	else if (!Prompt.IsEmpty())
	{
		DrawShadowedText(Prompt, FLinearColor(1.0f, 1.0f, 1.0f, 0.9f), CenterX, BarY - (28.0f * UIScale), MediumFont, UIScale, 0.5f);
	}
}

void AValorHUD::DrawAbilityBar(float CenterX, float UIScale)
{
	const AValorPlayerState* ValorPlayerState = PlayerOwner ? PlayerOwner->GetPlayerState<AValorPlayerState>() : nullptr;
	const UValorAgentDefinition* Agent = ValorPlayerState ? ValorPlayerState->GetAgent() : nullptr;
	if (!Agent || !GEngine)
	{
		return;
	}

	// 네 칸을 아래 가운데에 나란히: [키] 이름 / 충전(궁극기는 포인트/필요).
	static const TCHAR* const SlotKeys[] = {TEXT("C"), TEXT("Q"), TEXT("E"), TEXT("X")};
	const float BoxWidth = 120.0f * UIScale;
	const float BoxHeight = 46.0f * UIScale;
	const float Gap = 8.0f * UIScale;
	const float TotalWidth = (BoxWidth * ValorAbilityRules::SlotCount) + (Gap * (ValorAbilityRules::SlotCount - 1));
	const float Top = Canvas->ClipY - BoxHeight - (16.0f * UIScale);
	UFont* SmallFont = GEngine->GetSmallFont();

	for (int32 SlotIndex = 0; SlotIndex < ValorAbilityRules::SlotCount; ++SlotIndex)
	{
		const EValorAbilitySlot Slot = static_cast<EValorAbilitySlot>(SlotIndex);
		const FValorAbilitySlotConfig& Config = Agent->GetSlot(Slot);
		const bool bUltimate = Config.UltimatePointsRequired > 0;
		const int32 Charges = ValorPlayerState->GetAbilityCharges(Slot);
		const bool bReady = bUltimate ? ValorPlayerState->GetUltimatePoints() >= Config.UltimatePointsRequired : Charges > 0;
		const FString Amount = bUltimate ? FString::Printf(TEXT("%d/%d"), ValorPlayerState->GetUltimatePoints(), Config.UltimatePointsRequired) : FString::Printf(TEXT("x%d"), Charges);

		const float X = CenterX - (TotalWidth * 0.5f) + (SlotIndex * (BoxWidth + Gap));
		DrawRect2D(bReady ? FLinearColor(0.1f, 0.45f, 0.4f, 1.0f) : FLinearColor::Black, X, Top, BoxWidth, BoxHeight, bReady ? 0.75f : 0.45f);
		const FLinearColor TextColor = bReady ? FLinearColor::White : FLinearColor(0.6f, 0.6f, 0.6f, 1.0f);
		DrawShadowedText(FString::Printf(TEXT("%s  %s"), SlotKeys[SlotIndex], *Config.DisplayName.ToString().ToUpper()), TextColor, X + (6.0f * UIScale), Top + (4.0f * UIScale), SmallFont, UIScale, 0.0f);
		DrawShadowedText(Amount, bReady ? FLinearColor(0.95f, 0.9f, 0.6f, 1.0f) : TextColor, X + BoxWidth - (6.0f * UIScale), Top + BoxHeight - (20.0f * UIScale), SmallFont, UIScale, 1.0f);
	}
}

void AValorHUD::DrawRevealedEnemies(float UIScale)
{
	const AValorPlayerState* ValorPlayerState = PlayerOwner ? PlayerOwner->GetPlayerState<AValorPlayerState>() : nullptr;
	const EValorTeam LocalTeam = ValorPlayerState ? ValorPlayerState->GetTeam() : EValorTeam::None;
	if (LocalTeam == EValorTeam::None || !GEngine)
	{
		return;
	}

	for (TActorIterator<AValorCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsRevealedTo(LocalTeam))
		{
			continue;
		}

		const FVector Screen = Project(It->GetActorLocation() + FVector(0.0f, 0.0f, 110.0f));
		if (Screen.Z > 0.0f)
		{
			const float Size = 12.0f * UIScale;
			DrawRect2D(FLinearColor(1.0f, 0.2f, 0.2f, 1.0f), Screen.X - (Size * 0.5f), Screen.Y - Size, Size, Size, 0.9f);
			DrawShadowedText(TEXT("SPOTTED"), FLinearColor(1.0f, 0.35f, 0.35f, 1.0f), Screen.X, Screen.Y + (2.0f * UIScale), GEngine->GetSmallFont(), UIScale, 0.5f);
		}
	}
}

void AValorHUD::DrawFlashOverlay()
{
	const AValorPlayerController* ValorController = Cast<AValorPlayerController>(PlayerOwner);
	const float Alpha = ValorController ? ValorController->GetFlashAlpha() : 0.0f;
	if (Alpha > 0.0f)
	{
		DrawRect2D(FLinearColor::White, 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY, Alpha);
	}
}

void AValorHUD::DrawAgentSelect(float CenterX, float UIScale)
{
	const AValorPlayerController* ValorController = Cast<AValorPlayerController>(PlayerOwner);
	const AValorGameState* ValorGameState = GetWorld()->GetGameState<AValorGameState>();
	const AValorPlayerState* ValorPlayerState = PlayerOwner ? PlayerOwner->GetPlayerState<AValorPlayerState>() : nullptr;
	if (!ValorController || !ValorController->IsAgentSelectOpen() || !ValorGameState || !ValorPlayerState || !GEngine)
	{
		return;
	}

	DrawRect2D(FLinearColor::Black, 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY, 0.75f);
	UFont* MediumFont = GEngine->GetMediumFont();
	UFont* SmallFont = GEngine->GetSmallFont();
	const EValorTeam LocalTeam = ValorPlayerState->GetTeam();

	// 같은 팀이 확정한 요원(누가 확정했는지).
	TMap<const UClass*, FString> LockedByTeam;
	for (const APlayerState* Other : ValorGameState->PlayerArray)
	{
		const AValorPlayerState* OtherState = Cast<AValorPlayerState>(Other);
		if (OtherState && OtherState != ValorPlayerState && OtherState->GetTeam() == LocalTeam && OtherState->IsAgentLocked() && OtherState->GetAgentClass())
		{
			LockedByTeam.Add(OtherState->GetAgentClass(), OtherState->GetPlayerName());
		}
	}

	DrawShadowedText(FString::Printf(TEXT("SELECT YOUR AGENT  |  %s"), *ValorMatchHUD::FormatTime(ValorGameState->GetPhaseTimeRemaining())),
		FLinearColor(0.95f, 0.9f, 0.6f, 1.0f), CenterX, 110.0f * UIScale, MediumFont, 1.2f * UIScale, 0.5f);

	// 요원 카드: 이름 + 스킬 네 칸. 클릭 = 고르기.
	const TArray<TSubclassOf<UValorAgentDefinition>>& Roster = ValorGameState->GetAgentRoster();
	const float CardWidth = 220.0f * UIScale;
	const float CardHeight = 170.0f * UIScale;
	const float Gap = 14.0f * UIScale;
	const float FitScale = FMath::Min(1.0f, (Canvas->ClipX * 0.96f) / FMath::Max(1.0f, (Roster.Num() * (CardWidth + Gap)) - Gap));
	const float W = CardWidth * FitScale;
	const float H = CardHeight * FitScale;
	const float G = Gap * FitScale;
	const float StartX = CenterX - ((Roster.Num() * (W + G)) - G) * 0.5f;
	const float CardY = 170.0f * UIScale;
	static const TCHAR* const SlotKeys[] = {TEXT("C"), TEXT("Q"), TEXT("E"), TEXT("X")};

	for (int32 Index = 0; Index < Roster.Num(); ++Index)
	{
		const UValorAgentDefinition* Agent = Roster[Index] ? Roster[Index]->GetDefaultObject<UValorAgentDefinition>() : nullptr;
		if (!Agent)
		{
			continue;
		}

		const float X = StartX + (Index * (W + G));
		const bool bMine = ValorPlayerState->GetAgentClass() == Roster[Index];
		const FString* TakenBy = LockedByTeam.Find(Roster[Index].Get());
		FLinearColor Background = bMine ? FLinearColor(0.1f, 0.45f, 0.4f, 1.0f) : FLinearColor(0.18f, 0.18f, 0.2f, 1.0f);
		DrawRect2D(Background, X, CardY, W, H, TakenBy ? 0.4f : 0.95f);

		float Y = CardY + (8.0f * UIScale);
		Y += DrawShadowedText(Agent->DisplayName.ToString().ToUpper(), TakenBy ? FLinearColor(0.5f, 0.5f, 0.5f, 1.0f) : FLinearColor::White, X + (10.0f * UIScale), Y, MediumFont, 1.1f * FitScale * UIScale, 0.0f);
		Y += 6.0f * UIScale;
		for (int32 SlotIndex = 0; SlotIndex < ValorAbilityRules::SlotCount; ++SlotIndex)
		{
			const FValorAbilitySlotConfig& Config = Agent->GetSlot(static_cast<EValorAbilitySlot>(SlotIndex));
			Y += DrawShadowedText(FString::Printf(TEXT("%s  %s"), SlotKeys[SlotIndex], *Config.DisplayName.ToString()), FLinearColor(0.85f, 0.85f, 0.85f, 1.0f), X + (10.0f * UIScale), Y, SmallFont, FitScale * UIScale, 0.0f);
		}

		FString Status;
		if (TakenBy)
		{
			Status = FString::Printf(TEXT("TAKEN: %s"), **TakenBy);
		}
		else if (bMine)
		{
			Status = ValorPlayerState->IsAgentLocked() ? TEXT("LOCKED") : TEXT("SELECTED");
		}
		DrawShadowedText(Status, FLinearColor(0.95f, 0.9f, 0.6f, 1.0f), X + W - (8.0f * UIScale), CardY + H - (22.0f * UIScale), SmallFont, FitScale * UIScale, 1.0f);

		if (!TakenBy && !ValorPlayerState->IsAgentLocked())
		{
			AddHitBox(FVector2D(X, CardY), FVector2D(W, H), FName(*FString::Printf(TEXT("SelectAgent_%d"), Index)), true);
		}
	}

	// LOCK IN 버튼(고른 요원이 있고 아직 확정하지 않았을 때).
	const float ButtonWidth = 220.0f * UIScale;
	const float ButtonHeight = 50.0f * UIScale;
	const float ButtonX = CenterX - (ButtonWidth * 0.5f);
	const float ButtonY = CardY + H + (24.0f * UIScale);
	const bool bCanLock = ValorPlayerState->GetAgentClass() && !ValorPlayerState->IsAgentLocked() && !LockedByTeam.Contains(ValorPlayerState->GetAgentClass().Get());
	DrawRect2D(bCanLock ? FLinearColor(0.85f, 0.3f, 0.3f, 1.0f) : FLinearColor(0.3f, 0.3f, 0.3f, 1.0f), ButtonX, ButtonY, ButtonWidth, ButtonHeight, 0.95f);
	DrawShadowedText(ValorPlayerState->IsAgentLocked() ? TEXT("LOCKED IN") : TEXT("LOCK IN"), FLinearColor::White, CenterX, ButtonY + (12.0f * UIScale), MediumFont, UIScale, 0.5f);
	if (bCanLock)
	{
		AddHitBox(FVector2D(ButtonX, ButtonY), FVector2D(ButtonWidth, ButtonHeight), TEXT("LockIn"), true);
	}

	// 팀원 선택 상황(아래).
	float TeamY = ButtonY + ButtonHeight + (24.0f * UIScale);
	for (const APlayerState* Other : ValorGameState->PlayerArray)
	{
		const AValorPlayerState* OtherState = Cast<AValorPlayerState>(Other);
		if (!OtherState || OtherState->GetTeam() != LocalTeam)
		{
			continue;
		}
		const FString AgentName = OtherState->GetAgent() ? OtherState->GetAgent()->DisplayName.ToString() : TEXT("-");
		TeamY += DrawShadowedText(FString::Printf(TEXT("%s  —  %s%s"), *OtherState->GetPlayerName(), *AgentName, OtherState->IsAgentLocked() ? TEXT("  (LOCKED)") : TEXT("")),
			OtherState == ValorPlayerState ? FLinearColor(0.35f, 0.95f, 0.8f, 1.0f) : FLinearColor(0.85f, 0.85f, 0.85f, 1.0f), CenterX, TeamY, SmallFont, UIScale, 0.5f);
	}
}

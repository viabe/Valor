// 설정 창(AValorHUD의 일부). ValorHUD.cpp가 이미 길어서 이 창만 따로 둔다.
//
// 구조: 항목 표(탭 → 열 → 항목)를 한 번 만들어 두고, 그리기와 클릭 처리가 같은 표를 본다(항목을 더하면 둘 다 따라온다).
// 항목은 설정 값의 참조를 돌려주는 함수를 가진다. 바꾸면 바로 저장(UValorUserSettings::Save)하고, HUD·컨트롤러가 다음 프레임에 읽는다.
// 캔버스 UI라 글자는 영어(기본 폰트에 한글이 없다). 디자인은 UMG로 옮길 때 다시 한다.

#include "UI/ValorHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Player/ValorPlayerController.h"
#include "Settings/ValorUserSettings.h"
#include "Session/ValorSessionSubsystem.h"

namespace ValorSettingsMenu
{
	enum class EItemType : uint8
	{
		Header,
		Float,
		Bool,
		Color
	};

	using FFloatRef = float& (*)(UValorUserSettings&);
	using FBoolRef = bool& (*)(UValorUserSettings&);

	struct FItem
	{
		FName Id;
		const TCHAR* Label = TEXT("");
		EItemType Type = EItemType::Header;

		// 막대 범위(Min~Max)와 끌 때 단위(Step), +/- 버튼 단위(ButtonStep). 버튼은 LimitMin~LimitMax까지 간다(콘솔로 넣은 큰 값 유지).
		float Min = 0.0f;
		float Max = 1.0f;
		float Step = 0.01f;
		float ButtonStep = 0.01f;
		float LimitMin = 0.0f;
		float LimitMax = 1.0f;
		int32 Decimals = 2;
		FFloatRef FloatRef = nullptr;

		FBoolRef BoolRef = nullptr;
		const TCHAR* TrueLabel = TEXT("ON");
		const TCHAR* FalseLabel = TEXT("OFF");
	};

	FItem MakeHeader(const TCHAR* Label)
	{
		FItem Item;
		Item.Label = Label;
		return Item;
	}

	FItem MakeFloat(const TCHAR* Id, const TCHAR* Label, float Min, float Max, float Step, float ButtonStep, int32 Decimals, FFloatRef Ref, float LimitMin, float LimitMax)
	{
		FItem Item;
		Item.Id = FName(Id);
		Item.Label = Label;
		Item.Type = EItemType::Float;
		Item.Min = Min;
		Item.Max = Max;
		Item.Step = Step;
		Item.ButtonStep = ButtonStep;
		Item.LimitMin = LimitMin;
		Item.LimitMax = LimitMax;
		Item.Decimals = Decimals;
		Item.FloatRef = Ref;
		return Item;
	}

	// 범위를 넘을 일이 없는 값(투명도·길이 등): 버튼 한계 = 막대 범위.
	FItem MakeFloat(const TCHAR* Id, const TCHAR* Label, float Min, float Max, float Step, int32 Decimals, FFloatRef Ref)
	{
		return MakeFloat(Id, Label, Min, Max, Step, Step, Decimals, Ref, Min, Max);
	}

	FItem MakeBool(const TCHAR* Id, const TCHAR* Label, FBoolRef Ref, const TCHAR* TrueLabel = TEXT("ON"), const TCHAR* FalseLabel = TEXT("OFF"))
	{
		FItem Item;
		Item.Id = FName(Id);
		Item.Label = Label;
		Item.Type = EItemType::Bool;
		Item.BoolRef = Ref;
		Item.TrueLabel = TrueLabel;
		Item.FalseLabel = FalseLabel;
		return Item;
	}

	FItem MakeColor()
	{
		FItem Item;
		Item.Id = FName(TEXT("Color"));
		Item.Label = TEXT("COLOR");
		Item.Type = EItemType::Color;
		return Item;
	}

	using FColumns = TArray<TArray<FItem>>;

	const FColumns& GetColumns(int32 Tab)
	{
		using namespace ValorSettingsRules;

		// 0: 조작. 감도는 발로란트 숫자(1 = 0.07도/카운트). 막대는 0.05~5(기본 2.5가 가운데쯤), 버튼·콘솔은 0.001~10.
		static const FColumns Controls = {
			{
				MakeHeader(TEXT("MOUSE")),
				MakeFloat(TEXT("Sens"), TEXT("SENSITIVITY"), 0.05f, 5.0f, 0.001f, 0.01f, 3,
					[](UValorUserSettings& S) -> float& { return S.EditControls().MouseSensitivity; }, MinSensitivity, MaxSensitivity),
				MakeFloat(TEXT("ScopedSens"), TEXT("SCOPED MULTIPLIER"), 0.1f, 2.0f, 0.001f, 0.01f, 3,
					[](UValorUserSettings& S) -> float& { return S.EditControls().ScopedSensitivityMultiplier; }, MinScopedMultiplier, MaxScopedMultiplier),
				MakeBool(TEXT("Invert"), TEXT("INVERT MOUSE"), [](UValorUserSettings& S) -> bool& { return S.EditControls().bInvertMouse; }),
			},
			{
				MakeHeader(TEXT("AIM")),
				MakeBool(TEXT("ADS"), TEXT("AIM DOWN SIGHTS"), [](UValorUserSettings& S) -> bool& { return S.EditControls().bToggleADS; }, TEXT("TOGGLE"), TEXT("HOLD")),
				MakeBool(TEXT("SniperZoom"), TEXT("SNIPER SCOPE"), [](UValorUserSettings& S) -> bool& { return S.EditControls().bToggleSniperZoom; }, TEXT("TOGGLE"), TEXT("HOLD")),
				MakeBool(TEXT("ReScope"), TEXT("AUTO RE-SCOPE"), [](UValorUserSettings& S) -> bool& { return S.EditControls().bAutoReScopeAfterShot; }),
			}};

		// 1: 조준선. 기본/안쪽 선/바깥 선 세 열.
		static const FColumns Crosshair = {
			{
				MakeHeader(TEXT("GENERAL")),
				MakeColor(),
				MakeBool(TEXT("Outlines"), TEXT("OUTLINES"), [](UValorUserSettings& S) -> bool& { return S.EditCrosshair().bOutlines; }),
				MakeFloat(TEXT("OutlineOpacity"), TEXT("OUTLINE OPACITY"), 0.0f, 1.0f, 0.05f, 2, [](UValorUserSettings& S) -> float& { return S.EditCrosshair().OutlineOpacity; }),
				MakeFloat(TEXT("OutlineThickness"), TEXT("OUTLINE THICKNESS"), 1.0f, 6.0f, 1.0f, 0, [](UValorUserSettings& S) -> float& { return S.EditCrosshair().OutlineThickness; }),
				MakeBool(TEXT("CenterDot"), TEXT("CENTER DOT"), [](UValorUserSettings& S) -> bool& { return S.EditCrosshair().bCenterDot; }),
				MakeFloat(TEXT("DotOpacity"), TEXT("DOT OPACITY"), 0.0f, 1.0f, 0.05f, 2, [](UValorUserSettings& S) -> float& { return S.EditCrosshair().CenterDotOpacity; }),
				MakeFloat(TEXT("DotThickness"), TEXT("DOT THICKNESS"), 1.0f, 6.0f, 1.0f, 0, [](UValorUserSettings& S) -> float& { return S.EditCrosshair().CenterDotThickness; }),
			},
			{
				MakeHeader(TEXT("INNER LINES")),
				MakeBool(TEXT("InnerShow"), TEXT("SHOW"), [](UValorUserSettings& S) -> bool& { return S.EditCrosshair().Inner.bShow; }),
				MakeFloat(TEXT("InnerOpacity"), TEXT("OPACITY"), 0.0f, 1.0f, 0.05f, 2, [](UValorUserSettings& S) -> float& { return S.EditCrosshair().Inner.Opacity; }),
				MakeFloat(TEXT("InnerLength"), TEXT("LENGTH"), 0.0f, 20.0f, 1.0f, 0, [](UValorUserSettings& S) -> float& { return S.EditCrosshair().Inner.Length; }),
				MakeFloat(TEXT("InnerThickness"), TEXT("THICKNESS"), 0.0f, 10.0f, 1.0f, 0, [](UValorUserSettings& S) -> float& { return S.EditCrosshair().Inner.Thickness; }),
				MakeFloat(TEXT("InnerOffset"), TEXT("OFFSET"), 0.0f, 20.0f, 1.0f, 0, [](UValorUserSettings& S) -> float& { return S.EditCrosshair().Inner.Offset; }),
				MakeBool(TEXT("InnerMove"), TEXT("MOVEMENT ERROR"), [](UValorUserSettings& S) -> bool& { return S.EditCrosshair().Inner.bMovementError; }),
				MakeBool(TEXT("InnerFire"), TEXT("FIRING ERROR"), [](UValorUserSettings& S) -> bool& { return S.EditCrosshair().Inner.bFiringError; }),
			},
			{
				MakeHeader(TEXT("OUTER LINES")),
				MakeBool(TEXT("OuterShow"), TEXT("SHOW"), [](UValorUserSettings& S) -> bool& { return S.EditCrosshair().Outer.bShow; }),
				MakeFloat(TEXT("OuterOpacity"), TEXT("OPACITY"), 0.0f, 1.0f, 0.05f, 2, [](UValorUserSettings& S) -> float& { return S.EditCrosshair().Outer.Opacity; }),
				MakeFloat(TEXT("OuterLength"), TEXT("LENGTH"), 0.0f, 20.0f, 1.0f, 0, [](UValorUserSettings& S) -> float& { return S.EditCrosshair().Outer.Length; }),
				MakeFloat(TEXT("OuterThickness"), TEXT("THICKNESS"), 0.0f, 10.0f, 1.0f, 0, [](UValorUserSettings& S) -> float& { return S.EditCrosshair().Outer.Thickness; }),
				MakeFloat(TEXT("OuterOffset"), TEXT("OFFSET"), 0.0f, 20.0f, 1.0f, 0, [](UValorUserSettings& S) -> float& { return S.EditCrosshair().Outer.Offset; }),
				MakeBool(TEXT("OuterMove"), TEXT("MOVEMENT ERROR"), [](UValorUserSettings& S) -> bool& { return S.EditCrosshair().Outer.bMovementError; }),
				MakeBool(TEXT("OuterFire"), TEXT("FIRING ERROR"), [](UValorUserSettings& S) -> bool& { return S.EditCrosshair().Outer.bFiringError; }),
			}};

		return Tab == 1 ? Crosshair : Controls;
	}

	const FItem* FindItem(FName Id)
	{
		for (int32 Tab = 0; Tab < 2; ++Tab)
		{
			for (const TArray<FItem>& Column : GetColumns(Tab))
			{
				for (const FItem& Item : Column)
				{
					if (Item.Id == Id && Item.Type != EItemType::Header)
					{
						return &Item;
					}
				}
			}
		}
		return nullptr;
	}

	FString FormatValue(float Value, int32 Decimals)
	{
		switch (Decimals)
		{
		case 0: return FString::Printf(TEXT("%.0f"), Value);
		case 1: return FString::Printf(TEXT("%.1f"), Value);
		case 2: return FString::Printf(TEXT("%.2f"), Value);
		default: return FString::Printf(TEXT("%.3f"), Value);
		}
	}

	FName HitBoxName(const FItem& Item, const TCHAR* Action)
	{
		return FName(*FString::Printf(TEXT("Set_%s_%s"), *Item.Id.ToString(), Action));
	}

	// 설계 크기(이 크기를 배율로 키우거나 줄인다). 두 탭이 같은 크기라 탭을 바꿔도 창이 흔들리지 않는다.
	constexpr float DesignWidth = 1300.0f;
	constexpr float DesignHeight = 660.0f;
	constexpr float RowHeight = 34.0f;
	constexpr float ColumnGap = 24.0f;

	// 캔버스 색은 선형 값이라 0.07도 화면에서는 밝은 회색으로 보인다. 어두운 창은 0.01대로 잡는다.
	const FLinearColor PanelColor(0.012f, 0.014f, 0.02f, 1.0f);
	const FLinearColor ButtonColor(0.06f, 0.065f, 0.08f, 1.0f);
	const FLinearColor SelectedColor(0.1f, 0.45f, 0.4f, 1.0f);
	const FLinearColor AccentColor(0.95f, 0.9f, 0.6f, 1.0f);
	const FLinearColor LabelColor(0.85f, 0.85f, 0.85f, 1.0f);
	const FLinearColor HintColor(0.6f, 0.6f, 0.6f, 1.0f);
}

bool AValorHUD::IsSettingsMenuOpen() const
{
	const AValorPlayerController* ValorController = Cast<AValorPlayerController>(PlayerOwner);
	return ValorController && ValorController->IsSettingsOpen();
}

void AValorHUD::UpdateActiveSettingsSlider(float MouseX)
{
	using namespace ValorSettingsMenu;
	const FItem* Item = FindItem(ActiveSettingsSlider);
	const FVector2D* Span = SettingsSliderSpans.Find(ActiveSettingsSlider);
	if (!Item || !Span || Item->Type != EItemType::Float || !Item->FloatRef)
	{
		return;
	}
	const float Fraction = (MouseX - Span->X) / FMath::Max(Span->Y, 1.0f);
	Item->FloatRef(UValorUserSettings::Get()) = ValorSettingsRules::FractionToValue(Fraction, Item->Min, Item->Max, Item->Step);
}

bool AValorHUD::HandleSettingsHitBox(const FString& BoxName)
{
	using namespace ValorSettingsMenu;
	AValorPlayerController* ValorController = Cast<AValorPlayerController>(PlayerOwner);
	TArray<FString> Parts;
	BoxName.ParseIntoArray(Parts, TEXT("_"));
	if (!ValorController || Parts.Num() < 2 || Parts[0] != TEXT("Set"))
	{
		return false;
	}

	UValorUserSettings& Settings = UValorUserSettings::Get();
	const FString& Target = Parts[1];
	const FString Action = Parts.Num() > 2 ? Parts[2] : FString();

	if (Target == TEXT("Tab"))
	{
		ValorController->SettingsTab = FMath::Clamp(FCString::Atoi(*Action), 0, 1);
		return true;
	}
	if (Target == TEXT("Close"))
	{
		ValorController->SetSettingsOpen(false);
		return true;
	}
	if (Target == TEXT("Leave"))
	{
		// 두 번 눌러야 나간다(첫 번째는 3초 동안 "CONFIRM LEAVE"로 바뀐다).
		const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
		if (Now <= ValorController->LeaveConfirmUntil)
		{
			ValorController->LeaveConfirmUntil = -1.0;
			if (UValorSessionSubsystem* Session = UValorSessionSubsystem::Get(this))
			{
				Session->ReturnToMainMenu();
			}
		}
		else
		{
			ValorController->LeaveConfirmUntil = Now + 3.0;
		}
		return true;
	}

	if (Target == TEXT("Reset"))
	{
		// 보고 있는 탭만 기본값으로.
		if (ValorController->SettingsTab == 1)
		{
			Settings.ResetCrosshair();
		}
		else
		{
			Settings.ResetControls();
		}
		Settings.Save();
		return true;
	}
	if (Target == TEXT("Color"))
	{
		const TArray<FColor>& Presets = ValorSettingsRules::GetCrosshairColorPresets();
		const int32 Index = FCString::Atoi(*Action);
		if (Presets.IsValidIndex(Index))
		{
			Settings.EditCrosshair().Color = Presets[Index];
			Settings.Save();
		}
		return true;
	}

	const FItem* Item = FindItem(FName(*Target));
	if (!Item)
	{
		return false;
	}

	if (Item->Type == EItemType::Float && Item->FloatRef)
	{
		float& Value = Item->FloatRef(Settings);
		if (Action == TEXT("Minus") || Action == TEXT("Plus"))
		{
			const float Direction = Action == TEXT("Plus") ? 1.0f : -1.0f;
			Value = ValorSettingsRules::SnapToStep(Value + (Direction * Item->ButtonStep), Item->LimitMin, Item->LimitMax, Item->Step);
			Settings.Save();
		}
		else if (Action == TEXT("Bar"))
		{
			// 누른 곳으로 바로 옮기고, 버튼을 떼기 전까지 끌 수 있다(떼면 저장).
			ActiveSettingsSlider = Item->Id;
			float MouseX = 0.0f;
			float MouseY = 0.0f;
			PlayerOwner->GetMousePosition(MouseX, MouseY);
			UpdateActiveSettingsSlider(MouseX);
		}
		return true;
	}

	if (Item->Type == EItemType::Bool && Item->BoolRef)
	{
		Item->BoolRef(Settings) = Action == TEXT("On");
		Settings.Save();
		return true;
	}
	return false;
}

void AValorHUD::DrawSettingsMenu(float RequestedScale)
{
	using namespace ValorSettingsMenu;
	AValorPlayerController* ValorController = Cast<AValorPlayerController>(PlayerOwner);
	if (!ValorController || !GEngine || !Canvas)
	{
		return;
	}

	UValorUserSettings& Settings = UValorUserSettings::Get();
	const int32 Tab = FMath::Clamp(ValorController->SettingsTab, 0, 1);

	// 끌던 막대: 버튼을 누르고 있으면 따라가고, 떼면 저장한다. 막대 위치는 지난 프레임에 기록한 것을 쓴다.
	float MouseX = -1.0f;
	float MouseY = -1.0f;
	PlayerOwner->GetMousePosition(MouseX, MouseY);
	if (ActiveSettingsSlider != NAME_None)
	{
		if (PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton))
		{
			UpdateActiveSettingsSlider(MouseX);
		}
		else
		{
			ActiveSettingsSlider = NAME_None;
			Settings.Save();
		}
	}
	SettingsSliderSpans.Reset();

	// 창이 화면 안에 들어오도록 줄인다(글자 배율이 커도, PIE 창이 작아도).
	const float S = FMath::Min3(RequestedScale, (Canvas->ClipX * 0.96f) / DesignWidth, (Canvas->ClipY * 0.94f) / DesignHeight);
	const float PanelWidth = DesignWidth * S;
	const float PanelHeight = DesignHeight * S;
	const float PanelX = (Canvas->ClipX - PanelWidth) * 0.5f;
	const float PanelY = (Canvas->ClipY - PanelHeight) * 0.5f;
	UFont* SmallFont = GEngine->GetSmallFont();
	UFont* MediumFont = GEngine->GetMediumFont();

	DrawRect2D(FLinearColor::Black, 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY, 0.6f);
	DrawRect2D(PanelColor, PanelX, PanelY, PanelWidth, PanelHeight, 0.97f);

	// 글자를 칸 높이 가운데에 놓는다.
	auto DrawCenteredText = [&](const FString& Text, const FLinearColor& Color, float X, float Y, float Height, UFont* Font, float AlignX)
	{
		float TextWidth = 0.0f;
		float TextHeight = 0.0f;
		GetTextSize(Text, TextWidth, TextHeight, Font, S);
		DrawShadowedText(Text, Color, X, Y + ((Height - TextHeight) * 0.5f), Font, S, AlignX);
	};

	auto DrawButton = [&](float X, float Y, float Width, float Height, const FString& Text, bool bSelected, FName HitBox)
	{
		const bool bHovered = MouseX >= X && MouseX <= X + Width && MouseY >= Y && MouseY <= Y + Height;
		FLinearColor Background = bSelected ? SelectedColor : ButtonColor;
		if (bHovered)
		{
			Background = Background * 1.4f;
		}
		DrawRect2D(Background, X, Y, Width, Height, 0.95f);
		DrawCenteredText(Text, FLinearColor::White, X + (Width * 0.5f), Y, Height, SmallFont, 0.5f);
		AddHitBox(FVector2D(X, Y), FVector2D(Width, Height), HitBox, true);
	};

	// 제목·탭·닫기.
	const float Margin = 26.0f * S;
	DrawCenteredText(TEXT("SETTINGS"), FLinearColor::White, PanelX + Margin, PanelY + (12.0f * S), 40.0f * S, MediumFont, 0.0f);
	const float TabY = PanelY + (60.0f * S);
	const float TabWidth = 180.0f * S;
	const float TabHeight = 36.0f * S;
	static const TCHAR* const TabNames[] = {TEXT("CONTROLS"), TEXT("CROSSHAIR")};
	for (int32 TabIndex = 0; TabIndex < 2; ++TabIndex)
	{
		DrawButton(PanelX + Margin + (TabIndex * (TabWidth + (10.0f * S))), TabY, TabWidth, TabHeight, TabNames[TabIndex], TabIndex == Tab, FName(*FString::Printf(TEXT("Set_Tab_%d"), TabIndex)));
	}
	// 오른쪽 위 버튼: 닫기, 탭 초기화, (게임 중) 나가기. 오른쪽부터 채운다.
	const float TopButtonWidth = 150.0f * S;
	float ButtonRight = PanelX + PanelWidth - Margin;
	auto DrawTopButton = [&](const FString& Text, bool bHighlight, FName HitBox)
	{
		DrawButton(ButtonRight - TopButtonWidth, TabY, TopButtonWidth, TabHeight, Text, bHighlight, HitBox);
		ButtonRight -= TopButtonWidth + (10.0f * S);
	};
	DrawTopButton(TEXT("CLOSE"), false, TEXT("Set_Close"));
	DrawTopButton(TEXT("RESET TAB"), false, TEXT("Set_Reset"));
	if (!ValorController->IsInMainMenu())
	{
		const bool bConfirming = GetWorld() && GetWorld()->GetRealTimeSeconds() <= ValorController->LeaveConfirmUntil;
		if (bConfirming)
		{
			// 확인 단계는 빨간 바탕으로 눈에 띄게.
			const float X = ButtonRight - TopButtonWidth;
			DrawRect2D(FLinearColor(0.55f, 0.05f, 0.05f, 1.0f), X - (2.0f * S), TabY - (2.0f * S), TopButtonWidth + (4.0f * S), TabHeight + (4.0f * S), 1.0f);
		}
		DrawTopButton(bConfirming ? TEXT("CONFIRM LEAVE") : TEXT("LEAVE MATCH"), false, TEXT("Set_Leave"));
	}

	const float ContentX = PanelX + Margin;
	const float ContentWidth = PanelWidth - (Margin * 2.0f);
	float ContentY = TabY + TabHeight + (18.0f * S);

	// 조준선 탭: 미리보기(어두운 배경 / 밝은 배경). 게임과 같은 크기(1080p 기준 배율)로 그린다.
	if (Tab == 1)
	{
		const float PreviewHeight = 110.0f * S;
		const float HalfWidth = ContentWidth * 0.5f;
		DrawRect2D(FLinearColor(0.03f, 0.03f, 0.04f, 1.0f), ContentX, ContentY, HalfWidth, PreviewHeight, 1.0f);
		DrawRect2D(FLinearColor(0.55f, 0.57f, 0.6f, 1.0f), ContentX + HalfWidth, ContentY, HalfWidth, PreviewHeight, 1.0f);
		const float GameScale = Canvas->ClipY / 1080.0f;
		DrawCrosshair(Settings.GetCrosshair(), ContentX + (HalfWidth * 0.5f), ContentY + (PreviewHeight * 0.5f), GameScale, 0.0f, 0.0f);
		DrawCrosshair(Settings.GetCrosshair(), ContentX + (HalfWidth * 1.5f), ContentY + (PreviewHeight * 0.5f), GameScale, 0.0f, 0.0f);
		DrawShadowedText(TEXT("PREVIEW"), HintColor, ContentX + (8.0f * S), ContentY + (6.0f * S), SmallFont, S * 0.85f, 0.0f);
		ContentY += PreviewHeight + (16.0f * S);
	}

	// 항목 열.
	const FColumns& Columns = GetColumns(Tab);
	const float ColumnWidth = (ContentWidth - (ColumnGap * S * (Columns.Num() - 1))) / FMath::Max(1, Columns.Num());
	const float Row = RowHeight * S;
	const float ControlHeight = 26.0f * S;
	const TArray<FColor>& Presets = ValorSettingsRules::GetCrosshairColorPresets();
	float LowestY = ContentY;

	for (int32 ColumnIndex = 0; ColumnIndex < Columns.Num(); ++ColumnIndex)
	{
		const float ColumnX = ContentX + (ColumnIndex * (ColumnWidth + (ColumnGap * S)));
		const float ControlX = ColumnX + (ColumnWidth * 0.42f);
		const float ControlRight = ColumnX + ColumnWidth - (4.0f * S);
		const float ControlWidth = ControlRight - ControlX;
		float Y = ContentY;

		for (const FItem& Item : Columns[ColumnIndex])
		{
			const float ControlY = Y + ((Row - ControlHeight) * 0.5f);
			if (Item.Type == EItemType::Header)
			{
				DrawCenteredText(Item.Label, AccentColor, ColumnX, Y, Row, SmallFont, 0.0f);
				DrawRect2D(AccentColor, ColumnX, Y + Row - (3.0f * S), ColumnWidth, FMath::Max(1.0f, 1.0f * S), 0.35f);
				Y += Row;
				continue;
			}

			FString Label = Item.Label;
			if (Item.Type == EItemType::Color)
			{
				Label += FString::Printf(TEXT("  #%s"), *Settings.GetCrosshair().Color.ToHex().Left(6));
			}
			DrawCenteredText(Label, LabelColor, ColumnX + (4.0f * S), Y, Row, SmallFont, 0.0f);

			if (Item.Type == EItemType::Float && Item.FloatRef)
			{
				// [-] [막대] [+] 값
				const float Value = Item.FloatRef(Settings);
				const float ButtonSize = ControlHeight;
				const float ValueWidth = 60.0f * S;
				const float Gap = 6.0f * S;
				const float PlusX = ControlRight - ValueWidth - Gap - ButtonSize;
				const float BarX = ControlX + ButtonSize + Gap;
				const float BarWidth = FMath::Max(10.0f * S, PlusX - Gap - BarX);
				DrawButton(ControlX, ControlY, ButtonSize, ButtonSize, TEXT("-"), false, HitBoxName(Item, TEXT("Minus")));
				DrawButton(PlusX, ControlY, ButtonSize, ButtonSize, TEXT("+"), false, HitBoxName(Item, TEXT("Plus")));

				const float Fraction = ValorSettingsRules::ValueToFraction(Value, Item.Min, Item.Max);
				const float TrackHeight = 6.0f * S;
				const float TrackY = ControlY + ((ControlHeight - TrackHeight) * 0.5f);
				DrawRect2D(FLinearColor(0.25f, 0.26f, 0.3f, 1.0f), BarX, TrackY, BarWidth, TrackHeight, 1.0f);
				DrawRect2D(SelectedColor * 1.4f, BarX, TrackY, BarWidth * Fraction, TrackHeight, 1.0f);
				const float KnobWidth = 6.0f * S;
				DrawRect2D(FLinearColor::White, BarX + (BarWidth * Fraction) - (KnobWidth * 0.5f), ControlY + (2.0f * S), KnobWidth, ControlHeight - (4.0f * S), Item.Id == ActiveSettingsSlider ? 1.0f : 0.85f);
				AddHitBox(FVector2D(BarX, ControlY), FVector2D(BarWidth, ControlHeight), HitBoxName(Item, TEXT("Bar")), true);
				SettingsSliderSpans.Add(Item.Id, FVector2D(BarX, BarWidth));

				DrawCenteredText(FormatValue(Value, Item.Decimals), FLinearColor::White, ControlRight, Y, Row, SmallFont, 1.0f);
			}
			else if (Item.Type == EItemType::Bool && Item.BoolRef)
			{
				const bool bValue = Item.BoolRef(Settings);
				const float ButtonWidth = (ControlWidth - (6.0f * S)) * 0.5f;
				DrawButton(ControlX, ControlY, ButtonWidth, ControlHeight, Item.TrueLabel, bValue, HitBoxName(Item, TEXT("On")));
				DrawButton(ControlX + ButtonWidth + (6.0f * S), ControlY, ButtonWidth, ControlHeight, Item.FalseLabel, !bValue, HitBoxName(Item, TEXT("Off")));
			}
			else if (Item.Type == EItemType::Color)
			{
				// 색 견본. 지금 색은 흰 테두리.
				const float Swatch = FMath::Min(ControlHeight - (4.0f * S), (ControlWidth - (5.0f * S * (Presets.Num() - 1))) / FMath::Max(1, Presets.Num()));
				const float SwatchY = Y + ((Row - Swatch) * 0.5f);
				for (int32 Index = 0; Index < Presets.Num(); ++Index)
				{
					const float SwatchX = ControlX + (Index * (Swatch + (5.0f * S)));
					if (Settings.GetCrosshair().Color == Presets[Index])
					{
						const float Border = 2.0f * S;
						DrawRect2D(FLinearColor::White, SwatchX - Border, SwatchY - Border, Swatch + (Border * 2.0f), Swatch + (Border * 2.0f), 1.0f);
					}
					DrawRect2D(FLinearColor::FromSRGBColor(Presets[Index]), SwatchX, SwatchY, Swatch, Swatch, 1.0f);
					AddHitBox(FVector2D(SwatchX, SwatchY), FVector2D(Swatch, Swatch), FName(*FString::Printf(TEXT("Set_Color_%d"), Index)), true);
				}
			}
			Y += Row;
		}
		LowestY = FMath::Max(LowestY, Y);
	}

	// 조작 탭: 감도를 거리로(마우스 DPI별 360도 도는 거리). 다른 게임 감도와 맞춰 볼 때 쓴다.
	if (Tab == 0)
	{
		const float DegreesPerCount = ValorSettingsRules::YawDegreesPerCount * Settings.GetControls().MouseSensitivity;
		auto CentimetersPer360 = [DegreesPerCount](float Dpi) { return DegreesPerCount > 0.0f ? (360.0f / DegreesPerCount) / Dpi * 2.54f : 0.0f; };
		DrawShadowedText(FString::Printf(TEXT("360 TURN:  %.1f cm @ 400 DPI   |   %.1f cm @ 800 DPI   |   %.1f cm @ 1600 DPI   (SAME NUMBERS AS VALORANT SENSITIVITY)"),
			CentimetersPer360(400.0f), CentimetersPer360(800.0f), CentimetersPer360(1600.0f)), HintColor, ContentX, LowestY + (14.0f * S), SmallFont, S, 0.0f);
	}

	// 아래 안내.
	DrawShadowedText(TEXT("CHANGES SAVE AUTOMATICALLY   |   ESC / F10 TO CLOSE   |   CONSOLE: ValorSens 0.4   ValorScopedSens 1   ValorCrosshairColor 00FF00"),
		HintColor, PanelX + (PanelWidth * 0.5f), PanelY + PanelHeight - (34.0f * S), SmallFont, S, 0.5f);
}

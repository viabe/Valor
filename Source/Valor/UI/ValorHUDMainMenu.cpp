// 메인 메뉴 화면(AValorHUD의 일부, 메뉴 게임 모드에서만 그린다).
//
// 왼쪽: 제목과 PLAY / SETTINGS / QUIT. 오른쪽: 이름, 서버 참가(주소), 로컬 데디 서버(개발용), 접속 중·실패 표시.
// 입력 칸 글자는 컨트롤러(InputKey)가 받고, 여기서는 그리기와 클릭만 한다. 캔버스 기본 폰트라 글자는 영어다.

#include "UI/ValorHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "Player/ValorPlayerController.h"
#include "Session/ValorSessionSubsystem.h"

namespace ValorMainMenuHUD
{
	// 설계 크기(이 영역을 화면 가운데에 두고 배율로 맞춘다).
	constexpr float DesignWidth = 1400.0f;
	constexpr float DesignHeight = 780.0f;

	// 캔버스 색은 선형 값(작은 값이 화면에서는 꽤 밝다).
	const FLinearColor PanelColor(0.012f, 0.014f, 0.02f, 1.0f);
	const FLinearColor ButtonColor(0.06f, 0.065f, 0.08f, 1.0f);
	const FLinearColor SelectedColor(0.1f, 0.45f, 0.4f, 1.0f);
	const FLinearColor AccentColor(0.95f, 0.9f, 0.6f, 1.0f);
	const FLinearColor LabelColor(0.85f, 0.85f, 0.85f, 1.0f);
	const FLinearColor HintColor(0.6f, 0.6f, 0.6f, 1.0f);
	const FLinearColor ErrorColor(0.95f, 0.35f, 0.3f, 1.0f);
	const FLinearColor TitleColor(0.95f, 0.33f, 0.33f, 1.0f);
}

void AValorHUD::HandleMainMenuHitBox(const FString& BoxName)
{
	AValorPlayerController* ValorController = Cast<AValorPlayerController>(PlayerOwner);
	if (!ValorController)
	{
		return;
	}

	if (BoxName == TEXT("Menu_Settings"))
	{
		ValorController->FocusMenuField(EValorMenuField::None);
		ValorController->SetSettingsOpen(true);
	}
	else if (BoxName == TEXT("Menu_Quit"))
	{
		ValorController->MenuQuit();
	}
	else if (BoxName == TEXT("Menu_Field_Name"))
	{
		ValorController->FocusMenuField(EValorMenuField::Name);
	}
	else if (BoxName == TEXT("Menu_Field_Address"))
	{
		ValorController->FocusMenuField(EValorMenuField::Address);
	}
	else if (BoxName == TEXT("Menu_Mode_0") || BoxName == TEXT("Menu_Mode_1"))
	{
		ValorController->SetMenuHostMode(BoxName.EndsWith(TEXT("1")) ? EValorHostMode::Deathmatch : EValorHostMode::Match);
	}
	else if (BoxName == TEXT("Menu_StartServer"))
	{
		ValorController->MenuStartLocalServer();
	}
	else if (BoxName == TEXT("Menu_StopServer"))
	{
		ValorController->MenuStopLocalServer();
	}
	else if (BoxName == TEXT("Menu_Join"))
	{
		ValorController->MenuJoin();
	}
	else if (BoxName == TEXT("Menu_Cancel"))
	{
		ValorController->MenuCancelJoin();
	}
	else if (BoxName == TEXT("Menu_Ok"))
	{
		ValorController->DismissMenuMessage();
	}
	else if (BoxName == TEXT("Menu_Background"))
	{
		// 빈 곳 클릭 = 입력 칸에서 나가기.
		ValorController->FocusMenuField(EValorMenuField::None);
	}
}

void AValorHUD::DrawMainMenu(float RequestedScale)
{
	using namespace ValorMainMenuHUD;
	AValorPlayerController* ValorController = Cast<AValorPlayerController>(PlayerOwner);
	UValorSessionSubsystem* Session = UValorSessionSubsystem::Get(this);
	if (!ValorController || !GEngine || !Canvas)
	{
		return;
	}

	const float S = FMath::Min3(RequestedScale / 1.4f, (Canvas->ClipX * 0.96f) / DesignWidth, (Canvas->ClipY * 0.94f) / DesignHeight);
	const float OriginX = (Canvas->ClipX - (DesignWidth * S)) * 0.5f;
	const float OriginY = (Canvas->ClipY - (DesignHeight * S)) * 0.5f;
	auto X = [OriginX, S](float Design) { return OriginX + (Design * S); };
	auto Y = [OriginY, S](float Design) { return OriginY + (Design * S); };

	UFont* SmallFont = GEngine->GetSmallFont();
	UFont* MediumFont = GEngine->GetMediumFont();
	UFont* LargeFont = GEngine->GetLargeFont();
	const float MenuTextScale = S * 1.25f;
	float MouseX = -1.0f;
	float MouseY = -1.0f;
	PlayerOwner->GetMousePosition(MouseX, MouseY);
	const double RealTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;

	// 설정 창이 위에 열려 있으면 메뉴의 클릭 영역을 만들지 않는다(같은 우선순위라 메뉴 칸이 설정 칸보다 먼저 잡힌다).
	const bool bInteractive = !ValorController->IsSettingsOpen();
	auto AddMenuHitBox = [&](float BoxX, float BoxY, float Width, float Height, FName HitBox, int32 Priority)
	{
		if (bInteractive)
		{
			AddHitBox(FVector2D(BoxX, BoxY), FVector2D(Width, Height), HitBox, true, Priority);
		}
	};

	// 배경: 맵이 희미하게 보이도록 어둡게 덮고, 빈 곳 클릭은 입력 칸 닫기.
	DrawRect2D(FLinearColor::Black, 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY, 0.7f);
	AddMenuHitBox(0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY, TEXT("Menu_Background"), -1);

	auto DrawCenteredText = [&](const FString& Text, const FLinearColor& Color, float TextX, float TextY, float Height, UFont* Font, float Scale, float AlignX)
	{
		float TextWidth = 0.0f;
		float TextHeight = 0.0f;
		GetTextSize(Text, TextWidth, TextHeight, Font, Scale);
		DrawShadowedText(Text, Color, TextX, TextY + ((Height - TextHeight) * 0.5f), Font, Scale, AlignX);
	};

	auto DrawButton = [&](float BoxX, float BoxY, float Width, float Height, const FString& Text, bool bSelected, FName HitBox, UFont* Font)
	{
		const bool bHovered = MouseX >= BoxX && MouseX <= BoxX + Width && MouseY >= BoxY && MouseY <= BoxY + Height;
		FLinearColor Background = bSelected ? SelectedColor : ButtonColor;
		if (bHovered)
		{
			Background = Background * 1.5f;
		}
		DrawRect2D(Background, BoxX, BoxY, Width, Height, 0.95f);
		DrawCenteredText(Text, FLinearColor::White, BoxX + (Width * 0.5f), BoxY, Height, Font, MenuTextScale, 0.5f);
		AddMenuHitBox(BoxX, BoxY, Width, Height, HitBox, 0);
	};

	// 입력 칸: 선택되면 테두리가 밝아지고 끝에 깜빡이는 커서.
	auto DrawField = [&](float BoxX, float BoxY, float Width, float Height, const FString& Text, bool bFocused, FName HitBox)
	{
		const float Border = FMath::Max(1.0f, 2.0f * S);
		DrawRect2D(bFocused ? AccentColor : FLinearColor(0.2f, 0.2f, 0.22f, 1.0f), BoxX - Border, BoxY - Border, Width + (Border * 2.0f), Height + (Border * 2.0f), 1.0f);
		DrawRect2D(FLinearColor(0.004f, 0.004f, 0.006f, 1.0f), BoxX, BoxY, Width, Height, 1.0f);
		const bool bCaret = bFocused && FMath::Fmod(RealTime, 1.0) < 0.5;
		DrawCenteredText(Text + (bCaret ? TEXT("|") : TEXT("")), FLinearColor::White, BoxX + (10.0f * S), BoxY, Height, MediumFont, S * 1.1f, 0.0f);
		AddMenuHitBox(BoxX, BoxY, Width, Height, HitBox, 0);
	};

	auto DrawHeader = [&](const FString& Text, float PanelLeft, float PanelWidth, float HeaderY)
	{
		DrawShadowedText(Text, AccentColor, PanelLeft, HeaderY, SmallFont, MenuTextScale, 0.0f);
		DrawRect2D(AccentColor, PanelLeft, HeaderY + (26.0f * S), PanelWidth, FMath::Max(1.0f, S), 0.35f);
	};

	// === 왼쪽: 제목과 메뉴 ===
	DrawShadowedText(TEXT("VALOR"), TitleColor, X(0.0f), Y(0.0f), LargeFont, S * 3.2f, 0.0f);
	DrawShadowedText(TEXT("TACTICAL SHOOTER  -  PROTOTYPE"), HintColor, X(4.0f), Y(96.0f), SmallFont, MenuTextScale, 0.0f);
	const float NavWidth = 260.0f * S;
	const float NavHeight = 54.0f * S;
	DrawButton(X(0.0f), Y(180.0f), NavWidth, NavHeight, TEXT("PLAY"), true, TEXT("Menu_Play"), MediumFont);
	DrawButton(X(0.0f), Y(246.0f), NavWidth, NavHeight, TEXT("SETTINGS"), false, TEXT("Menu_Settings"), MediumFont);
	DrawButton(X(0.0f), Y(312.0f), NavWidth, NavHeight, TEXT("QUIT"), false, TEXT("Menu_Quit"), MediumFont);

	// === 오른쪽: 플레이 판 ===
	const float PanelLeft = X(320.0f);
	const float PanelTop = Y(180.0f);
	const float PanelWidth = 1080.0f * S;
	const float PanelHeight = 600.0f * S;
	DrawRect2D(PanelColor, PanelLeft, PanelTop, PanelWidth, PanelHeight, 0.95f);
	const float Left = PanelLeft + (28.0f * S);
	const float Inner = PanelWidth - (56.0f * S);
	auto PY = [PanelTop, S](float Design) { return PanelTop + (Design * S); };
	const float FieldX = Left + (170.0f * S);
	const float FieldWidth = 440.0f * S;
	const float FieldHeight = 40.0f * S;
	const EValorMenuField Focus = ValorController->GetMenuFocus();

	// 이름.
	DrawHeader(TEXT("PROFILE"), Left, Inner, PY(22.0f));
	DrawCenteredText(TEXT("NAME"), LabelColor, Left, PY(62.0f), FieldHeight, SmallFont, MenuTextScale, 0.0f);
	DrawField(FieldX, PY(62.0f), FieldWidth, FieldHeight, ValorController->GetMenuNameText(), Focus == EValorMenuField::Name, TEXT("Menu_Field_Name"));
	DrawCenteredText(TEXT("SHOWN ON THE SCOREBOARD AND KILL FEED  (A-Z, 0-9, _ -)"), HintColor, FieldX + FieldWidth + (20.0f * S), PY(62.0f), FieldHeight, SmallFont, S, 0.0f);

	// 서버 참가(데디 서버 주소).
	DrawHeader(TEXT("JOIN A SERVER"), Left, Inner, PY(136.0f));
	DrawCenteredText(TEXT("ADDRESS"), LabelColor, Left, PY(176.0f), FieldHeight, SmallFont, MenuTextScale, 0.0f);
	DrawField(FieldX, PY(176.0f), FieldWidth, FieldHeight, ValorController->GetMenuAddressText(), Focus == EValorMenuField::Address, TEXT("Menu_Field_Address"));
	DrawButton(FieldX + FieldWidth + (16.0f * S), PY(176.0f), 160.0f * S, FieldHeight, TEXT("JOIN"), true, TEXT("Menu_Join"), MediumFont);
	DrawShadowedText(FString::Printf(TEXT("SERVER IP OR IP:PORT (DEFAULT PORT %d)   |   CTRL+V PASTES   |   ENTER JOINS   |   SAME GAME BUILD AS THE SERVER"), ValorSession::DefaultPort),
		HintColor, FieldX, PY(226.0f), SmallFont, S, 0.0f);

	// 로컬 데디 서버(개발용): 서버 실행 파일이 있을 때만.
	if (Session && Session->CanStartLocalServer())
	{
		DrawHeader(TEXT("LOCAL SERVER (DEV)"), Left, Inner, PY(280.0f));
		const EValorHostMode HostMode = ValorController->GetMenuHostMode();
		DrawCenteredText(TEXT("MODE"), LabelColor, Left, PY(320.0f), FieldHeight, SmallFont, MenuTextScale, 0.0f);
		const float ModeWidth = 214.0f * S;
		DrawButton(FieldX, PY(320.0f), ModeWidth, FieldHeight, TEXT("MATCH"), HostMode == EValorHostMode::Match, TEXT("Menu_Mode_0"), SmallFont);
		DrawButton(FieldX + ModeWidth + (12.0f * S), PY(320.0f), ModeWidth, FieldHeight, TEXT("DEATHMATCH"), HostMode == EValorHostMode::Deathmatch, TEXT("Menu_Mode_1"), SmallFont);
		const bool bRunning = Session->IsLocalServerRunning();
		DrawButton(FieldX + (ModeWidth * 2.0f) + (24.0f * S), PY(320.0f), 240.0f * S, FieldHeight, bRunning ? TEXT("STOP LOCAL SERVER") : TEXT("START LOCAL SERVER"),
			!bRunning, bRunning ? TEXT("Menu_StopServer") : TEXT("Menu_StartServer"), SmallFont);
		const FString LocalAddress = Session->GetLocalAddress();
		const FString Hint = bRunning
			? FString::Printf(TEXT("DEDICATED SERVER RUNNING ON THIS PC.  OTHERS JOIN WITH  %s  (UDP %d)   |   LOG: Saved/Logs/LocalServer.log"), LocalAddress.IsEmpty() ? TEXT("THIS PC'S IP") : *LocalAddress, ValorSession::DefaultPort)
			: (HostMode == EValorHostMode::Match
				? TEXT("STARTS A DEDICATED SERVER ON THIS PC AND JOINS IT. MATCH STARTS 10 S AFTER THE FIRST PLAYER JOINS.")
				: TEXT("STARTS A DEDICATED DEATHMATCH SERVER ON THIS PC AND JOINS IT. RESPAWN, JOIN ANY TIME."));
		DrawShadowedText(Hint, HintColor, FieldX, PY(370.0f), SmallFont, S, 0.0f);
	}

	// 상태: 접속 중(취소) / 실패·입력 오류(확인).
	const float StatusY = PY(480.0f);
	const float StatusHeight = 56.0f * S;
	const FString& Error = ValorController->GetMenuMessage().IsEmpty() && Session ? Session->GetLastError() : ValorController->GetMenuMessage();
	if (Session && Session->IsConnecting())
	{
		const int32 Dots = static_cast<int32>(FMath::Fmod(RealTime * 2.0, 4.0));
		DrawRect2D(FLinearColor(0.02f, 0.05f, 0.05f, 1.0f), Left, StatusY, Inner, StatusHeight, 1.0f);
		DrawCenteredText(FString::Printf(TEXT("CONNECTING TO %s%s"), *Session->GetConnectingAddress(), *FString::ChrN(Dots, TEXT('.'))), FLinearColor::White, Left + (16.0f * S), StatusY, StatusHeight, MediumFont, S * 1.1f, 0.0f);
		DrawButton(Left + Inner - (170.0f * S), StatusY + (8.0f * S), 154.0f * S, StatusHeight - (16.0f * S), TEXT("CANCEL"), false, TEXT("Menu_Cancel"), SmallFont);
	}
	else if (!Error.IsEmpty())
	{
		DrawRect2D(FLinearColor(0.12f, 0.01f, 0.01f, 1.0f), Left, StatusY, Inner, StatusHeight, 1.0f);
		DrawCenteredText(Error, ErrorColor, Left + (16.0f * S), StatusY, StatusHeight, MediumFont, S * 1.1f, 0.0f);
		DrawButton(Left + Inner - (170.0f * S), StatusY + (8.0f * S), 154.0f * S, StatusHeight - (16.0f * S), TEXT("OK"), false, TEXT("Menu_Ok"), SmallFont);
	}
}

#include "Session/ValorSessionTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// 메뉴·접속 규칙: 방 옵션, 이름 거르기, 주소 검사, 같은 이름 번호, 키 → 글자.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorSessionRulesTest, "Valor.Session.Rules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorSessionRulesTest::RunTest(const FString& Parameters)
{
	// 서버 URL: 경기 = 대기실(최소 인원), 데스매치 = DM 모드. 리슨(?listen)이 아니라 데디 서버가 연다.
	TestEqual(TEXT("경기 서버"), ValorSession::BuildServerURL(EValorHostMode::Match, 2), FString(TEXT("/Game/Maps/ThridPersonExampleMap?Lobby=1?MinPlayers=2")));
	TestEqual(TEXT("최소 인원은 1~10"), ValorSession::BuildServerURL(EValorHostMode::Match, 0), FString(TEXT("/Game/Maps/ThridPersonExampleMap?Lobby=1?MinPlayers=1")));
	TestEqual(TEXT("데스매치 서버"), ValorSession::BuildServerURL(EValorHostMode::Deathmatch, 2), FString(TEXT("/Game/Maps/ThridPersonExampleMap?game=DM")));

	// 로컬 데디 서버 명령줄: 에디터 실행 파일은 프로젝트 + -server, 패키지는 URL만.
	const FString EditorArguments = ValorSession::BuildLocalServerArguments(true, TEXT("D:/P/Valor.uproject"), TEXT("/Game/M?Lobby=1"), 7777, TEXT("D:/P/S.log"));
	TestEqual(TEXT("에디터 실행 파일 인자"), EditorArguments, FString(TEXT("\"D:/P/Valor.uproject\" /Game/M?Lobby=1 -server -port=7777 -log -unattended -nosound -abslog=\"D:/P/S.log\"")));
	const FString PackagedArguments = ValorSession::BuildLocalServerArguments(false, TEXT("D:/P/Valor.uproject"), TEXT("/Game/M?game=DM"), 7778, TEXT("S.log"));
	TestEqual(TEXT("패키지 서버 인자"), PackagedArguments, FString(TEXT("/Game/M?game=DM -port=7778 -log -unattended -nosound -abslog=\"S.log\"")));

	// 이름: 영문·숫자·_·-만, 16자.
	TestEqual(TEXT("공백·기호 제거"), ValorSession::SanitizePlayerName(TEXT(" Hello World!?=")), FString(TEXT("HelloWorld")));
	TestEqual(TEXT("_ - 유지"), ValorSession::SanitizePlayerName(TEXT("ace_shot-1")), FString(TEXT("ace_shot-1")));
	TestEqual(TEXT("16자까지"), ValorSession::SanitizePlayerName(TEXT("abcdefghijklmnopqrstuvwxyz")).Len(), 16);
	TestEqual(TEXT("한글은 거른다(기본 폰트에 없다)"), ValorSession::SanitizePlayerName(TEXT("발로ab")), FString(TEXT("ab")));

	// 주소.
	FString Error;
	TestTrue(TEXT("IPv4"), ValorSession::IsValidJoinAddress(TEXT("127.0.0.1"), Error));
	TestTrue(TEXT("IPv4:포트"), ValorSession::IsValidJoinAddress(TEXT("192.168.0.5:7777"), Error));
	TestTrue(TEXT("이름"), ValorSession::IsValidJoinAddress(TEXT("localhost"), Error));
	TestTrue(TEXT("이름:포트, 앞뒤 공백"), ValorSession::IsValidJoinAddress(TEXT(" my-pc.lan:7778 "), Error));
	TestFalse(TEXT("빈 주소"), ValorSession::IsValidJoinAddress(TEXT(""), Error));
	TestFalse(TEXT("포트 비어 있음"), ValorSession::IsValidJoinAddress(TEXT("1.2.3.4:"), Error));
	TestFalse(TEXT("포트 범위 밖"), ValorSession::IsValidJoinAddress(TEXT("1.2.3.4:99999"), Error));
	TestEqual(TEXT("포트 오류 문구"), Error, FString(TEXT("PORT MUST BE 1-65535")));
	TestFalse(TEXT("포트 숫자 아님"), ValorSession::IsValidJoinAddress(TEXT("1.2.3.4:abc"), Error));
	TestFalse(TEXT("공백 섞임"), ValorSession::IsValidJoinAddress(TEXT("1.2 .3.4"), Error));
	TestFalse(TEXT("IPv6는 받지 않음"), ValorSession::IsValidJoinAddress(TEXT("::1"), Error));
	TestFalse(TEXT("점으로 시작"), ValorSession::IsValidJoinAddress(TEXT(".1.2.3"), Error));
	TestFalse(TEXT("점 두 개 연속"), ValorSession::IsValidJoinAddress(TEXT("1..2.3"), Error));
	TestFalse(TEXT("IPv4 칸이 255 넘음(콜론 빠뜨린 오타)"), ValorSession::IsValidJoinAddress(TEXT("1.2.3.499999"), Error));
	TestFalse(TEXT("IPv4 세 칸"), ValorSession::IsValidJoinAddress(TEXT("10.0.1"), Error));

	// 같은 이름: 번호를 붙인다.
	TestEqual(TEXT("겹치지 않으면 그대로"), ValorSession::MakeUniqueName(TEXT("Ace"), {TEXT("Bob")}), FString(TEXT("Ace")));
	TestEqual(TEXT("겹치면 2"), ValorSession::MakeUniqueName(TEXT("Ace"), {TEXT("Ace")}), FString(TEXT("Ace 2")));
	TestEqual(TEXT("2도 있으면 3"), ValorSession::MakeUniqueName(TEXT("Ace"), {TEXT("Ace"), TEXT("Ace 2")}), FString(TEXT("Ace 3")));

	// 키 → 글자(영문 자판).
	TestEqual(TEXT("A"), static_cast<int32>(ValorSession::KeyToChar(EKeys::A, false)), static_cast<int32>(TEXT('a')));
	TestEqual(TEXT("Shift+A"), static_cast<int32>(ValorSession::KeyToChar(EKeys::A, true)), static_cast<int32>(TEXT('A')));
	TestEqual(TEXT("1"), static_cast<int32>(ValorSession::KeyToChar(EKeys::One, false)), static_cast<int32>(TEXT('1')));
	TestEqual(TEXT("Shift+1은 글자 아님"), static_cast<int32>(ValorSession::KeyToChar(EKeys::One, true)), static_cast<int32>(static_cast<TCHAR>(0)));
	TestEqual(TEXT("숫자 패드 5"), static_cast<int32>(ValorSession::KeyToChar(EKeys::NumPadFive, false)), static_cast<int32>(TEXT('5')));
	TestEqual(TEXT("."), static_cast<int32>(ValorSession::KeyToChar(EKeys::Period, false)), static_cast<int32>(TEXT('.')));
	TestEqual(TEXT("-"), static_cast<int32>(ValorSession::KeyToChar(EKeys::Hyphen, false)), static_cast<int32>(TEXT('-')));
	TestEqual(TEXT("Shift+- = _"), static_cast<int32>(ValorSession::KeyToChar(EKeys::Hyphen, true)), static_cast<int32>(TEXT('_')));
	TestEqual(TEXT("Shift+; = :"), static_cast<int32>(ValorSession::KeyToChar(EKeys::Semicolon, true)), static_cast<int32>(TEXT(':')));
	TestEqual(TEXT(";는 글자 아님"), static_cast<int32>(ValorSession::KeyToChar(EKeys::Semicolon, false)), static_cast<int32>(static_cast<TCHAR>(0)));
	TestEqual(TEXT("Esc는 글자 아님"), static_cast<int32>(ValorSession::KeyToChar(EKeys::Escape, false)), static_cast<int32>(static_cast<TCHAR>(0)));
	TestEqual(TEXT("F10은 글자 아님"), static_cast<int32>(ValorSession::KeyToChar(EKeys::F10, false)), static_cast<int32>(static_cast<TCHAR>(0)));
	return true;
}

#endif

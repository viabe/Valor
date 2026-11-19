#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

// 서버 모드. 경기 = 라운드제(대기실: 인원이 모이면 카운트다운 뒤 시작), 데스매치 = 바로 시작·부활.
enum class EValorHostMode : uint8
{
	Match,
	Deathmatch
};

// 메인 메뉴 입력 칸.
enum class EValorMenuField : uint8
{
	None,
	Name,
	Address
};

// 메뉴·접속 규칙(순수 함수, 단위 테스트 대상).
namespace ValorSession
{
	// 메인 메뉴 URL 옵션. 기본 맵(GameDefaultMap)에 붙여 연다. DefaultEngine.ini LocalMapOptions와 같은 값이어야
	// 접속 실패·끊김(엔진이 기본 맵으로 돌려보냄)도 메뉴로 돌아온다.
	inline constexpr const TCHAR* MenuOptions = TEXT("game=Menu");

	// 서버 맵(지금은 하나). 경기 모드는 맵의 게임 모드(BP_ValorMatchGame: 스파이크 등 사용자 설정)를 그대로 쓴다.
	inline constexpr const TCHAR* ServerMap = TEXT("/Game/Maps/ThridPersonExampleMap");

	// 데디 서버 기본 포트(엔진 기본값). 다른 컴퓨터에서 들어오려면 서버 PC의 이 UDP 포트가 열려 있어야 한다.
	inline constexpr int32 DefaultPort = 7777;

	inline constexpr int32 MaxNameLength = 16;
	inline constexpr int32 MaxAddressLength = 64;

	// 데디 서버가 열 맵 URL. 경기 = 대기실(?Lobby=1?MinPlayers=N: N명이 모이면 카운트다운 뒤 시작), 데스매치 = ?game=DM.
	FString BuildServerURL(EValorHostMode Mode, int32 MinPlayers);

	// 이 PC에 데디 서버를 띄우는 명령줄 인자. 에디터 실행 파일이면 프로젝트 파일과 -server를 붙인다(쿠킹 없이 실행),
	// 패키지면 ValorServer.exe에 맵 URL만. 로그는 따로 파일로(클라 로그와 섞이지 않게).
	FString BuildLocalServerArguments(bool bEditorBinary, const FString& ProjectFile, const FString& ServerURL, int32 Port, const FString& LogFile);

	// 이름: 영문·숫자·_·-만, 16자. URL·점수판에 그대로 쓸 수 있게 거른다(공백·?·= 제거).
	FString SanitizePlayerName(const FString& Name);
	bool IsAllowedNameChar(TCHAR Character);

	// 주소: "192.168.0.5", "192.168.0.5:7777", "localhost". IPv6는 받지 않는다. 틀리면 OutError에 짧은 영어 설명.
	bool IsAllowedAddressChar(TCHAR Character);
	bool IsValidJoinAddress(const FString& Address, FString& OutError);

	// 같은 이름이 이미 있으면 " 2", " 3"을 붙인다(점수판에서 구분되게). Taken에는 다른 플레이어 이름.
	FString MakeUniqueName(const FString& Desired, const TArray<FString>& Taken);

	// 키 → 글자(메뉴 입력 칸). 글자가 아닌 키는 0. 자판은 영문 QWERTY 기준.
	TCHAR KeyToChar(const FKey& Key, bool bShift);
}

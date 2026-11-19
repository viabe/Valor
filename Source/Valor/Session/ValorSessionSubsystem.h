#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "HAL/PlatformProcess.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Session/ValorSessionTypes.h"
#include "ValorSessionSubsystem.generated.h"

/**
 * 서버 참가·나가기·로컬 데디 서버(개발용)와 접속 실패 메시지(게임 인스턴스당 하나, 맵을 옮겨도 남는다).
 *
 * 왜 게임 인스턴스 서브시스템인가: 접속이 끊기면 엔진이 월드를 통째로 바꿔(기본 맵 = 메인 메뉴) 컨트롤러·HUD가 사라진다.
 *   실패 이유를 메뉴에 보여 주려면 맵 이동을 넘어 살아 있는 곳에 남겨야 한다. 설정 파일을 바꿀 필요도 없다(자동 생성).
 * 네트워크: 데디 서버 기준. 클라는 서버 주소(IP[:포트])로 직접 참가한다. 세션 검색·NAT 통과는 없다.
 *   로컬 서버: 개발할 때 이 PC에 데디 서버 프로세스를 띄우고 127.0.0.1로 붙는다. 이 게임이 꺼지면 같이 끈다.
 */
UCLASS()
class VALOR_API UValorSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UValorSessionSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// 참가: 주소가 맞으면 접속을 시작한다(접속하는 동안 메뉴가 그대로 보인다). 틀리면 false와 이유.
	bool JoinGame(const FString& Address, FString& OutError);

	// 접속 중이면 그만둔다.
	void CancelJoin();
	bool IsConnecting() const;
	const FString& GetConnectingAddress() const { return ConnectingAddress; }

	// 메인 메뉴로(서버에서 나간다).
	void ReturnToMainMenu();

	// === 로컬 데디 서버(개발용) ===
	// 에디터 실행 파일로 돌고 있으면 같은 실행 파일을 -server로, 패키지면 옆의 ValorServer.exe를 띄운다. 띄우고 바로 붙는다
	// (서버가 뜨는 동안 접속 요청을 계속 다시 보내므로 준비되면 들어간다).
	bool CanStartLocalServer() const;
	bool StartLocalServer(EValorHostMode Mode, FString& OutError);
	void StopLocalServer();
	bool IsLocalServerRunning() const;

	// 마지막 실패 메시지(메뉴가 보여 주고 확인을 누르면 지운다).
	const FString& GetLastError() const { return LastError; }
	void ClearLastError() { LastError.Reset(); }

	// 이 컴퓨터의 주 IP(로컬 서버에 다른 사람이 들어올 때 칠 주소). 처음 한 번 구한다.
	const FString& GetLocalAddress() const { return LocalAddress; }

	// 로컬 서버가 쓰는 최소 인원(혼자서도 시작되게 1).
	static constexpr int32 LocalServerMinPlayers = 1;

private:
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	bool IsOurWorld(const UWorld* World) const;

	// 맵이 열릴 때: 서버에 붙었으면(클라) 접속 중 표시를 끝낸다.
	void HandlePostLoadMap(UWorld* LoadedWorld);

	// 로컬 서버 실행 파일. 에디터 실행 파일이면 bEditorBinary.
	bool FindLocalServerExecutable(FString& OutExecutable, bool& bOutEditorBinary) const;

	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;

	FString LastError;
	FString LocalAddress;
	FString ConnectingAddress;

	FProcHandle LocalServerProcess;

	// 스스로 나간 직후의 끊김 알림은 오류로 보여 주지 않는다.
	double IgnoreFailuresUntil = 0.0;
};

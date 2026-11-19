#include "Session/ValorSessionSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameMapsSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "IPAddress.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Settings/ValorUserSettings.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogValorSession, Log, All);

namespace
{
	// 이 PC에서 UDP 포트를 이미 누가 쓰는지(서버가 떠 있는지) 묶어 보고 바로 놓는다.
	bool IsLocalUdpPortInUse(int32 Port)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!SocketSubsystem)
		{
			return false;
		}
		FUniqueSocket Socket = SocketSubsystem->CreateUniqueSocket(NAME_DGram, TEXT("ValorPortProbe"), FNetworkProtocolTypes::IPv4);
		if (!Socket)
		{
			return false;
		}
		TSharedRef<FInternetAddr> Address = SocketSubsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
		Address->SetAnyAddress();
		Address->SetPort(Port);
		return !Socket->Bind(*Address);
	}
}

UValorSessionSubsystem* UValorSessionSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UValorSessionSubsystem>() : nullptr;
}

void UValorSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UValorSessionSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UValorSessionSubsystem::HandleTravelFailure);
	}
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UValorSessionSubsystem::HandlePostLoadMap);

	// 주 IP. 여러 어댑터가 있으면 엔진이 고른 것(보통 기본 경로)을 쓴다.
	if (ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
	{
		bool bCanBindAll = false;
		const TSharedRef<FInternetAddr> Address = Sockets->GetLocalHostAddr(*GLog, bCanBindAll);
		LocalAddress = Address->IsValid() ? Address->ToString(false) : FString();
	}
}

void UValorSessionSubsystem::Deinitialize()
{
	// 이 게임이 꺼지면 띄워 둔 로컬 서버도 끈다(창 없이 떠 있어 남으면 찾기 어렵다).
	StopLocalServer();
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	Super::Deinitialize();
}

bool UValorSessionSubsystem::IsOurWorld(const UWorld* World) const
{
	// GEngine 알림은 모든 게임 인스턴스(PIE 창 여러 개)에 온다. 내 인스턴스 것만 받는다.
	return World && World->GetGameInstance() == GetGameInstance();
}

void UValorSessionSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (IsOurWorld(LoadedWorld) && LoadedWorld->GetNetMode() == NM_Client && !ConnectingAddress.IsEmpty())
	{
		UE_LOG(LogValorSession, Log, TEXT("참가 완료: %s"), *ConnectingAddress);
		ConnectingAddress.Reset();
	}
}

bool UValorSessionSubsystem::JoinGame(const FString& Address, FString& OutError)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	if (!Controller)
	{
		OutError = TEXT("NOT READY");
		return false;
	}
	if (!ValorSession::IsValidJoinAddress(Address, OutError))
	{
		return false;
	}

	LastError.Reset();
	ConnectingAddress = Address.TrimStartAndEnd();
	UValorUserSettings& Settings = UValorUserSettings::Get();
	Settings.SetLastJoinAddress(ConnectingAddress);
	Settings.Save();

	// 절대 이동 = 지금 월드(메뉴)를 그대로 두고 접속을 시작한다. 접속되면 서버 맵으로 바뀌고, 실패하면 엔진이 기본 맵(메뉴)으로 보낸다.
	UE_LOG(LogValorSession, Log, TEXT("참가: %s"), *ConnectingAddress);
	Controller->ClientTravel(ConnectingAddress, TRAVEL_Absolute);
	return true;
}

void UValorSessionSubsystem::CancelJoin()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World && GEngine)
	{
		GEngine->CancelPending(World);
		// 아직 접속을 시작하지 않고 이동만 예약된 상태도 지운다.
		if (FWorldContext* Context = GEngine->GetWorldContextFromWorld(World))
		{
			Context->TravelURL.Reset();
		}
	}
	UE_LOG(LogValorSession, Log, TEXT("참가 취소: %s"), *ConnectingAddress);
	ConnectingAddress.Reset();
}

bool UValorSessionSubsystem::IsConnecting() const
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const FWorldContext* Context = (World && GEngine) ? GEngine->GetWorldContextFromWorld(World) : nullptr;
	return Context && (Context->PendingNetGame != nullptr || (!Context->TravelURL.IsEmpty() && !ConnectingAddress.IsEmpty()));
}

void UValorSessionSubsystem::ReturnToMainMenu()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	IgnoreFailuresUntil = FPlatformTime::Seconds() + 3.0;
	ConnectingAddress.Reset();
	UE_LOG(LogValorSession, Log, TEXT("메인 메뉴로(넷 모드 %d)"), static_cast<int32>(World->GetNetMode()));
	UGameplayStatics::OpenLevel(World, FName(*UGameMapsSettings::GetGameDefaultMap()), true, ValorSession::MenuOptions);
}

bool UValorSessionSubsystem::FindLocalServerExecutable(FString& OutExecutable, bool& bOutEditorBinary) const
{
#if WITH_EDITOR
	// 에디터 실행 파일(-game 포함): 같은 실행 파일을 -server로 띄우면 쿠킹 없이 데디 서버가 된다.
	OutExecutable = FPlatformProcess::ExecutablePath();
	bOutEditorBinary = true;
#else
	// 패키지: 클라 실행 파일 옆의 데디 서버 실행 파일(Server 대상으로 따로 패키징해 같은 폴더에 둔 경우).
	OutExecutable = FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("ValorServer.exe"));
	bOutEditorBinary = false;
#endif
	return !OutExecutable.IsEmpty() && FPaths::FileExists(OutExecutable);
}

bool UValorSessionSubsystem::CanStartLocalServer() const
{
	FString Executable;
	bool bEditorBinary = false;
	return FindLocalServerExecutable(Executable, bEditorBinary);
}

bool UValorSessionSubsystem::StartLocalServer(EValorHostMode Mode, FString& OutError)
{
	FString Executable;
	bool bEditorBinary = false;
	if (!FindLocalServerExecutable(Executable, bEditorBinary))
	{
		OutError = TEXT("NO SERVER EXECUTABLE NEXT TO THE GAME");
		return false;
	}

	StopLocalServer();

	// 이미 다른 서버(손으로 띄운 서버, 다른 클라 창의 로컬 서버)가 7777을 쓰면 새 서버는 뜨지 못한다.
	// 그대로 127.0.0.1에 붙으면 그 다른 서버에 들어가 헷갈리므로 띄우지 않고 알린다.
	if (IsLocalUdpPortInUse(ValorSession::DefaultPort))
	{
		OutError = FString::Printf(TEXT("PORT %d IS IN USE - A SERVER IS ALREADY RUNNING ON THIS PC (JOIN 127.0.0.1)"), ValorSession::DefaultPort);
		return false;
	}

	const FString LogFile = FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir() / TEXT("LocalServer.log"));
	const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString Arguments = ValorSession::BuildLocalServerArguments(bEditorBinary, ProjectFile,
		ValorSession::BuildServerURL(Mode, LocalServerMinPlayers), ValorSession::DefaultPort, LogFile);

	// 창 없이 띄운다(로그는 Saved/Logs/LocalServer.log).
	uint32 ProcessId = 0;
	LocalServerProcess = FPlatformProcess::CreateProc(*Executable, *Arguments, true, true, true, &ProcessId, 0, nullptr, nullptr);
	if (!LocalServerProcess.IsValid())
	{
		OutError = TEXT("COULD NOT START THE LOCAL SERVER");
		return false;
	}
	UE_LOG(LogValorSession, Log, TEXT("로컬 서버 시작(PID %u): %s %s"), ProcessId, *Executable, *Arguments);

	// 서버가 뜨는 동안에도 클라는 접속 요청을 다시 보내므로 바로 붙기 시작한다.
	return JoinGame(FString::Printf(TEXT("127.0.0.1:%d"), ValorSession::DefaultPort), OutError);
}

void UValorSessionSubsystem::StopLocalServer()
{
	if (LocalServerProcess.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(LocalServerProcess))
		{
			UE_LOG(LogValorSession, Log, TEXT("로컬 서버 끄기"));
			FPlatformProcess::TerminateProc(LocalServerProcess, true);
		}
		FPlatformProcess::CloseProc(LocalServerProcess);
	}
	LocalServerProcess = FProcHandle();
}

bool UValorSessionSubsystem::IsLocalServerRunning() const
{
	FProcHandle Handle = LocalServerProcess;
	return Handle.IsValid() && FPlatformProcess::IsProcRunning(Handle);
}

void UValorSessionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	if (!IsOurWorld(World) || FPlatformTime::Seconds() < IgnoreFailuresUntil)
	{
		return;
	}

	FString Reason;
	switch (FailureType)
	{
	case ENetworkFailure::PendingConnectionFailure:
	case ENetworkFailure::ConnectionTimeout:
		Reason = ConnectingAddress.IsEmpty() ? TEXT("CONNECTION TIMED OUT") : FString::Printf(TEXT("COULD NOT CONNECT TO %s"), *ConnectingAddress);
		break;
	case ENetworkFailure::OutdatedClient:
	case ENetworkFailure::OutdatedServer:
	case ENetworkFailure::NetChecksumMismatch:
	case ENetworkFailure::NetGuidMismatch:
		Reason = TEXT("VERSION MISMATCH WITH THE SERVER (USE THE SAME BUILD)");
		break;
	default:
		Reason = TEXT("CONNECTION TO THE SERVER WAS LOST");
		break;
	}
	LastError = Reason;
	ConnectingAddress.Reset();
	UE_LOG(LogValorSession, Warning, TEXT("네트워크 실패 %s: %s (%s)"), ENetworkFailure::ToString(FailureType), *ErrorString, *Reason);
}

void UValorSessionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	if (!IsOurWorld(World) || FPlatformTime::Seconds() < IgnoreFailuresUntil)
	{
		return;
	}
	LastError = FailureType == ETravelFailure::NoLevel ? TEXT("MAP NOT FOUND") : TEXT("COULD NOT JOIN THE GAME");
	ConnectingAddress.Reset();
	UE_LOG(LogValorSession, Warning, TEXT("이동 실패 %s: %s"), ETravelFailure::ToString(FailureType), *ErrorString);
}

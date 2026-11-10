#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Weapons/ValorWeaponBase.h"

#if WITH_DEV_AUTOMATION_TESTS

// 자동화 테스트 공용 도우미(테스트 파일에서만 포함한다). 여러 테스트 파일이 같이 쓰므로 헤더 하나에 둔다
// (익명 네임스페이스에 각자 두면 유니티 빌드에서 한 번역 단위로 합쳐질 때 이름이 겹친다).

/** 테스트 전용 임시 게임 월드. 액터 초기화(InitializeActorsForPlay)를 해야 SpawnActor가 PostInitializeComponents를 호출한다. */
struct FValorScopedTestWorld
{
	UWorld* World = nullptr;

	explicit FValorScopedTestWorld(const TCHAR* WorldName = TEXT("ValorTestWorld"))
	{
		World = UWorld::CreateWorld(EWorldType::Game, false, WorldName);
		if (World && GEngine)
		{
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}
	}

	~FValorScopedTestWorld()
	{
		if (World)
		{
			if (GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}

			World->DestroyWorld(false);
		}
	}

	FValorScopedTestWorld(const FValorScopedTestWorld&) = delete;
	FValorScopedTestWorld& operator=(const FValorScopedTestWorld&) = delete;
};

/** /Game/Valor/Gun의 총 BP 클래스(이름, 클래스). 줍기 BP처럼 총이 아닌 BP는 빠진다. */
inline TArray<TPair<FString, UClass*>> LoadValorGunBlueprintClasses()
{
	TArray<FString> AssetFiles;
	IFileManager::Get().FindFiles(AssetFiles, *(FPaths::ProjectContentDir() / TEXT("Valor/Gun/BP_*.uasset")), true, false);

	TArray<TPair<FString, UClass*>> GunClasses;
	for (const FString& AssetFile : AssetFiles)
	{
		const FString AssetName = FPaths::GetBaseFilename(AssetFile);
		UClass* LoadedClass = LoadObject<UClass>(nullptr, *FString::Printf(TEXT("/Game/Valor/Gun/%s.%s_C"), *AssetName, *AssetName));
		if (LoadedClass && LoadedClass->IsChildOf(AValorWeaponBase::StaticClass()))
		{
			GunClasses.Emplace(AssetName, LoadedClass);
		}
	}

	return GunClasses;
}

#endif

#include "Commandlets/ValorGenerateWeaponAssetsCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"
#include "Weapons/ValorWeaponPresets.h"

DEFINE_LOG_CATEGORY_STATIC(LogValorWeaponAssets, Log, All);

namespace ValorWeaponAssetGeneration
{
	const TCHAR* const PackageRoot = TEXT("/Game/Valor/DataAsset");

	// -overwrite로 수치를 되돌릴 때도, 에디터에서 연결해 둔 연출 참조는 그대로 둔다(프리셋은 수치만 책임진다).
	void KeepEditorAssignments(const FValorWeaponConfig& Existing, FValorWeaponConfig& InOutConfig)
	{
		InOutConfig.FireMontage = Existing.FireMontage;
		InOutConfig.FireMontagePlayRate = Existing.FireMontagePlayRate;
		InOutConfig.GripSocketName = Existing.GripSocketName;
		InOutConfig.Effects = Existing.Effects;
	}

	bool SaveAsset(UValorWeaponDataAsset* Asset)
	{
		UPackage* Package = Asset->GetPackage();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);
	}
}

UValorGenerateWeaponAssetsCommandlet::UValorGenerateWeaponAssetsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UValorGenerateWeaponAssetsCommandlet::Main(const FString& Params)
{
	using namespace ValorWeaponAssetGeneration;

	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamValues;
	ParseCommandLine(*Params, Tokens, Switches, ParamValues);

	const bool bOverwrite = Switches.Contains(TEXT("overwrite"));

	// -only=Phantom,Spectre 처럼 일부만 만들 수 있다(대소문자 무시: FName 비교).
	TSet<FName> OnlyWeapons;
	if (const FString* OnlyValue = ParamValues.Find(TEXT("only")))
	{
		TArray<FString> Names;
		OnlyValue->ParseIntoArray(Names, TEXT(","), true);
		for (const FString& Name : Names)
		{
			OnlyWeapons.Add(FName(*Name.TrimStartAndEnd()));
		}
	}

	int32 CreatedCount = 0;
	int32 UpdatedCount = 0;
	int32 SkippedCount = 0;
	int32 FailedCount = 0;

	for (FValorWeaponPreset& Preset : ValorWeaponPresets::BuildAll())
	{
		if (OnlyWeapons.Num() > 0 && !OnlyWeapons.Contains(Preset.WeaponId))
		{
			continue;
		}

		const FString AssetName = FString::Printf(TEXT("DA_%s"), *Preset.WeaponId.ToString());
		const FString PackageName = FString(PackageRoot) / AssetName;
		const bool bAlreadyExists = FPackageName::DoesPackageExist(PackageName);

		UValorWeaponDataAsset* Asset = nullptr;
		if (bAlreadyExists)
		{
			if (!bOverwrite)
			{
				UE_LOG(LogValorWeaponAssets, Display, TEXT("건너뜀: %s (이미 있음, 수치를 프리셋으로 되돌리려면 -overwrite)"), *PackageName);
				++SkippedCount;
				continue;
			}

			Asset = LoadObject<UValorWeaponDataAsset>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName));
			if (!Asset)
			{
				UE_LOG(LogValorWeaponAssets, Error, TEXT("실패: %s 를 UValorWeaponDataAsset으로 불러올 수 없다"), *PackageName);
				++FailedCount;
				continue;
			}

			KeepEditorAssignments(Asset->WeaponConfig, Preset.Config);
		}
		else
		{
			UPackage* Package = CreatePackage(*PackageName);
			Asset = NewObject<UValorWeaponDataAsset>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Asset);
		}

		Asset->WeaponConfig = MoveTemp(Preset.Config);
#if WITH_EDITORONLY_DATA
		Asset->SourceNotes = MoveTemp(Preset.SourceNotes);
#endif
		Asset->MarkPackageDirty();

		if (!SaveAsset(Asset))
		{
			UE_LOG(LogValorWeaponAssets, Error, TEXT("실패: %s 저장 실패"), *PackageName);
			++FailedCount;
			continue;
		}

		if (bAlreadyExists)
		{
			++UpdatedCount;
			UE_LOG(LogValorWeaponAssets, Display, TEXT("갱신: %s"), *PackageName);
		}
		else
		{
			++CreatedCount;
			UE_LOG(LogValorWeaponAssets, Display, TEXT("생성: %s"), *PackageName);
		}
	}

	UE_LOG(LogValorWeaponAssets, Display, TEXT("무기 데이터 에셋 결과: 생성 %d, 갱신 %d, 건너뜀 %d, 실패 %d"), CreatedCount, UpdatedCount, SkippedCount, FailedCount);
	return FailedCount > 0 ? 1 : 0;
}

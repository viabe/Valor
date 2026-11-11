#include "Commandlets/ValorRepairCharacterBlueprintCommandlet.h"

#include "Components/ValorCharacterMovementComponent.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Character.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogValorRepairCharacter, Log, All);

UValorRepairCharacterBlueprintCommandlet::UValorRepairCharacterBlueprintCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UValorRepairCharacterBlueprintCommandlet::Main(const FString& Params)
{
	FString BlueprintPath = TEXT("/Game/Valor/Character/BP_ValorCharacterBase");
	FParse::Value(*Params, TEXT("bp="), BlueprintPath);

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		UE_LOG(LogValorRepairCharacter, Error, TEXT("BP를 불러오지 못했다: %s"), *BlueprintPath);
		return 1;
	}

	auto MovementClassName = [Blueprint]()
	{
		const ACharacter* CharacterDefaults = Cast<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject());
		const UCharacterMovementComponent* Movement = CharacterDefaults ? CharacterDefaults->GetCharacterMovement() : nullptr;
		return Movement ? Movement->GetClass()->GetName() : FString(TEXT("(없음)"));
	};

	UE_LOG(LogValorRepairCharacter, Display, TEXT("컴파일 전 이동 컴포넌트: %s"), *MovementClassName());
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	const FString AfterClass = MovementClassName();
	UE_LOG(LogValorRepairCharacter, Display, TEXT("컴파일 후 이동 컴포넌트: %s"), *AfterClass);

	const ACharacter* CharacterDefaults = Cast<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject());
	if (!CharacterDefaults || !Cast<UValorCharacterMovementComponent>(CharacterDefaults->GetCharacterMovement()))
	{
		UE_LOG(LogValorRepairCharacter, Error, TEXT("컴파일 뒤에도 Valor 이동 컴포넌트가 아니다(%s). 저장하지 않는다."), *AfterClass);
		return 1;
	}

	UPackage* Package = Blueprint->GetOutermost();
	const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs))
	{
		UE_LOG(LogValorRepairCharacter, Error, TEXT("저장 실패: %s"), *Filename);
		return 1;
	}

	UE_LOG(LogValorRepairCharacter, Display, TEXT("저장함: %s"), *Filename);
	return 0;
}

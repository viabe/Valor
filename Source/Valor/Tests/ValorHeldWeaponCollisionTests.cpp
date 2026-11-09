#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Weapons/ValorInventoryTypes.h"
#include "Weapons/ValorWeaponBase.h"

#if WITH_DEV_AUTOMATION_TESTS

// 손에 드는 무기의 충돌이 모두 꺼져 있는지 검증한다.
// 배경 버그: BP_Operator에 추가된 부품 StaticMesh 컴포넌트들이 기본 충돌(BlockAllDynamic)을 가진 채 손에 붙어,
//   캐릭터 캡슐이 매 이동마다 밀려나 비정상적으로 빨라졌다(캡슐 스윕은 자기 액터만 무시하고 붙은 무기 액터는 무시하지 않는다).
// 실행: 콘솔 "Automation RunTests Valor.Weapons.HeldCollision".

namespace
{
	// 테스트 전용 임시 게임 월드. 액터 초기화(InitializeActorsForPlay)를 해야 SpawnActor가 PostInitializeComponents를 호출한다.
	struct FValorScopedTestWorld
	{
		UWorld* World = nullptr;

		FValorScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ValorHeldWeaponCollisionTestWorld"));
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
	};

	// 충돌이 켜진 프리미티브 컴포넌트 이름 목록(비어 있으면 통과).
	TArray<FString> FindCollidingComponents(const AActor& Actor)
	{
		TArray<FString> CollidingComponents;
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(&Actor);
		for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (PrimitiveComponent->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
			{
				CollidingComponents.Add(PrimitiveComponent->GetName());
			}
		}

		return CollidingComponents;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorHeldWeaponCollisionHelperTest, "Valor.Weapons.HeldCollision.DisablesAddedComponents", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorHeldWeaponCollisionHelperTest::RunTest(const FString& Parameters)
{
	FValorScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("테스트 월드 생성"), TestWorld.World))
	{
		return false;
	}

	AActor* WeaponActor = TestWorld.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("테스트 액터 생성"), WeaponActor))
	{
		return false;
	}

	// BP에서 부품을 추가한 것과 같은 상황: 기본 설정 그대로인 StaticMesh 컴포넌트.
	UStaticMeshComponent* AddedPart = NewObject<UStaticMeshComponent>(WeaponActor, TEXT("AddedPart"));
	WeaponActor->SetRootComponent(AddedPart);
	AddedPart->RegisterComponent();

	// 전제: StaticMesh 컴포넌트는 기본으로 충돌이 켜져 있다(그래서 버그가 났다). 이 전제가 깨지면 아래 검사가 의미 없다.
	TestTrue(TEXT("StaticMesh 기본 충돌은 켜져 있음"), AddedPart->GetCollisionEnabled() != ECollisionEnabled::NoCollision);

	ValorInventory::DisableHeldWeaponCollision(*WeaponActor);

	TestEqual(TEXT("추가된 부품 충돌 꺼짐"), AddedPart->GetCollisionEnabled(), ECollisionEnabled::Type(ECollisionEnabled::NoCollision));
	TestFalse(TEXT("겹침 이벤트 꺼짐"), AddedPart->GetGenerateOverlapEvents());
	TestEqual(TEXT("캐릭터가 밟고 올라설 수 없음"), AddedPart->CanCharacterStepUpOn.GetValue(), ECanBeCharacterBase::ECB_No);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorHeldWeaponCollisionBlueprintsTest, "Valor.Weapons.HeldCollision.AllGunBlueprints", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorHeldWeaponCollisionBlueprintsTest::RunTest(const FString& Parameters)
{
	// /Game/Valor/Gun의 총 BP를 모두 실제로 생성해 본다. 새 총 BP에 부품 메시를 추가해도 이 테스트가 같은 문제를 잡는다.
	TArray<FString> AssetFiles;
	IFileManager::Get().FindFiles(AssetFiles, *(FPaths::ProjectContentDir() / TEXT("Valor/Gun/BP_*.uasset")), true, false);

	FValorScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("테스트 월드 생성"), TestWorld.World))
	{
		return false;
	}

	int32 CheckedWeaponCount = 0;
	for (const FString& AssetFile : AssetFiles)
	{
		const FString AssetName = FPaths::GetBaseFilename(AssetFile);
		const FString ClassPath = FString::Printf(TEXT("/Game/Valor/Gun/%s.%s_C"), *AssetName, *AssetName);
		UClass* LoadedClass = LoadObject<UClass>(nullptr, *ClassPath);
		if (!LoadedClass || !LoadedClass->IsChildOf(AValorWeaponBase::StaticClass()))
		{
			// 줍기 BP 등 총이 아닌 BP는 건너뛴다.
			continue;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* SpawnedWeapon = TestWorld.World->SpawnActor<AActor>(LoadedClass, FTransform::Identity, SpawnParameters);
		if (!TestNotNull(*FString::Printf(TEXT("%s 생성"), *AssetName), SpawnedWeapon))
		{
			continue;
		}

		const TArray<FString> CollidingComponents = FindCollidingComponents(*SpawnedWeapon);
		TestTrue(
			*FString::Printf(TEXT("%s: 충돌이 켜진 컴포넌트 없음 (남은 것: %s)"), *AssetName, *FString::Join(CollidingComponents, TEXT(", "))),
			CollidingComponents.IsEmpty());

		SpawnedWeapon->Destroy();
		++CheckedWeaponCount;
	}

	if (CheckedWeaponCount == 0)
	{
		AddWarning(TEXT("/Game/Valor/Gun에서 총 BP를 찾지 못해 검사하지 않았습니다."));
	}

	return true;
}

#endif

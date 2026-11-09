#include "AbilitySystem/ValorDamageTypes.h"
#include "GameplayTagContainer.h"
#include "Misc/AutomationTest.h"
#include "ValorGameMode.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

// 피해 적용 규칙(실드 → 체력), 처치 판정, 부위 태그, 부활 지점 선택을 검증한다. 모두 서버 판정 규칙 그 자체다.
// 실행: 콘솔 "Automation RunTests Valor.Combat" / "Automation RunTests Valor.Game".

namespace ValorDamageTests
{
	// 같은 피해를 처치될 때까지 반복해서 맞힌 횟수(서버가 한 발씩 GE를 적용하는 것과 같은 순서).
	int32 ShotsToKill(float Health, float Shield, float DamagePerShot)
	{
		for (int32 Shot = 1; Shot <= 100; ++Shot)
		{
			const FValorDamageResolution Result = ValorDamage::Resolve(Health, Shield, DamagePerShot);
			Health = Result.NewHealth;
			Shield = Result.NewShield;
			if (Result.bKilled)
			{
				return Shot;
			}
		}
		return INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorDamageShieldFirstTest, "Valor.Combat.Damage.ShieldAbsorbsFirst", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorDamageShieldFirstTest::RunTest(const FString& Parameters)
{
	// 실드 50 + 체력 100에 몸샷 40: 실드만 깎인다.
	FValorDamageResolution Result = ValorDamage::Resolve(100.0f, 50.0f, 40.0f);
	TestEqual(TEXT("실드가 먼저 흡수"), Result.NewShield, 10.0f);
	TestEqual(TEXT("체력은 그대로"), Result.NewHealth, 100.0f);
	TestEqual(TEXT("실드 피해 기록"), Result.ShieldDamage, 40.0f);
	TestEqual(TEXT("체력 피해 없음"), Result.HealthDamage, 0.0f);
	TestFalse(TEXT("처치 아님"), Result.bKilled);

	// 실드 10이 남은 상태에서 40: 실드 10을 지우고 30이 체력으로 넘어간다.
	Result = ValorDamage::Resolve(100.0f, 10.0f, 40.0f);
	TestEqual(TEXT("실드 소진"), Result.NewShield, 0.0f);
	TestEqual(TEXT("남은 피해가 체력으로"), Result.NewHealth, 70.0f);

	// 남은 체력보다 큰 피해는 잘린다(히트 확인 "준 피해" 표시가 과장되지 않게).
	Result = ValorDamage::Resolve(20.0f, 0.0f, 160.0f);
	TestEqual(TEXT("체력 0"), Result.NewHealth, 0.0f);
	TestEqual(TEXT("실제 깎인 양은 남은 체력까지"), Result.HealthDamage, 20.0f);
	TestTrue(TEXT("처치"), Result.bKilled);

	// 이미 0인 대상(같은 프레임에 들어온 두 번째 피해)은 다시 처치로 세지 않는다 → 사망 처리·킬 크레딧이 두 번 일어나지 않는다.
	Result = ValorDamage::Resolve(0.0f, 0.0f, 50.0f);
	TestFalse(TEXT("이미 죽은 대상은 처치 아님"), Result.bKilled);
	TestEqual(TEXT("깎인 양 0"), Result.HealthDamage, 0.0f);

	// 음수 피해(잘못된 입력)는 회복이 되지 않는다.
	Result = ValorDamage::Resolve(50.0f, 0.0f, -30.0f);
	TestEqual(TEXT("음수 피해는 무시"), Result.NewHealth, 50.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorDamageVandalTtkTest, "Valor.Combat.Damage.VandalShotsToKill", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorDamageVandalTtkTest::RunTest(const FString& Parameters)
{
	// 피해량 구간 기본값 = 밴달(머리 160 / 몸 40 / 다리 34, 거리 감쇠 없음).
	const FValorDamageRangeStep Vandal;

	// 데스매치 스폰 상태(체력 100 + 중형 실드 50): 발로란트에서 알려진 밴달 처치 탄 수와 같아야 한다.
	TestEqual(TEXT("풀실드 머리 1발"), ValorDamageTests::ShotsToKill(100.0f, 50.0f, Vandal.HeadDamage), 1);
	TestEqual(TEXT("풀실드 몸 4발"), ValorDamageTests::ShotsToKill(100.0f, 50.0f, Vandal.BodyDamage), 4);
	TestEqual(TEXT("풀실드 다리 5발"), ValorDamageTests::ShotsToKill(100.0f, 50.0f, Vandal.LegDamage), 5);

	// 실드 없음: 몸 3발.
	TestEqual(TEXT("실드 없음 몸 3발"), ValorDamageTests::ShotsToKill(100.0f, 0.0f, Vandal.BodyDamage), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorDamageHitZoneTagTest, "Valor.Combat.Damage.HitZoneTags", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorDamageHitZoneTagTest::RunTest(const FString& Parameters)
{
	// 부위 → 스펙 태그 → 부위 왕복(AttributeSet이 킬피드 헤드샷 여부를 이 태그로 읽는다).
	const EValorHitZone Zones[] = {EValorHitZone::Head, EValorHitZone::Body, EValorHitZone::Leg};
	for (const EValorHitZone Zone : Zones)
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(ValorDamage::HitZoneToTag(Zone));
		TestEqual(TEXT("부위 태그 왕복"), ValorDamage::HitZoneFromTags(Tags), Zone);
	}

	TestFalse(TEXT("부위 없음은 태그 없음"), ValorDamage::HitZoneToTag(EValorHitZone::None).IsValid());
	TestEqual(TEXT("빈 태그는 부위 없음"), ValorDamage::HitZoneFromTags(FGameplayTagContainer()), EValorHitZone::None);

	// 산탄이 여러 부위를 맞히면 대표 부위는 가장 치명적인 곳(머리 > 몸 > 다리).
	TestEqual(TEXT("몸 + 머리 = 머리"), ValorDamage::MoreSevereZone(EValorHitZone::Body, EValorHitZone::Head), EValorHitZone::Head);
	TestEqual(TEXT("다리 + 몸 = 몸"), ValorDamage::MoreSevereZone(EValorHitZone::Leg, EValorHitZone::Body), EValorHitZone::Body);
	TestEqual(TEXT("첫 펠릿(없음) + 다리 = 다리"), ValorDamage::MoreSevereZone(EValorHitZone::None, EValorHitZone::Leg), EValorHitZone::Leg);
	TestEqual(TEXT("머리는 다리에 밀리지 않음"), ValorDamage::MoreSevereZone(EValorHitZone::Head, EValorHitZone::Leg), EValorHitZone::Head);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorSafestSpawnTest, "Valor.Game.Spawn.SafestSpawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorSafestSpawnTest::RunTest(const FString& Parameters)
{
	const TArray<FVector> Starts = {FVector(0.0f, 0.0f, 0.0f), FVector(1000.0f, 0.0f, 0.0f), FVector(3000.0f, 0.0f, 0.0f)};

	TestEqual(TEXT("적이 원점 근처면 가장 먼 3000"), AValorGameMode::PickSafestSpawnIndex(Starts, {FVector(100.0f, 0.0f, 0.0f)}), 2);
	TestEqual(TEXT("적이 3000 근처면 원점"), AValorGameMode::PickSafestSpawnIndex(Starts, {FVector(2900.0f, 0.0f, 0.0f)}), 0);

	// 적이 양 끝에 있으면 "가장 가까운 적까지의 거리"가 최대인 가운데(1000: 가장 가까운 적 1000) — 끝(0: 0)은 위험하다.
	TestEqual(TEXT("양쪽에 적이 있으면 가운데"), AValorGameMode::PickSafestSpawnIndex(Starts, {FVector(0.0f, 0.0f, 0.0f), FVector(3000.0f, 0.0f, 0.0f)}), 1);

	TestEqual(TEXT("적이 없으면 고르지 않음(무작위 기본 규칙)"), AValorGameMode::PickSafestSpawnIndex(Starts, {}), INDEX_NONE);
	TestEqual(TEXT("후보가 없으면 고르지 않음"), AValorGameMode::PickSafestSpawnIndex({}, {FVector::ZeroVector}), INDEX_NONE);
	return true;
}

#endif

#include "Misc/AutomationTest.h"
#include "World/ValorWeaponPickup.h"

#if WITH_DEV_AUTOMATION_TESTS

// 줍기 대상 선택 규칙(조준선 기준)을 검증한다. 픽업은 충돌이 없어서(사격을 막지 않게) 이 수학 판정이 줍기의 전부다.
// 실행: 콘솔 "Automation RunTests Valor.Weapons.Pickup".

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorPickupAimOffsetTest, "Valor.Weapons.Pickup.AimLineSelection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorPickupAimOffsetTest::RunTest(const FString& Parameters)
{
	const FVector ViewLocation(0.0f, 0.0f, 150.0f);
	const FVector ViewDirection(1.0f, 0.0f, 0.0f);
	constexpr float Radius = 120.0f;
	constexpr float MaxDistance = 350.0f;
	float Offset = -1.0f;

	TestTrue(TEXT("정면 2m의 총은 후보"), AValorWeaponPickup::ComputeAimOffset(ViewLocation, ViewDirection, FVector(200.0f, 0.0f, 150.0f), Radius, MaxDistance, Offset));
	TestEqual(TEXT("정면이면 조준선과의 거리 0"), Offset, 0.0f, 1.0e-3f);

	TestFalse(TEXT("등 뒤의 총은 후보가 아니다"), AValorWeaponPickup::ComputeAimOffset(ViewLocation, ViewDirection, FVector(-200.0f, 0.0f, 150.0f), Radius, MaxDistance, Offset));
	TestFalse(TEXT("줍기 거리(3.5m)보다 먼 총은 후보가 아니다"), AValorWeaponPickup::ComputeAimOffset(ViewLocation, ViewDirection, FVector(400.0f, 0.0f, 150.0f), Radius, MaxDistance, Offset));
	TestFalse(TEXT("조준선에서 반경보다 멀면 후보가 아니다"), AValorWeaponPickup::ComputeAimOffset(ViewLocation, ViewDirection, FVector(200.0f, 150.0f, 150.0f), Radius, MaxDistance, Offset));

	TestTrue(TEXT("조준선 옆 1m(반경 안)는 후보"), AValorWeaponPickup::ComputeAimOffset(ViewLocation, ViewDirection, FVector(200.0f, 100.0f, 150.0f), Radius, MaxDistance, Offset));
	TestEqual(TEXT("조준선과의 거리 = 옆으로 벗어난 거리"), Offset, 100.0f, 1.0e-3f);

	// 바닥의 총을 내려다보는 경우: 눈높이 150cm에서 1.5m 앞 바닥을 45° 아래로 본다.
	const FVector LookDown = FVector(1.0f, 0.0f, -1.0f).GetSafeNormal();
	TestTrue(TEXT("45° 아래 바닥의 총은 후보"), AValorWeaponPickup::ComputeAimOffset(ViewLocation, LookDown, FVector(150.0f, 0.0f, 0.0f), Radius, MaxDistance, Offset));
	TestEqual(TEXT("정확히 내려다보면 조준선과의 거리 0"), Offset, 0.0f, 1.0e-2f);
	return true;
}

#endif

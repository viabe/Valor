#include "Components/ValorCharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"
#include "Weapons/ValorInventoryTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

// 무기 슬롯 규칙(슬롯 분류, 교체 예측 조정, 휠 순환, 장착 시각 검증)과 이동 속도 규칙(무기·조준·걷기 배율, 양자화, 서버 검증)을 검증한다.
// 모두 서버와 소유 클라가 같이 쓰는 순수 계산이라, 여기서 맞으면 두 쪽의 결과도 같다.
// 실행: 콘솔 "Automation RunTests Valor.Inventory" / "Automation RunTests Valor.Movement".

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorInventorySlotRulesTest, "Valor.Inventory.SlotRules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorInventorySlotRulesTest::RunTest(const FString& Parameters)
{
	// 권총만 보조무기 칸, 나머지는 모두 주무기 칸.
	TestEqual(TEXT("권총 → 보조무기"), ValorInventory::SlotForCategory(EValorWeaponCategory::Sidearm), EValorWeaponSlot::Secondary);
	TestEqual(TEXT("라이플 → 주무기"), ValorInventory::SlotForCategory(EValorWeaponCategory::Rifle), EValorWeaponSlot::Primary);
	TestEqual(TEXT("저격총 → 주무기"), ValorInventory::SlotForCategory(EValorWeaponCategory::Sniper), EValorWeaponSlot::Primary);
	TestEqual(TEXT("산탄총 → 주무기"), ValorInventory::SlotForCategory(EValorWeaponCategory::Shotgun), EValorWeaponSlot::Primary);

	// 버린 뒤 꺼낼 무기: 주무기 > 보조무기 > 칼.
	TestEqual(TEXT("둘 다 있으면 주무기"), ValorInventory::SelectFallbackSlot(true, true), EValorWeaponSlot::Primary);
	TestEqual(TEXT("권총만 있으면 권총"), ValorInventory::SelectFallbackSlot(false, true), EValorWeaponSlot::Secondary);
	TestEqual(TEXT("총이 없으면 칼"), ValorInventory::SelectFallbackSlot(false, false), EValorWeaponSlot::Melee);

	// 휠 순환: 1 → 2 → 3 → 1, 빈 칸은 건너뛴다.
	TestEqual(TEXT("주무기 다음은 보조무기"), ValorInventory::CycleSlot(EValorWeaponSlot::Primary, 1, true, true), EValorWeaponSlot::Secondary);
	TestEqual(TEXT("칼 다음은 다시 주무기"), ValorInventory::CycleSlot(EValorWeaponSlot::Melee, 1, true, true), EValorWeaponSlot::Primary);
	TestEqual(TEXT("주무기 이전은 칼"), ValorInventory::CycleSlot(EValorWeaponSlot::Primary, -1, true, true), EValorWeaponSlot::Melee);
	TestEqual(TEXT("보조무기가 없으면 주무기 다음은 칼"), ValorInventory::CycleSlot(EValorWeaponSlot::Primary, 1, true, false), EValorWeaponSlot::Melee);
	TestEqual(TEXT("칼만 있으면 그대로"), ValorInventory::CycleSlot(EValorWeaponSlot::Melee, 1, false, false), EValorWeaponSlot::Melee);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorInventoryPredictionAckTest, "Valor.Inventory.PredictionAck", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorInventoryPredictionAckTest::RunTest(const FString& Parameters)
{
	// 서버가 클라의 마지막 요청까지 처리했을 때만 서버 상태를 받아들인다.
	TestTrue(TEXT("처리 번호 = 마지막 요청"), ValorInventory::IsRequestAcknowledged(5, 5));
	TestFalse(TEXT("요청 2개(4, 5) 중 4까지만 처리 → 낡은 상태"), ValorInventory::IsRequestAcknowledged(4, 5));
	TestTrue(TEXT("요청이 없는 처음 상태"), ValorInventory::IsRequestAcknowledged(0, 0));

	// 1바이트 순환: 255 다음 요청은 0. 순환을 사이에 두고도 "아직 처리 안 된 요청이 있다"를 맞게 판정해야 한다.
	TestTrue(TEXT("255까지 처리, 클라도 255"), ValorInventory::IsRequestAcknowledged(255, 255));
	TestFalse(TEXT("255까지 처리, 클라는 순환해서 0까지 보냄 → 아직"), ValorInventory::IsRequestAcknowledged(255, 0));
	TestFalse(TEXT("254까지 처리, 클라는 255·0 두 개 더 보냄 → 아직"), ValorInventory::IsRequestAcknowledged(254, 0));
	TestTrue(TEXT("순환 뒤 0까지 처리 = 완료"), ValorInventory::IsRequestAcknowledged(0, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorInventoryEquipTimeClampTest, "Valor.Inventory.EquipRequestTimeClamp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorInventoryEquipTimeClampTest::RunTest(const FString& Parameters)
{
	// 정상 클라: 누른 시각이 서버 현재보다 지연만큼 과거 → 그대로 장착 시작 시각이 된다(클라 예측과 서버 완료 시각 일치).
	TestEqual(TEXT("지연 50ms는 그대로"), ValorInventory::ClampEquipRequestTime(99.95f, 100.0, 0.25f), 99.95f, 1.0e-4f);

	// 조작: 1초 전에 눌렀다고 주장 → 되감기 한도(0.25초)까지만 인정. 오퍼레이터 장착 1.5초를 0.5초로 줄일 수 없다.
	TestEqual(TEXT("너무 먼 과거는 한도로 자름"), ValorInventory::ClampEquipRequestTime(99.0f, 100.0, 0.25f), 99.75f, 1.0e-4f);

	// 미래 시각(클라 시계가 앞섬)은 서버 현재로.
	TestEqual(TEXT("미래 시각은 서버 현재"), ValorInventory::ClampEquipRequestTime(100.3f, 100.0, 0.25f), 100.0f, 1.0e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorMovementWeaponSpeedTest, "Valor.Movement.WeaponSpeeds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorMovementWeaponSpeedTest::RunTest(const FString& Parameters)
{
	constexpr float BaseRunSpeed = 675.0f;
	constexpr float WalkRatio = 0.61f;
	constexpr float CrouchRatio = 0.36f;

	// 구조체 기본값 = 밴달(힙 0.8, ADS 0.76).
	const FValorWeaponConfig Vandal;
	const float VandalHip = ValorMovement::DequantizeSpeedMultiplier(ValorMovement::QuantizeSpeedMultiplier(Vandal.HipFire.MoveSpeedMultiplier));
	const float VandalADS = ValorMovement::DequantizeSpeedMultiplier(ValorMovement::QuantizeSpeedMultiplier(Vandal.HipFire.MoveSpeedMultiplier * Vandal.AltFire.MoveSpeedMultiplier));

	// 위키: 칼 6.75m/s, 밴달 5.4m/s, 밴달 조준 4.104m/s. 양자화 오차는 1/255(약 0.4%) 이내.
	TestEqual(TEXT("칼 달리기 675"), ValorMovement::ComputeMaxGroundSpeed(BaseRunSpeed, 1.0f, false, false, WalkRatio, CrouchRatio), 675.0f, 0.01f);
	TestEqual(TEXT("밴달 달리기 540"), ValorMovement::ComputeMaxGroundSpeed(BaseRunSpeed, VandalHip, false, false, WalkRatio, CrouchRatio), 540.0f, 1.0f);
	TestEqual(TEXT("밴달 조준 410.4"), ValorMovement::ComputeMaxGroundSpeed(BaseRunSpeed, VandalADS, false, false, WalkRatio, CrouchRatio), 410.4f, 1.5f);

	// 걷기 61%(Riot 측정), 앉기는 걷기보다 우선.
	TestEqual(TEXT("밴달 걷기 = 540 × 0.61"), ValorMovement::ComputeMaxGroundSpeed(BaseRunSpeed, VandalHip, true, false, WalkRatio, CrouchRatio), 540.0f * WalkRatio, 1.0f);
	TestEqual(TEXT("앉아서 걷기 키 → 앉기 속도"), ValorMovement::ComputeMaxGroundSpeed(BaseRunSpeed, VandalHip, true, true, WalkRatio, CrouchRatio), 540.0f * CrouchRatio, 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorMovementQuantizeTest, "Valor.Movement.SpeedQuantization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorMovementQuantizeTest::RunTest(const FString& Parameters)
{
	// 양자화 → 복원 → 다시 양자화하면 같은 바이트(클라가 쓴 값과 서버가 받은 값이 비트 단위로 같다).
	for (int32 Value = 1; Value <= 255; ++Value)
	{
		const uint8 Quantized = static_cast<uint8>(Value);
		if (ValorMovement::QuantizeSpeedMultiplier(ValorMovement::DequantizeSpeedMultiplier(Quantized)) != Quantized)
		{
			AddError(FString::Printf(TEXT("양자화 왕복 불일치: %d"), Value));
			return false;
		}
	}

	TestEqual(TEXT("배율 1 = 255"), ValorMovement::QuantizeSpeedMultiplier(1.0f), static_cast<uint8>(255));
	TestEqual(TEXT("0.8 = 204(정확히 0.8)"), ValorMovement::QuantizeSpeedMultiplier(0.8f), static_cast<uint8>(204));
	TestTrue(TEXT("0은 1로 막는다(속도 0으로 멈추는 잘못된 값 방지)"), ValorMovement::QuantizeSpeedMultiplier(0.0f) >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorMovementServerValidationTest, "Valor.Movement.ServerSpeedValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorMovementServerValidationTest::RunTest(const FString& Parameters)
{
	constexpr uint8 Knife = 255;
	constexpr uint8 Rifle = 204;
	constexpr uint8 RifleADS = 155;
	constexpr float Grace = 0.3f;

	// 서버도 라이플: 라이플 주장은 그대로.
	TestEqual(TEXT("일치하는 주장"), ValorMovement::ResolveClaimedSpeedMultiplier(Rifle, Rifle, Rifle, 10.0, Grace), Rifle);

	// 더 느린 주장(클라가 먼저 조준)은 항상 받아들인다.
	TestEqual(TEXT("느린 주장 허용"), ValorMovement::ResolveClaimedSpeedMultiplier(RifleADS, Rifle, Rifle, 10.0, Grace), RifleADS);

	// 칼 → 라이플로 바꾼 직후: 교체 전에 시뮬레이션된 칼 속도 이동이 늦게 도착해도 유예 시간 안이면 허용(보정 없음).
	TestEqual(TEXT("교체 직후 이전 속도 허용"), ValorMovement::ResolveClaimedSpeedMultiplier(Knife, Rifle, Knife, 0.1, Grace), Knife);

	// 유예 시간이 지났는데도 칼 속도 주장(라이플을 든 채 빨리 달리기 조작) → 서버 속도로 자른다.
	TestEqual(TEXT("유예 후 빠른 주장은 자름"), ValorMovement::ResolveClaimedSpeedMultiplier(Knife, Rifle, Knife, 1.0, Grace), Rifle);

	// 서버 상태가 바뀐 적 없는데 빠른 주장 → 자름.
	TestEqual(TEXT("근거 없는 빠른 주장은 자름"), ValorMovement::ResolveClaimedSpeedMultiplier(Knife, Rifle, Rifle, 0.0, Grace), Rifle);
	return true;
}

#endif

#include "Misc/AutomationTest.h"
#include "Weapons/ValorMeleeWeapon.h"

#if WITH_DEV_AUTOMATION_TESTS

// 칼 공격 규칙: 뒤치기 판정 각도, 판정 광선 배치(조준 중심 우선), 피해·간격·판정 크기의 위키 관계.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorMeleeRulesTest, "Valor.Weapons.Melee.Rules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorMeleeRulesTest::RunTest(const FString& Parameters)
{
	// 맞는 사람은 원점에서 +X를 본다.
	const FVector Victim = FVector::ZeroVector;
	const FVector VictimForward = FVector::ForwardVector;
	TestTrue(TEXT("바로 뒤에서 찌르면 뒤치기"), ValorMelee::IsBackstab(FVector(-120.0f, 0.0f, 0.0f), Victim, VictimForward, 60.0f));
	TestTrue(TEXT("등 뒤 45도에서도 뒤치기"), ValorMelee::IsBackstab(FVector(-100.0f, -100.0f, 0.0f), Victim, VictimForward, 60.0f));
	TestFalse(TEXT("옆(90도)은 뒤치기 아님"), ValorMelee::IsBackstab(FVector(0.0f, 120.0f, 0.0f), Victim, VictimForward, 60.0f));
	TestFalse(TEXT("정면은 뒤치기 아님"), ValorMelee::IsBackstab(FVector(120.0f, 0.0f, 0.0f), Victim, VictimForward, 60.0f));
	TestTrue(TEXT("높이 차이는 보지 않음(수평 방향만)"), ValorMelee::IsBackstab(FVector(-120.0f, 0.0f, 80.0f), Victim, VictimForward, 60.0f));

	// 판정 광선: 중심 1 + 반경 절반 고리 8 + 바깥 고리 8. 고리 번호가 커지는 순서(조준 중심 우선 판정이 이 순서에 기댄다).
	const FVector Aim = FVector(1.0f, 1.0f, 0.3f).GetSafeNormal();
	TArray<FVector> Offsets;
	TArray<int32> Rings;
	ValorMelee::BuildProbeOffsets(Aim, 30.0f, Offsets, Rings);
	TestEqual(TEXT("광선 17개"), Offsets.Num(), 17);
	TestEqual(TEXT("고리 번호 17개"), Rings.Num(), 17);
	TestTrue(TEXT("0번은 조준 중심"), Offsets.Num() > 0 && Offsets[0].IsNearlyZero() && Rings[0] == 0);
	bool bSorted = true;
	bool bPerpendicular = true;
	bool bRadiusMatches = true;
	for (int32 Index = 1; Index < Offsets.Num(); ++Index)
	{
		bSorted &= Rings[Index] >= Rings[Index - 1];
		bPerpendicular &= FMath::Abs(FVector::DotProduct(Offsets[Index], Aim)) < 0.01f;
		const float ExpectedRadius = Rings[Index] == 1 ? 15.0f : 30.0f;
		bRadiusMatches &= FMath::IsNearlyEqual(Offsets[Index].Size(), ExpectedRadius, 0.01f);
	}
	TestTrue(TEXT("고리 순서로 놓임"), bSorted);
	TestTrue(TEXT("오프셋은 조준 방향에 수직"), bPerpendicular);
	TestTrue(TEXT("고리 반경 = 절반 / 전체"), bRadiusMatches);

	ValorMelee::BuildProbeOffsets(Aim, 0.0f, Offsets, Rings);
	TestEqual(TEXT("반경 0이면 중심 광선 하나"), Offsets.Num(), 1);

	// 위키 Melee 수치와 관계(좌클릭 판정이 더 크고 길다, 우클릭은 회복이 길다).
	const AValorMeleeWeapon* Knife = GetDefault<AValorMeleeWeapon>();
	TestEqual(TEXT("앞 베기 50"), Knife->GetAttackDamage(false, false), 50.0f);
	TestEqual(TEXT("앞 찌르기 75"), Knife->GetAttackDamage(true, false), 75.0f);
	TestEqual(TEXT("뒤 베기 100"), Knife->GetAttackDamage(false, true), 100.0f);
	TestEqual(TEXT("뒤 찌르기 150(실드 50 + 체력 100도 한 방)"), Knife->GetAttackDamage(true, true), 150.0f);
	TestTrue(TEXT("사거리 2m 이하, 좌클릭이 더 길다"), Knife->GetAttackRange(false) <= 200.0f && Knife->GetAttackRange(false) > Knife->GetAttackRange(true));
	TestTrue(TEXT("좌클릭 판정이 더 굵다"), Knife->GetAttackRadius(false) > Knife->GetAttackRadius(true));
	TestTrue(TEXT("우클릭 회복이 더 길다"), Knife->GetAttackInterval(true) > Knife->GetAttackInterval(false));
	TestEqual(TEXT("좌클릭 3연속"), Knife->GetPrimaryComboLength(), 3);
	return true;
}

#endif

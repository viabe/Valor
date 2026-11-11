#include "Misc/AutomationTest.h"
#include "ValorMatchGameMode.h"
#include "ValorMatchTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

// 경기 규칙의 순수 계산(경제·승리 조건·공수·팀 배정·스폰 나누기)을 검증한다.
// 실제 라운드 진행·상점은 개발 도구 Valor.Tools.MatchFlow(-game, ?game=Match)로 확인한다.
// 실행: 콘솔 "Automation RunTests Valor.Match".

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorMatchEconomyTest, "Valor.Match.Rules.Economy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorMatchEconomyTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("승리 3000"), ValorMatchRules::ComputeRoundReward(true, 0), 3000);
	TestEqual(TEXT("1연패 1900"), ValorMatchRules::ComputeRoundReward(false, 1), 1900);
	TestEqual(TEXT("2연패 2400"), ValorMatchRules::ComputeRoundReward(false, 2), 2400);
	TestEqual(TEXT("3연패 2900"), ValorMatchRules::ComputeRoundReward(false, 3), 2900);
	TestEqual(TEXT("5연패도 2900"), ValorMatchRules::ComputeRoundReward(false, 5), 2900);
	TestEqual(TEXT("상한 9000"), ValorMatchRules::ClampCredits(9500), 9000);
	TestEqual(TEXT("음수 없음"), ValorMatchRules::ClampCredits(-100), 0);

	// 권총이 상점에 없으면 시작 크레딧을 가장 싼 총 값까지 올린다(800 → 버키 850). 권총이 있으면 800 그대로.
	TestEqual(TEXT("권총 없음: 가장 싼 총 850"), ValorMatchRules::ComputeEconomyResetCredits(800, false, 850), 850);
	TestEqual(TEXT("권총 있음: 800"), ValorMatchRules::ComputeEconomyResetCredits(800, true, 850), 800);
	TestEqual(TEXT("가장 싼 총이 800보다 싸면 800"), ValorMatchRules::ComputeEconomyResetCredits(800, false, 500), 800);
	TestEqual(TEXT("상점이 비면 800"), ValorMatchRules::ComputeEconomyResetCredits(800, false, 0), 800);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorMatchWinnerTest, "Valor.Match.Rules.MatchWinner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorMatchWinnerTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("12:5 진행 중"), ValorMatchRules::GetMatchWinner(12, 5), EValorTeam::None);
	TestEqual(TEXT("13:5 A 승"), ValorMatchRules::GetMatchWinner(13, 5), EValorTeam::TeamA);
	TestEqual(TEXT("11:13 B 승"), ValorMatchRules::GetMatchWinner(11, 13), EValorTeam::TeamB);
	TestEqual(TEXT("13:12 연장 진행 중(2점 차 필요)"), ValorMatchRules::GetMatchWinner(13, 12), EValorTeam::None);
	TestEqual(TEXT("14:12 A 승"), ValorMatchRules::GetMatchWinner(14, 12), EValorTeam::TeamA);
	TestEqual(TEXT("15:15 진행 중"), ValorMatchRules::GetMatchWinner(15, 15), EValorTeam::None);
	TestEqual(TEXT("15:17 B 승"), ValorMatchRules::GetMatchWinner(15, 17), EValorTeam::TeamB);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorMatchSidesTest, "Valor.Match.Rules.Sides", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorMatchSidesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("1라운드 A 공격"), ValorMatchRules::GetAttackingTeam(1), EValorTeam::TeamA);
	TestEqual(TEXT("12라운드 A 공격"), ValorMatchRules::GetAttackingTeam(12), EValorTeam::TeamA);
	TestEqual(TEXT("13라운드(후반) B 공격"), ValorMatchRules::GetAttackingTeam(13), EValorTeam::TeamB);
	TestEqual(TEXT("24라운드 B 공격"), ValorMatchRules::GetAttackingTeam(24), EValorTeam::TeamB);
	TestEqual(TEXT("25라운드(연장 1) A 공격"), ValorMatchRules::GetAttackingTeam(25), EValorTeam::TeamA);
	TestEqual(TEXT("26라운드(연장 2) B 공격"), ValorMatchRules::GetAttackingTeam(26), EValorTeam::TeamB);
	TestFalse(TEXT("24라운드는 연장 아님"), ValorMatchRules::IsOvertimeRound(24));
	TestTrue(TEXT("25라운드는 연장"), ValorMatchRules::IsOvertimeRound(25));
	TestTrue(TEXT("1라운드 경제 초기화"), ValorMatchRules::IsEconomyResetRound(1));
	TestTrue(TEXT("13라운드 경제 초기화"), ValorMatchRules::IsEconomyResetRound(13));
	TestFalse(TEXT("2라운드는 초기화 아님"), ValorMatchRules::IsEconomyResetRound(2));
	TestFalse(TEXT("25라운드는 초기화 아님(연장은 크레딧만 5000)"), ValorMatchRules::IsEconomyResetRound(25));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorMatchTeamsAndSpawnsTest, "Valor.Match.Rules.TeamsAndSpawns", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorMatchTeamsAndSpawnsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("0:0 → A"), AValorMatchGameMode::PickTeamForNewPlayer(0, 0), EValorTeam::TeamA);
	TestEqual(TEXT("1:0 → B"), AValorMatchGameMode::PickTeamForNewPlayer(1, 0), EValorTeam::TeamB);
	TestEqual(TEXT("1:1 → A"), AValorMatchGameMode::PickTeamForNewPlayer(1, 1), EValorTeam::TeamA);
	TestEqual(TEXT("3:4 → A"), AValorMatchGameMode::PickTeamForNewPlayer(3, 4), EValorTeam::TeamA);
	TestEqual(TEXT("상대 팀"), ValorTeams::GetOpposingTeam(EValorTeam::TeamA), EValorTeam::TeamB);
	TestEqual(TEXT("팀 없음의 상대는 없음"), ValorTeams::GetOpposingTeam(EValorTeam::None), EValorTeam::None);

	// 시작점 6개: 왼쪽 무리 3개(x ≈ 0), 오른쪽 무리 3개(x ≈ 3000). 서로 먼 두 점 기준으로 나뉘어야 한다.
	const TArray<FVector> Locations = {
		FVector(0, 0, 0), FVector(100, 200, 0), FVector(-50, -150, 0),
		FVector(3000, 0, 0), FVector(3100, 150, 0), FVector(2950, -200, 0)};
	TArray<int32> Groups;
	AValorMatchGameMode::SplitSpawnGroups(Locations, Groups);
	TestEqual(TEXT("무리 수만큼 결과"), Groups.Num(), Locations.Num());
	TestTrue(TEXT("왼쪽 셋은 같은 무리"), Groups[0] == Groups[1] && Groups[1] == Groups[2]);
	TestTrue(TEXT("오른쪽 셋은 같은 무리"), Groups[3] == Groups[4] && Groups[4] == Groups[5]);
	TestTrue(TEXT("두 무리는 다르다"), Groups[0] != Groups[3]);
	return true;
}

#endif

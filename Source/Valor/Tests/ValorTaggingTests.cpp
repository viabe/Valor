#include "Components/ValorTaggingTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// 피격 감속 규칙: 모양(걸림 → 유지 → 회복), 이어 맞기, 서버 검증 허용치, 세기 고르기.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorTaggingRulesTest, "Valor.Movement.Tagging", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorTaggingRulesTest::RunTest(const FString& Parameters)
{
	const FValorTaggingConfig Config;
	auto Near = [](float A, float B) { return FMath::IsNearlyEqual(A, B, 0.005f); };

	// 모양: 0.1초 동안 걸리고 0.3초까지 유지, 0.5초에 풀림.
	TestTrue(TEXT("맞기 전 0"), Near(ValorTagging::ComputeEnvelope(-0.01f, Config), 0.0f));
	TestTrue(TEXT("0.05초 = 절반"), Near(ValorTagging::ComputeEnvelope(0.05f, Config), 0.5f));
	TestTrue(TEXT("0.1초 = 다 걸림"), Near(ValorTagging::ComputeEnvelope(0.1f, Config), 1.0f));
	TestTrue(TEXT("0.3초까지 유지"), Near(ValorTagging::ComputeEnvelope(0.3f, Config), 1.0f));
	TestTrue(TEXT("0.4초 = 절반 회복"), Near(ValorTagging::ComputeEnvelope(0.4f, Config), 0.5f));
	TestTrue(TEXT("0.5초 = 풀림"), Near(ValorTagging::ComputeEnvelope(0.5f, Config), 0.0f));

	// 표준 72.5%: 다 걸리면 속도 27.5%(3.0 패치의 데드존 27.5%와 같다).
	const FValorTagState Hit = ValorTagging::AddHit(FValorTagState(), 10.0, Config.StandardStrength, Config);
	TestTrue(TEXT("감속 없는 상태는 배율 1"), Near(ValorTagging::ComputeSpeedMultiplier(FValorTagState(), 10.0, Config), 1.0f));
	TestTrue(TEXT("다 걸리면 속도 27.5%"), Near(ValorTagging::ComputeSpeedMultiplier(Hit, 10.2, Config), 0.275f));
	TestTrue(TEXT("0.5초 뒤 원래 속도"), Near(ValorTagging::ComputeSpeedMultiplier(Hit, 10.6, Config), 1.0f));

	// 이어 맞기: 회복 중(모양 0.5)에 다시 맞으면 같은 높이에서 다시 걸리고(속도가 튀지 않음) 다시 끝까지 유지된다.
	const FValorTagState Again = ValorTagging::AddHit(Hit, 10.4, Config.StandardStrength, Config);
	TestTrue(TEXT("다시 맞은 순간 속도가 이어짐"), Near(ValorTagging::ComputeSpeedMultiplier(Again, 10.4, Config), ValorTagging::ComputeSpeedMultiplier(Hit, 10.4, Config)));
	TestTrue(TEXT("0.05초 뒤 다 걸림"), Near(ValorTagging::ComputeSpeedMultiplier(Again, 10.45, Config), 0.275f));
	TestTrue(TEXT("다시 유지(10.65까지)"), Near(ValorTagging::ComputeSpeedMultiplier(Again, 10.64, Config), 0.275f));

	// 세기는 센 쪽: 72.5% 감속 중에 벽 관통(25%)을 맞아도 약해지지 않는다.
	const FValorTagState Weaker = ValorTagging::AddHit(Hit, 10.2, Config.WallPenetrationStrength, Config);
	TestTrue(TEXT("센 감속 유지"), Near(ValorTagging::ComputeSpeedMultiplier(Weaker, 10.25, Config), 0.275f));

	// 이동 시계: 클라가 받은 이동 시각(5.0)부터 같은 모양. 서버도 같은 이동 시각으로 시작하므로 같은 값이다.
	const FValorTagMoveClock Clock = ValorTagging::AcknowledgeOnMoveClock(FValorTagMoveClock(), Hit, 5.0f, Config);
	TestEqual(TEXT("받은 순번"), static_cast<int32>(Clock.Sequence), static_cast<int32>(Hit.Sequence));
	TestTrue(TEXT("받기 전 시계는 감속 없음"), Near(ValorTagging::ComputeMoveClockMultiplier(FValorTagMoveClock(), 5.2f, Config), 1.0f));
	TestTrue(TEXT("받은 이동에서는 아직 그대로"), Near(ValorTagging::ComputeMoveClockMultiplier(Clock, 5.0f, Config), 1.0f));
	TestTrue(TEXT("0.2초 뒤 27.5%"), Near(ValorTagging::ComputeMoveClockMultiplier(Clock, 5.2f, Config), 0.275f));
	TestTrue(TEXT("0.5초 뒤 회복"), Near(ValorTagging::ComputeMoveClockMultiplier(Clock, 5.5f, Config), 1.0f));

	// 회복 중 다시 받음: 같은 높이에서 이어진다.
	const FValorTagState HitAgain = ValorTagging::AddHit(Hit, 10.4, Config.StandardStrength, Config);
	const FValorTagMoveClock ClockAgain = ValorTagging::AcknowledgeOnMoveClock(Clock, HitAgain, 5.4f, Config);
	TestTrue(TEXT("다시 받은 순간 이어짐"), Near(ValorTagging::ComputeMoveClockMultiplier(ClockAgain, 5.4f, Config), ValorTagging::ComputeMoveClockMultiplier(Clock, 5.4f, Config)));
	TestTrue(TEXT("순번이 오른다"), static_cast<uint8>(Hit.Sequence + 1) == HitAgain.Sequence);

	// 서버 허용치: 이동 시각 ± 여유(0.02초) 중 빠른 쪽. 걸리는 중에는 조금 앞선 값, 정확히 같은 계산이면 클라 주장이 항상 허용된다.
	const float Claimed = ValorTagging::ComputeMoveClockMultiplier(Clock, 5.05f, Config);
	const float Allowed = ValorTagging::ComputeAllowedMoveClockMultiplier(Clock, 5.05f, 0.02f, Config);
	TestTrue(TEXT("같은 계산의 주장은 허용"), Claimed <= Allowed + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("여유는 0.02초만큼만"), Near(Allowed, ValorTagging::ComputeMoveClockMultiplier(Clock, 5.03f, Config)));
	TestTrue(TEXT("감속을 무시한 주장(1.0)은 유지 구간에서 허용되지 않음"), ValorTagging::ComputeAllowedMoveClockMultiplier(Clock, 5.2f, 0.02f, Config) < 0.3f);

	// 세기: 표준 72.5%, 벽 관통 25%, 산탄 10m 밖 30%, 둘 다면 약한 쪽.
	TestTrue(TEXT("라이플 직접 72.5%"), Near(ValorTagging::SelectStrength(false, 2000.0f, false, Config), 0.725f));
	TestTrue(TEXT("벽 관통 25%"), Near(ValorTagging::SelectStrength(false, 500.0f, true, Config), 0.25f));
	TestTrue(TEXT("산탄 10m 안 72.5%"), Near(ValorTagging::SelectStrength(true, 800.0f, false, Config), 0.725f));
	TestTrue(TEXT("산탄 10m 밖 30%"), Near(ValorTagging::SelectStrength(true, 1200.0f, false, Config), 0.30f));
	TestTrue(TEXT("산탄 10m 밖 + 관통 = 25%"), Near(ValorTagging::SelectStrength(true, 1200.0f, true, Config), 0.25f));
	return true;
}

#endif

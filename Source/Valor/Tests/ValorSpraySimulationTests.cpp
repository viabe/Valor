#include "Components/ValorCameraKickSpring.h"
#include "Misc/AutomationTest.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"
#include "Weapons/ValorSpraySimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

// 발로란트식 스프레이 시뮬레이션이 설계대로(결정적, 패턴 진행, 회복, 탭 효율, 자세 배율) 동작하는지 검증한다.
// 실행: 에디터 Session Frontend → Automation → "Valor.Weapons.Spray" 또는 콘솔 "Automation RunTests Valor.Weapons.Spray".

namespace ValorSprayTests
{
	constexpr int32 TestWeaponSeed = 12345;

	FValorComputedShotData Fire(const FValorWeaponConfig& Config, const FValorShooterStance& Stance, FValorSprayState& State, double ShotTime, int32 ShotCounter)
	{
		return ValorSpray::AdvanceShot(Config, Stance, State, ShotTime, ValorSpray::MakeShotSeed(TestWeaponSeed, ShotCounter));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorSprayFullAutoTest, "Valor.Weapons.Spray.FullAutoFollowsPattern", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorSprayFullAutoTest::RunTest(const FString& Parameters)
{
	const FValorWeaponConfig Config;
	const FValorShooterStance Stance;
	FValorSprayState State;
	const double Interval = ValorSpray::GetFireInterval(Config, false);

	for (int32 ShotIndex = 0; ShotIndex < Config.MagazineSize; ++ShotIndex)
	{
		const FValorComputedShotData Shot = ValorSprayTests::Fire(Config, Stance, State, ShotIndex * Interval, ShotIndex);
		TestEqual(FString::Printf(TEXT("풀오토 %d번째 발의 스프레이 진행도"), ShotIndex), Shot.SprayIndex, static_cast<float>(ShotIndex), 1.0e-3f);

		// 보호 탄 6발(인덱스 0~5)과 방향이 정해지는 7번째 발(인덱스 6)까지는 수평 반동이 없어야 한다(곧은 수직 상승).
		if (ShotIndex <= Config.RecoilProfile.ProtectedBulletCount)
		{
			TestEqual(FString::Printf(TEXT("보호 구간 %d번째 발의 수평 반동"), ShotIndex), Shot.RecoilYawDegrees, 0.0f, 1.0e-4f);
		}
	}

	FValorSprayState FreshState;
	const FValorComputedShotData FirstShot = ValorSprayTests::Fire(Config, Stance, FreshState, 0.0, 0);
	TestEqual(TEXT("첫 발은 반동이 없다"), FirstShot.RecoilPitchDegrees, 0.0f, 1.0e-4f);
	TestEqual(TEXT("첫 발 탄퍼짐 = 1st Shot Spread 0.25도"), FirstShot.FiringErrorDegrees, 0.25f, 1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorSprayRecoveryTest, "Valor.Weapons.Spray.GunRecoveryTimeResetsSpray", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorSprayRecoveryTest::RunTest(const FString& Parameters)
{
	const FValorWeaponConfig Config;
	const FValorShooterStance Stance;
	FValorSprayState State;
	const double Interval = ValorSpray::GetFireInterval(Config, false);

	for (int32 ShotIndex = 0; ShotIndex < 10; ++ShotIndex)
	{
		ValorSprayTests::Fire(Config, Stance, State, ShotIndex * Interval, ShotIndex);
	}

	const double LastShotTime = 9.0 * Interval;
	const FValorSprayEvaluation Holding = ValorSpray::Evaluate(Config, Stance, State, LastShotTime + Interval);
	TestEqual(TEXT("연사 여유 시간 안에서는 회복하지 않는다"), Holding.SprayIndex, 10.0f, 1.0e-3f);

	const FValorSprayEvaluation Recovered = ValorSpray::Evaluate(Config, Stance, State, LastShotTime + Config.RecoilProfile.GunRecoveryTime + 0.001);
	TestEqual(TEXT("Gun Recovery Time 이후 진행도 0"), Recovered.SprayIndex, 0.0f, 1.0e-4f);
	TestEqual(TEXT("Gun Recovery Time 이후 반동 0"), Recovered.RecoilPitchDegrees, 0.0f, 1.0e-4f);

	const FValorComputedShotData NextShot = ValorSprayTests::Fire(Config, Stance, State, LastShotTime + Config.RecoilProfile.GunRecoveryTime + 0.001, 10);
	TestEqual(TEXT("완전 회복 후 첫 발은 새 스프레이"), NextShot.ShotIndexInSpray, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorSprayTapEfficiencyTest, "Valor.Weapons.Spray.TapEfficiencySlowsAccrual", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorSprayTapEfficiencyTest::RunTest(const FString& Parameters)
{
	const FValorWeaponConfig Config;
	const FValorShooterStance Stance;

	auto SteadyStateIndex = [&Config, &Stance](double TapInterval)
	{
		FValorSprayState State;
		float LastIndex = 0.0f;
		for (int32 TapIndex = 0; TapIndex < 12; ++TapIndex)
		{
			LastIndex = ValorSprayTests::Fire(Config, Stance, State, TapIndex * TapInterval, TapIndex).SprayIndex;
		}
		return LastIndex;
	};

	// 0.3초 간격 탭은 거의 첫 발 정확도를 유지하고, 0.2초 간격은 조금씩 쌓이며, 연사에 가까울수록 패턴을 따라간다.
	const float SlowTap = SteadyStateIndex(0.30);
	const float MediumTap = SteadyStateIndex(0.20);
	const float FastTap = SteadyStateIndex(0.15);
	TestTrue(FString::Printf(TEXT("0.3초 탭 진행도(%.2f) < 0.3"), SlowTap), SlowTap < 0.3f);
	TestTrue(FString::Printf(TEXT("0.2초 탭 진행도(%.2f)는 1~2.5 사이"), MediumTap), MediumTap > 1.0f && MediumTap < 2.5f);
	TestTrue(FString::Printf(TEXT("빠른 탭일수록 더 부정확(%.2f > %.2f)"), FastTap, MediumTap), FastTap > MediumTap);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorSprayDeterminismTest, "Valor.Weapons.Spray.DeterministicAcrossInstances", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorSprayDeterminismTest::RunTest(const FString& Parameters)
{
	// 서버 인스턴스와 클라 인스턴스가 같은 입력(시각·시드 번호·자세)으로 같은 결과를 내는지 = 클라 예측이 서버와 일치하는지.
	const FValorWeaponConfig Config;
	FValorShooterStance Stance;
	Stance.HorizontalSpeed = 120.0f;

	FValorSprayState ServerState;
	FValorSprayState ClientState;
	const double Interval = ValorSpray::GetFireInterval(Config, false);
	const FRotator Aim(3.0f, 45.0f, 0.0f);

	for (int32 ShotIndex = 0; ShotIndex < Config.MagazineSize; ++ShotIndex)
	{
		const double ShotTime = 100.0 + ShotIndex * Interval;
		const FValorComputedShotData ServerShot = ValorSprayTests::Fire(Config, Stance, ServerState, ShotTime, ShotIndex);
		const FValorComputedShotData ClientShot = ValorSprayTests::Fire(Config, Stance, ClientState, ShotTime, ShotIndex);

		TestEqual(FString::Printf(TEXT("%d번째 발 수직 반동 일치"), ShotIndex), ClientShot.RecoilPitchDegrees, ServerShot.RecoilPitchDegrees);
		TestEqual(FString::Printf(TEXT("%d번째 발 수평 반동 일치"), ShotIndex), ClientShot.RecoilYawDegrees, ServerShot.RecoilYawDegrees);
		TestTrue(FString::Printf(TEXT("%d번째 발 탄 방향 일치"), ShotIndex),
			ValorSpray::ComputeShotDirection(Aim, ClientShot).Equals(ValorSpray::ComputeShotDirection(Aim, ServerShot), 1.0e-6f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorSprayStanceTest, "Valor.Weapons.Spray.StanceModifiers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorSprayStanceTest::RunTest(const FString& Parameters)
{
	const FValorWeaponConfig Config;
	const double Interval = ValorSpray::GetFireInterval(Config, false);

	auto PitchAtShot = [&Config, Interval](const FValorShooterStance& Stance, int32 TargetShot)
	{
		FValorSprayState State;
		float Pitch = 0.0f;
		for (int32 ShotIndex = 0; ShotIndex <= TargetShot; ++ShotIndex)
		{
			Pitch = ValorSprayTests::Fire(Config, Stance, State, ShotIndex * Interval, ShotIndex).RecoilPitchDegrees;
		}
		return Pitch;
	};

	FValorShooterStance Standing;
	FValorShooterStance Crouched;
	Crouched.bIsCrouched = true;
	FValorShooterStance Running;
	Running.HorizontalSpeed = Running.RunSpeed;

	const float StandingPitch = PitchAtShot(Standing, 6);
	TestEqual(TEXT("7번째 발(인덱스 6) 수직 반동 = 6.30도(영상 측정 기반 커브)"), StandingPitch, 6.30f, 1.0e-3f);
	TestEqual(TEXT("앉아서 정지 시 반동 ×0.85"), PitchAtShot(Crouched, 6), StandingPitch * 0.85f, 1.0e-3f);
	TestEqual(TEXT("최고 속도로 달리며 사격 시 수직 반동 ×1.8"), PitchAtShot(Running, 6), StandingPitch * 1.8f, 1.0e-3f);

	FValorSprayState State;
	const FValorComputedShotData RunningShot = ValorSprayTests::Fire(Config, Running, State, 0.0, 0);
	TestEqual(TEXT("달리기 첫 발 탄퍼짐 = 0.25 + 6"), RunningShot.FiringErrorDegrees, 6.25f, 1.0e-3f);

	FValorComputedShotData ZeroShot;
	const FRotator Aim(10.0f, 30.0f, 0.0f);
	TestTrue(TEXT("반동·탄퍼짐 0이면 조준 방향 그대로"), ValorSpray::ComputeShotDirection(Aim, ZeroShot).Equals(Aim.Vector(), 1.0e-6f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorSprayYawTest, "Valor.Weapons.Spray.YawTracksSideWithinAmplitude", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorSprayYawTest::RunTest(const FString& Parameters)
{
	// 수평 반동: 보호 탄 이후에만 벌어지고, 진폭(최대 2.8도) 안에 머물며, 한 발 사이 이동량은 전환 속도를 넘지 않아야 한다.
	const FValorWeaponConfig Config;
	const FValorShooterStance Stance;
	const double Interval = ValorSpray::GetFireInterval(Config, false);
	const float MaxStepPerShot = 2.0f * 2.8f / Config.RecoilProfile.YawSwitchTime * static_cast<float>(Interval) + 1.0e-3f;

	for (int32 Seed = 0; Seed < 20; ++Seed)
	{
		FValorSprayState State;
		float PreviousYaw = 0.0f;
		float MaxAbsYaw = 0.0f;
		for (int32 ShotIndex = 0; ShotIndex < Config.MagazineSize; ++ShotIndex)
		{
			const FValorComputedShotData Shot = ValorSpray::AdvanceShot(Config, Stance, State, ShotIndex * Interval, ValorSpray::MakeShotSeed(1000 + Seed, ShotIndex));
			TestTrue(FString::Printf(TEXT("시드 %d, %d번째 발 수평 이동량이 전환 속도 이내"), Seed, ShotIndex), FMath::Abs(Shot.RecoilYawDegrees - PreviousYaw) <= MaxStepPerShot);
			PreviousYaw = Shot.RecoilYawDegrees;
			MaxAbsYaw = FMath::Max(MaxAbsYaw, FMath::Abs(Shot.RecoilYawDegrees));
		}

		TestTrue(FString::Printf(TEXT("시드 %d 수평 반동이 진폭 이내(%.2f)"), Seed, MaxAbsYaw), MaxAbsYaw <= 2.8f + 1.0e-3f);
		TestTrue(FString::Printf(TEXT("시드 %d 수평 반동이 실제로 벌어짐(%.2f)"), Seed, MaxAbsYaw), MaxAbsYaw > 1.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorCameraKickSpringTest, "Valor.Camera.KickSpring.PeaksAndSettles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorCameraKickSpringTest::RunTest(const FString& Parameters)
{
	// 한 발 킥: 설정한 시간(0.04초)에 설정한 크기(1도)로 최고점에 닿고, 오버슈트 없이 복귀해야 한다.
	FValorCameraKickSpring Spring;
	Spring.AddKick(FVector(1.0f, 0.0f, 0.0f), 0.04f);

	float PeakDegrees = 0.0f;
	float PeakTime = 0.0f;
	float MinDegrees = 0.0f;
	float Elapsed = 0.0f;
	for (int32 Step = 0; Step < 500; ++Step)
	{
		Spring.Advance(0.001f, 10.0f);
		Elapsed += 0.001f;
		if (Spring.Offset.X > PeakDegrees)
		{
			PeakDegrees = Spring.Offset.X;
			PeakTime = Elapsed;
		}
		MinDegrees = FMath::Min(MinDegrees, static_cast<float>(Spring.Offset.X));
	}

	TestEqual(TEXT("킥 최고점 = 1도"), PeakDegrees, 1.0f, 0.02f);
	TestEqual(TEXT("최고점 도달 시간 = 0.04초"), PeakTime, 0.04f, 0.002f);
	TestTrue(TEXT("임계 감쇠라 아래로 넘어가지 않는다"), MinDegrees > -1.0e-3f);
	TestTrue(TEXT("0.5초 뒤 제자리로 복귀"), FMath::Abs(Spring.Offset.X) < 1.0e-3f);

	// 프레임레이트 무관성: 30fps와 240fps로 같은 시간(0.1초)만큼 진행하면 같은 위치여야 한다(해석해 적분).
	FValorCameraKickSpring Spring30;
	FValorCameraKickSpring Spring240;
	Spring30.AddKick(FVector(1.0f, 0.5f, -0.3f), 0.04f);
	Spring240.AddKick(FVector(1.0f, 0.5f, -0.3f), 0.04f);
	for (int32 Step = 0; Step < 3; ++Step)
	{
		Spring30.Advance(0.1f / 3.0f, 10.0f);
	}
	for (int32 Step = 0; Step < 24; ++Step)
	{
		Spring240.Advance(0.1f / 24.0f, 10.0f);
	}
	TestTrue(TEXT("30fps와 240fps의 킥 궤적 일치"), Spring30.Offset.Equals(Spring240.Offset, 1.0e-4f));
	return true;
}

#endif

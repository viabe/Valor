#include "Misc/AutomationTest.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"
#include "Weapons/ValorSpraySimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

// 점사·발사 속도 가속·산탄·발사 요청 직렬화가 설계대로(서버/클라 같은 규칙) 동작하는지 검증한다.
// 실행: 콘솔 "Automation RunTests Valor.Weapons.FireMode".

namespace ValorFireModeTests
{
	// 직전 발이 정한 최소 간격(NextShotCooldown)대로 쏘고, 각 발 뒤의 간격을 기록한다(클라 발사 루프와 같은 방식).
	TArray<float> FireAtCooldown(const FValorWeaponConfig& Config, const FValorShooterStance& Stance, FValorSprayState& State, double& InOutTime, int32 ShotCount)
	{
		TArray<float> Cooldowns;
		for (int32 ShotIndex = 0; ShotIndex < ShotCount; ++ShotIndex)
		{
			ValorSpray::AdvanceShot(Config, Stance, State, InOutTime, ValorSpray::MakeShotSeed(7, ShotIndex));
			Cooldowns.Add(State.NextShotCooldown);
			InOutTime += State.NextShotCooldown;
		}
		return Cooldowns;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorBurstCadenceTest, "Valor.Weapons.FireMode.BurstCadence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorBurstCadenceTest::RunTest(const FString& Parameters)
{
	// 불독 ADS: 3점사, 점사 안 13.333발/초, 평균 6.316발/초 → 점사 주기 0.475초.
	FValorWeaponConfig Config;
	Config.AltFire.FireRate = 6.315715f;
	Config.AltFire.BurstCount = 3;
	Config.AltFire.BurstFireRate = 13.333f;

	FValorShooterStance Stance;
	Stance.bIsADS = true;
	FValorSprayState State;
	double Time = 10.0;
	const TArray<float> Cooldowns = ValorFireModeTests::FireAtCooldown(Config, Stance, State, Time, 6);

	TestEqual(TEXT("점사 1→2발 간격 = 1/13.333"), Cooldowns[0], 0.075f, 1.0e-3f);
	TestEqual(TEXT("점사 2→3발 간격 = 1/13.333"), Cooldowns[1], 0.075f, 1.0e-3f);
	TestEqual(TEXT("점사를 다 쏜 뒤 대기 = 0.475 - 0.15"), Cooldowns[2], 0.325f, 1.0e-3f);
	TestEqual(TEXT("다음 점사도 같은 리듬"), Cooldowns[3], 0.075f, 1.0e-3f);
	TestEqual(TEXT("다음 점사 끝 대기"), Cooldowns[5], 0.325f, 1.0e-3f);

	// 점사 도중 오래 끊기면(탄창이 비어 멈춘 경우 등) 다음 발은 새 점사의 첫 발이다.
	FValorSprayState CutState;
	double CutTime = 20.0;
	ValorFireModeTests::FireAtCooldown(Config, Stance, CutState, CutTime, 2);
	TestEqual(TEXT("점사 2발째 뒤 남은 발 있음"), CutState.ShotsInBurst, 2);
	ValorSpray::AdvanceShot(Config, Stance, CutState, CutTime + 1.0, ValorSpray::MakeShotSeed(7, 100));
	TestEqual(TEXT("1초 끊긴 뒤 발은 새 점사의 첫 발"), CutState.ShotsInBurst, 1);

	// 힙(점사 아님)은 기본 간격 그대로.
	FValorShooterStance HipStance;
	FValorSprayState HipState;
	double HipTime = 30.0;
	const TArray<float> HipCooldowns = ValorFireModeTests::FireAtCooldown(Config, HipStance, HipState, HipTime, 3);
	TestEqual(TEXT("힙파이어는 1/FireRate"), HipCooldowns[2], 1.0f / Config.HipFire.FireRate, 1.0e-4f);
	TestEqual(TEXT("힙파이어는 점사 상태를 쓰지 않는다"), HipState.ShotsInBurst, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorSpinUpCadenceTest, "Valor.Weapons.FireMode.SpinUpCadence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorSpinUpCadenceTest::RunTest(const FString& Parameters)
{
	// 오딘 힙파이어: 12발/초에서 시작해 1초 동안 이어 쏘면 15.6발/초.
	FValorWeaponConfig Config;
	Config.HipFire.FireRate = 12.0f;
	Config.HipFire.SpinUpMaxFireRate = 15.6f;
	Config.HipFire.SpinUpTimeSeconds = 1.0f;

	const FValorShooterStance Stance;
	FValorSprayState State;
	double Time = 5.0;
	const TArray<float> Cooldowns = ValorFireModeTests::FireAtCooldown(Config, Stance, State, Time, 25);

	TestEqual(TEXT("첫 발 뒤 간격 = 1/12"), Cooldowns[0], 1.0f / 12.0f, 1.0e-4f);
	TestTrue(TEXT("쏠수록 간격이 줄어든다"), Cooldowns[10] < Cooldowns[1]);
	TestEqual(TEXT("1초 넘게 이어 쏘면 1/15.6"), Cooldowns.Last(), 1.0f / 15.6f, 1.0e-4f);

	// 0.5초 쉬었다 다시 쏘면 처음 속도부터.
	ValorSpray::AdvanceShot(Config, Stance, State, Time + 0.5, ValorSpray::MakeShotSeed(7, 200));
	TestEqual(TEXT("끊긴 뒤 다시 쏘면 1/12부터"), State.NextShotCooldown, 1.0f / 12.0f, 1.0e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorPelletSpreadTest, "Valor.Weapons.FireMode.PelletSpread", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorPelletSpreadTest::RunTest(const FString& Parameters)
{
	// 저지처럼 첫 발 탄퍼짐 2.5°인 총의 한 발 = 12펠릿.
	FValorWeaponConfig Config;
	Config.HipFire.FirstShotError = 2.5f;
	Config.HipFire.MaxFiringError = 2.5f;

	const FValorShooterStance Stance;
	FValorSprayState State;
	const FValorComputedShotData Shot = ValorSpray::AdvanceShot(Config, Stance, State, 1.0, ValorSpray::MakeShotSeed(99, 0));
	const FRotator Aim(0.0f, 0.0f, 0.0f);
	const FVector Center = ValorSpray::ComputeRecoilDirection(Aim, Shot);

	TestTrue(TEXT("0번 펠릿 = 단발 탄 방향"), ValorSpray::ComputePelletDirection(Aim, Shot, 0).Equals(ValorSpray::ComputeShotDirection(Aim, Shot), 1.0e-6f));

	TSet<FIntVector> DistinctDirections;
	for (int32 PelletIndex = 0; PelletIndex < 12; ++PelletIndex)
	{
		const FVector Direction = ValorSpray::ComputePelletDirection(Aim, Shot, PelletIndex);
		const float AngleFromCenter = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Direction, Center), -1.0f, 1.0f)));
		TestTrue(FString::Printf(TEXT("%d번 펠릿이 탄퍼짐 원(2.5°) 안(%.2f°)"), PelletIndex, AngleFromCenter), AngleFromCenter <= 2.5f + 0.01f);
		TestTrue(FString::Printf(TEXT("%d번 펠릿은 같은 입력이면 같은 방향(서버/클라 일치)"), PelletIndex),
			Direction.Equals(ValorSpray::ComputePelletDirection(Aim, Shot, PelletIndex), 0.0f));
		DistinctDirections.Add(FIntVector(FMath::RoundToInt(Direction.Y * 10000.0f), FMath::RoundToInt(Direction.Z * 10000.0f), 0));
	}

	TestTrue(TEXT("펠릿들이 서로 다른 곳으로 퍼진다"), DistinctDirections.Num() >= 11);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorShotRequestSerializationTest, "Valor.Weapons.FireMode.ShotRequestSerialization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorShotRequestSerializationTest::RunTest(const FString& Parameters)
{
	// 발사 요청은 시각 + 조준(16비트 압축) + 우클릭 발사 1비트. 직렬화 → 역직렬화 후 같은 값이어야 한다.
	for (const bool bAltFire : {false, true})
	{
		FValorShotRequest Sent;
		Sent.ClientShotTime = 123.456f;
		Sent.AimRotation = FRotator(-12.5f, 250.0f, 0.0f);
		Sent.bAltFire = bAltFire;
		Sent.Quantize();

		FBitWriter Writer(0, true);
		bool bWriteSuccess = false;
		Sent.NetSerialize(Writer, nullptr, bWriteSuccess);

		FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
		FValorShotRequest Received;
		bool bReadSuccess = false;
		Received.NetSerialize(Reader, nullptr, bReadSuccess);

		TestTrue(TEXT("직렬화 성공"), bWriteSuccess && bReadSuccess && !Reader.IsError());
		TestTrue(TEXT("우클릭 발사 여부 유지"), Received.bAltFire == bAltFire);
		TestEqual(TEXT("발사 시각 유지"), Received.ClientShotTime, Sent.ClientShotTime);
		TestTrue(TEXT("조준 유지(양자화 후 값과 비트 단위로 같음)"), Received.AimRotation.Equals(Sent.AimRotation, 1.0e-4f));
		TestTrue(TEXT("크기 = 시각 32비트 + 각도 16비트 × 2 + 1비트"), Writer.GetNumBits() == 65);
	}

	return true;
}

#endif

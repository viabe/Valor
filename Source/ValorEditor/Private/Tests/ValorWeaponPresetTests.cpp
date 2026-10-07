#include "Misc/AutomationTest.h"
#include "Weapons/ValorSpraySimulation.h"
#include "Weapons/ValorWeaponPresets.h"

#if WITH_DEV_AUTOMATION_TESTS

// 무기 프리셋(데이터 에셋 생성 원본)이 공식 수치를 담고 있고, 스프레이 시뮬레이션에 넣었을 때 규칙대로 동작하는지 검증한다.
// 실행: 콘솔 "Automation RunTests Valor.Weapons.Presets".

namespace ValorPresetTests
{
	const FValorWeaponPreset* FindPreset(const TArray<FValorWeaponPreset>& Presets, const TCHAR* WeaponName)
	{
		return Presets.FindByPredicate([WeaponName](const FValorWeaponPreset& Preset) { return Preset.WeaponId == FName(WeaponName); });
	}

	float EvalCurve(const FRuntimeFloatCurve& Curve, float X)
	{
		const FRichCurve* RichCurve = Curve.GetRichCurveConst();
		return RichCurve && RichCurve->GetNumKeys() > 0 ? RichCurve->Eval(X) : 0.0f;
	}

	bool IsNonDecreasing(const FRuntimeFloatCurve& Curve, float MaxX)
	{
		float Previous = EvalCurve(Curve, 0.0f);
		for (float X = 0.25f; X <= MaxX; X += 0.25f)
		{
			const float Value = EvalCurve(Curve, X);
			if (Value + 1.0e-4f < Previous)
			{
				return false;
			}
			Previous = Value;
		}
		return true;
	}

	// 에셋과 프리셋을 비교할 핵심 수치를 한 줄로 만든다(다르면 어느 값이 다른지 경고 메시지로 바로 보이게).
	FString DescribeKeyValues(const FValorWeaponConfig& Config)
	{
		const FValorRecoilProfile& Recoil = Config.RecoilProfile;
		FString Damage;
		for (const FValorDamageRangeStep& Step : Config.DamageRanges)
		{
			Damage += FString::Printf(TEXT("[%.0fcm %.2f/%.2f/%.2f]"), Step.MaxDistanceCm, Step.HeadDamage, Step.BodyDamage, Step.LegDamage);
		}

		return FString::Printf(
			TEXT("Cost=%d Mag=%d/%d Reload=%.2f Auto=%d FR=%.4f/%.4f Err=%.4f/%.4f ADSErr=%.4f/%.4f Pellet=%d/%d Burst=%d Silenced=%d Protected=%d Recovery=%.3f Tap=%.2f YawTime=%.2f V3=%.2f V6=%.2f V10=%.2f H10=%.2f E3=%.3f Deadzone=%.3f Move=%.2f/%.2f/%.2f/%.2f Damage=%s"),
			Config.Cost, Config.MagazineSize, Config.MaxReserveAmmo, Config.ReloadDuration, Config.bAutomatic ? 1 : 0,
			Config.HipFire.FireRate, Config.AltFire.FireRate, Config.HipFire.FirstShotError, Config.HipFire.MaxFiringError,
			Config.AltFire.FirstShotError, Config.AltFire.MaxFiringError, Config.HipFire.PelletCount, Config.AltFire.PelletCount,
			Config.AltFire.BurstCount, Config.bSilenced ? 1 : 0, Recoil.ProtectedBulletCount, Recoil.GunRecoveryTime, Recoil.TapEfficiency,
			Recoil.YawSwitchTime, EvalCurve(Recoil.VerticalRecoilCurve, 3.0f), EvalCurve(Recoil.VerticalRecoilCurve, 6.0f),
			EvalCurve(Recoil.VerticalRecoilCurve, 10.0f), EvalCurve(Recoil.HorizontalRecoilAmplitudeCurve, 10.0f),
			EvalCurve(Recoil.FiringErrorCurve, 3.0f), Config.MovementAccuracy.DeadzoneSpeedRatio, Config.MovementAccuracy.CrouchMovingError,
			Config.MovementAccuracy.WalkingError, Config.MovementAccuracy.RunningError, Config.MovementAccuracy.AirborneError, *Damage);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorWeaponPresetSanityTest, "Valor.Weapons.Presets.AllWeaponsAreValid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorWeaponPresetSanityTest::RunTest(const FString& Parameters)
{
	const TArray<FValorWeaponPreset> Presets = ValorWeaponPresets::BuildAll();
	TestEqual(TEXT("발로란트 무기 20종"), Presets.Num(), 20);

	TSet<FName> SeenIds;
	for (const FValorWeaponPreset& Preset : Presets)
	{
		const FValorWeaponConfig& Config = Preset.Config;
		const FString Name = Preset.WeaponId.ToString();

		TestFalse(FString::Printf(TEXT("%s: WeaponId 중복 없음"), *Name), SeenIds.Contains(Preset.WeaponId));
		SeenIds.Add(Preset.WeaponId);
		TestEqual(FString::Printf(TEXT("%s: 에셋 ID = 설정 ID"), *Name), Config.WeaponId.ToString(), Name);
		TestFalse(FString::Printf(TEXT("%s: 출처 메모 있음"), *Name), Preset.SourceNotes.IsEmpty());

		TestTrue(FString::Printf(TEXT("%s: 탄창/발사 속도 양수"), *Name), Config.MagazineSize > 0 && Config.HipFire.FireRate > 0.0f && Config.AltFire.FireRate > 0.0f);
		TestTrue(FString::Printf(TEXT("%s: 탄퍼짐 음수 아님"), *Name), Config.HipFire.FirstShotError >= 0.0f && Config.HipFire.MaxFiringError >= 0.0f && Config.AltFire.FirstShotError >= 0.0f);

		// 피해 구간은 가까운 구간부터, 거리가 늘어날수록 피해가 줄거나 같아야 한다(발로란트 감쇠는 계단식 감소).
		TestTrue(FString::Printf(TEXT("%s: 피해 구간 있음"), *Name), Config.DamageRanges.Num() > 0);
		for (int32 Index = 1; Index < Config.DamageRanges.Num(); ++Index)
		{
			const FValorDamageRangeStep& Near = Config.DamageRanges[Index - 1];
			const FValorDamageRangeStep& Far = Config.DamageRanges[Index];
			TestTrue(FString::Printf(TEXT("%s: 피해 구간 %d 거리 증가"), *Name, Index), Far.MaxDistanceCm > Near.MaxDistanceCm);
			TestTrue(FString::Printf(TEXT("%s: 피해 구간 %d 감쇠"), *Name, Index), Far.HeadDamage <= Near.HeadDamage && Far.BodyDamage <= Near.BodyDamage && Far.LegDamage <= Near.LegDamage);
		}

		// 수직 반동은 쏠수록 줄어들지 않고(누적값), 탄퍼짐 보간 비율은 0~1 안에 있어야 한다.
		const float MaxIndex = static_cast<float>(Config.MagazineSize);
		TestTrue(FString::Printf(TEXT("%s: 수직 반동 커브 단조 증가"), *Name), ValorPresetTests::IsNonDecreasing(Config.RecoilProfile.VerticalRecoilCurve, MaxIndex));
		TestTrue(FString::Printf(TEXT("%s: 탄퍼짐 커브 단조 증가"), *Name), ValorPresetTests::IsNonDecreasing(Config.RecoilProfile.FiringErrorCurve, MaxIndex));
		TestTrue(FString::Printf(TEXT("%s: 탄퍼짐 커브 0~1"), *Name),
			ValorPresetTests::EvalCurve(Config.RecoilProfile.FiringErrorCurve, 0.0f) >= 0.0f && ValorPresetTests::EvalCurve(Config.RecoilProfile.FiringErrorCurve, MaxIndex) <= 1.0f + 1.0e-4f);

		// ADS가 없는 무기는 줌이 없어야 하고, 우클릭 수치가 힙과 같아야 한다(지금은 우클릭을 모두 정조준으로 처리하므로).
		if (Config.AltFireType == EValorAltFireType::None)
		{
			TestEqual(FString::Printf(TEXT("%s: ADS 없음 → 줌 1"), *Name), Config.ADSZoomMultiplier, 1.0f);
			TestEqual(FString::Printf(TEXT("%s: ADS 없음 → 우클릭 발사 속도 = 힙"), *Name), Config.AltFire.FireRate, Config.HipFire.FireRate);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorWeaponPresetOfficialTest, "Valor.Weapons.Presets.OfficialNumbers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorWeaponPresetOfficialTest::RunTest(const FString& Parameters)
{
	using ValorPresetTests::FindPreset;
	const TArray<FValorWeaponPreset> Presets = ValorWeaponPresets::BuildAll();

	// 밴달 프리셋 = 구조체 기본값(영상 실측으로 맞춘 밴달). 둘이 어긋나면 DA_Vandal과 코드 기본값이 서로 다른 총이 된다.
	if (const FValorWeaponPreset* Vandal = FindPreset(Presets, TEXT("Vandal")))
	{
		const FValorWeaponConfig Defaults;
		TestEqual(TEXT("밴달 발사 속도 = 기본값"), Vandal->Config.HipFire.FireRate, Defaults.HipFire.FireRate);
		TestEqual(TEXT("밴달 7번째 발 수직 반동 = 기본값(6.30도)"), ValorPresetTests::EvalCurve(Vandal->Config.RecoilProfile.VerticalRecoilCurve, 6.0f), 6.30f, 1.0e-3f);
		TestEqual(TEXT("밴달 보호 탄 = 6"), Vandal->Config.RecoilProfile.ProtectedBulletCount, 6);
	}
	else
	{
		AddError(TEXT("밴달 프리셋이 없다"));
	}

	if (const FValorWeaponPreset* Phantom = FindPreset(Presets, TEXT("Phantom")))
	{
		const FValorWeaponConfig& Config = Phantom->Config;
		TestEqual(TEXT("팬텀 발사 속도 11 / ADS 9.9"), Config.HipFire.FireRate + Config.AltFire.FireRate, 20.9f, 1.0e-3f);
		TestEqual(TEXT("팬텀 탄창 30 / 예비 60"), Config.MagazineSize * 1000 + Config.MaxReserveAmmo, 30060);
		TestEqual(TEXT("팬텀 첫 발 0.2 / 최대 0.9"), Config.HipFire.FirstShotError + Config.HipFire.MaxFiringError, 1.1f, 1.0e-4f);
		TestEqual(TEXT("팬텀 보호 탄 8"), Config.RecoilProfile.ProtectedBulletCount, 8);
		TestEqual(TEXT("팬텀 회복 0.35초"), Config.RecoilProfile.GunRecoveryTime, 0.35f, 1.0e-4f);
		TestEqual(TEXT("팬텀 탭 효율 4"), Config.RecoilProfile.TapEfficiency, 4.0f, 1.0e-4f);
		TestTrue(TEXT("팬텀 소음기"), Config.bSilenced);
		TestEqual(TEXT("팬텀 0~20m 머리 156"), Config.DamageRanges[0].HeadDamage, 156.0f);
		TestEqual(TEXT("팬텀 20m 경계"), Config.DamageRanges[0].MaxDistanceCm, 2000.0f);
		TestEqual(TEXT("팬텀 20m~ 몸 35"), Config.DamageRanges[1].BodyDamage, 35.0f);
	}
	else
	{
		AddError(TEXT("팬텀 프리셋이 없다"));
	}

	if (const FValorWeaponPreset* Spectre = FindPreset(Presets, TEXT("Spectre")))
	{
		const FValorWeaponConfig& Config = Spectre->Config;
		TestEqual(TEXT("스펙터 발사 속도 13.333"), Config.HipFire.FireRate, 13.333f, 1.0e-3f);
		TestEqual(TEXT("스펙터 보호 탄 5"), Config.RecoilProfile.ProtectedBulletCount, 5);
		TestEqual(TEXT("스펙터 수평 전환 0.28초"), Config.RecoilProfile.YawSwitchTime, 0.28f, 1.0e-4f);
		TestEqual(TEXT("스펙터 탭 효율 3"), Config.RecoilProfile.TapEfficiency, 3.0f, 1.0e-4f);
		TestEqual(TEXT("스펙터 달리기 수평 반동 ×1.5"), Config.MovementAccuracy.RunningHorizontalRecoilMultiplier, 1.5f, 1.0e-4f);
		TestEqual(TEXT("스펙터 피해 구간 3개"), Config.DamageRanges.Num(), 3);
	}
	else
	{
		AddError(TEXT("스펙터 프리셋이 없다"));
	}

	if (const FValorWeaponPreset* Operator = FindPreset(Presets, TEXT("Operator")))
	{
		TestEqual(TEXT("오퍼레이터 데드존 15%(1.09)"), Operator->Config.MovementAccuracy.DeadzoneSpeedRatio, 0.15f, 1.0e-4f);
		TestEqual(TEXT("오퍼레이터 2단 줌 5배"), Operator->Config.SecondaryADSZoomMultiplier, 5.0f);
		TestEqual(TEXT("오퍼레이터 줌 탄퍼짐 0"), Operator->Config.AltFire.FirstShotError, 0.0f);
		TestTrue(TEXT("오퍼레이터는 조준경 + 쏘면 조준 해제"), Operator->Config.bUseScopeOverlay && Operator->Config.bUnscopeAfterShot);
	}

	if (const FValorWeaponPreset* Marshal = FindPreset(Presets, TEXT("Marshal")))
	{
		TestTrue(TEXT("마샬은 조준경 + 쏘면 조준 해제"), Marshal->Config.bUseScopeOverlay && Marshal->Config.bUnscopeAfterShot);
	}

	if (const FValorWeaponPreset* Outlaw = FindPreset(Presets, TEXT("Outlaw")))
	{
		// 13.01: 첫 발 뒤 조준 탄퍼짐 2.25°, 반동 4.0°. 쌍열이라 쏴도 조준이 유지된다.
		TestTrue(TEXT("아웃로는 조준경 + 쏴도 조준 유지"), Outlaw->Config.bUseScopeOverlay && !Outlaw->Config.bUnscopeAfterShot);
		TestEqual(TEXT("아웃로 두 번째 발 조준 탄퍼짐 2.25(13.01)"), Outlaw->Config.AltFire.MaxFiringError, 2.25f, 1.0e-4f);
		TestEqual(TEXT("아웃로 두 번째 발 반동 4.0(13.01)"), ValorPresetTests::EvalCurve(Outlaw->Config.RecoilProfile.VerticalRecoilCurve, 1.0f), 4.0f, 1.0e-4f);
	}

	if (const FValorWeaponPreset* Bandit = FindPreset(Presets, TEXT("Bandit")))
	{
		// 13.00: 회복 0.4초, 탭 효율 4, 최대 수직 반동 3.
		TestEqual(TEXT("밴딧 회복 0.4초(13.00)"), Bandit->Config.RecoilProfile.GunRecoveryTime, 0.4f, 1.0e-4f);
		TestEqual(TEXT("밴딧 탭 효율 4(13.00)"), Bandit->Config.RecoilProfile.TapEfficiency, 4.0f, 1.0e-4f);
		TestEqual(TEXT("밴딧 최대 수직 반동 3(13.00)"), ValorPresetTests::EvalCurve(Bandit->Config.RecoilProfile.VerticalRecoilCurve, 20.0f), 3.0f, 1.0e-4f);
	}

	if (const FValorWeaponPreset* Bucky = FindPreset(Presets, TEXT("Bucky")))
	{
		TestEqual(TEXT("버키 펠릿 15 / 우클릭 5"), Bucky->Config.HipFire.PelletCount * 100 + Bucky->Config.AltFire.PelletCount, 1505);
		TestEqual(TEXT("버키 캐니스터 7.5m"), Bucky->Config.AirBurstDistanceCm, 750.0f);
		TestEqual(TEXT("버키 달리기 오차 2(12.09)"), Bucky->Config.MovementAccuracy.RunningError, 2.0f);
	}

	if (const FValorWeaponPreset* Bulldog = FindPreset(Presets, TEXT("Bulldog")))
	{
		// 평균 6.316발/초 = 3발 / (3/13.333 + 0.25초) → 점사 사이 대기 0.25초가 나와야 한다.
		const FValorFireModeStats& Burst = Bulldog->Config.AltFire;
		const float BurstGap = Burst.BurstCount / Burst.FireRate - Burst.BurstCount / Burst.BurstFireRate;
		TestEqual(TEXT("불독 점사 사이 대기 0.25초"), BurstGap, 0.25f, 1.0e-3f);
		TestTrue(TEXT("불독은 조준 직후 발사 지연이 있다(4.07)"), Bulldog->Config.ADSFireDelaySeconds > 0.0f);
	}

	if (const FValorWeaponPreset* Stinger = FindPreset(Presets, TEXT("Stinger")))
	{
		const FValorFireModeStats& Burst = Stinger->Config.AltFire;
		const float BurstGap = Burst.BurstCount / Burst.FireRate - Burst.BurstCount / Burst.BurstFireRate;
		TestEqual(TEXT("스팅어 점사 사이 대기 0.25초"), BurstGap, 0.25f, 1.0e-3f);
		TestTrue(TEXT("스팅어 점사는 별도 반동 규칙(회복 0.4초)"), Stinger->Config.bUseSeparateAltFireRecoil && FMath::IsNearlyEqual(Stinger->Config.AltFireRecoilProfile.GunRecoveryTime, 0.4f));
		TestTrue(TEXT("스팅어는 조준 직후 발사 지연이 있다(4.07)"), Stinger->Config.ADSFireDelaySeconds > 0.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorWeaponPresetSprayTest, "Valor.Weapons.Presets.SprayRules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorWeaponPresetSprayTest::RunTest(const FString& Parameters)
{
	const TArray<FValorWeaponPreset> Presets = ValorWeaponPresets::BuildAll();
	const FValorShooterStance Stance;

	// 모든 연사 무기: 풀오토로 한 탄창을 쏘면 진행도가 한 발씩 오르고, 보호 탄(+방향이 정해지는 다음 발)까지는 수평 반동이 0이어야 한다.
	for (const FValorWeaponPreset& Preset : Presets)
	{
		const FValorWeaponConfig& Config = Preset.Config;
		if (!Config.bAutomatic)
		{
			continue;
		}

		const double Interval = ValorSpray::GetFireInterval(Config, false);
		FValorSprayState State;
		for (int32 ShotIndex = 0; ShotIndex < Config.MagazineSize; ++ShotIndex)
		{
			const FValorComputedShotData Shot = ValorSpray::AdvanceShot(Config, Stance, State, ShotIndex * Interval, ValorSpray::MakeShotSeed(777, ShotIndex));
			if (!FMath::IsNearlyEqual(Shot.SprayIndex, static_cast<float>(ShotIndex), 1.0e-3f))
			{
				AddError(FString::Printf(TEXT("%s: %d번째 발 진행도 %.3f"), *Preset.WeaponId.ToString(), ShotIndex, Shot.SprayIndex));
				break;
			}

			if (ShotIndex <= Config.RecoilProfile.ProtectedBulletCount && !FMath::IsNearlyZero(Shot.RecoilYawDegrees, 1.0e-4f))
			{
				AddError(FString::Printf(TEXT("%s: 보호 구간 %d번째 발에 수평 반동 %.3f"), *Preset.WeaponId.ToString(), ShotIndex, Shot.RecoilYawDegrees));
				break;
			}
		}
	}

	// 스펙터: 탄퍼짐이 3·6·8번째 발(인덱스 2·5·7)에서 계단처럼 커진다(4.0 패치).
	if (const FValorWeaponPreset* Spectre = ValorPresetTests::FindPreset(Presets, TEXT("Spectre")))
	{
		const FValorWeaponConfig& Config = Spectre->Config;
		const double Interval = ValorSpray::GetFireInterval(Config, false);
		FValorSprayState State;
		TArray<float> Errors;
		for (int32 ShotIndex = 0; ShotIndex < 9; ++ShotIndex)
		{
			Errors.Add(ValorSpray::AdvanceShot(Config, Stance, State, ShotIndex * Interval, ValorSpray::MakeShotSeed(1, ShotIndex)).FiringErrorDegrees);
		}

		TestEqual(TEXT("스펙터 1~2번째 발 = 첫 발 탄퍼짐 0.4"), Errors[1], 0.4f, 1.0e-3f);
		TestTrue(TEXT("스펙터 3번째 발에서 탄퍼짐 증가"), Errors[2] > Errors[1] + 0.1f);
		TestEqual(TEXT("스펙터 4~5번째 발은 3번째와 같음"), Errors[4], Errors[2], 1.0e-3f);
		TestTrue(TEXT("스펙터 6번째 발에서 다시 증가"), Errors[5] > Errors[4] + 0.1f);
		TestEqual(TEXT("스펙터 8번째 발부터 최대 1.4"), Errors[7], 1.4f, 1.0e-3f);
	}

	// 아레스: 쏠수록 정확해진다(첫 발 1.0 → 14번째 발 0.7).
	if (const FValorWeaponPreset* Ares = ValorPresetTests::FindPreset(Presets, TEXT("Ares")))
	{
		const FValorWeaponConfig& Config = Ares->Config;
		const double Interval = ValorSpray::GetFireInterval(Config, false);
		FValorSprayState State;
		float FirstError = 0.0f;
		float LaterError = 0.0f;
		for (int32 ShotIndex = 0; ShotIndex <= 13; ++ShotIndex)
		{
			const float Error = ValorSpray::AdvanceShot(Config, Stance, State, ShotIndex * Interval, ValorSpray::MakeShotSeed(2, ShotIndex)).FiringErrorDegrees;
			FirstError = ShotIndex == 0 ? Error : FirstError;
			LaterError = Error;
		}

		TestEqual(TEXT("아레스 첫 발 1.0"), FirstError, 1.0f, 1.0e-3f);
		TestEqual(TEXT("아레스 14번째 발 0.7"), LaterError, 0.7f, 1.0e-3f);
	}

	// 클래식 우클릭: 별도 규칙이라 연속 점사 탄퍼짐이 1.9 → 2.5 → 5.78로 뛴다(좌클릭 커브와 무관).
	if (const FValorWeaponPreset* Classic = ValorPresetTests::FindPreset(Presets, TEXT("Classic")))
	{
		const FValorWeaponConfig& Config = Classic->Config;
		FValorShooterStance AltStance;
		AltStance.bIsADS = true;
		const double Interval = ValorSpray::GetFireInterval(Config, true);
		FValorSprayState State;
		TArray<float> Errors;
		for (int32 ShotIndex = 0; ShotIndex < 3; ++ShotIndex)
		{
			Errors.Add(ValorSpray::AdvanceShot(Config, AltStance, State, ShotIndex * Interval, ValorSpray::MakeShotSeed(3, ShotIndex)).FiringErrorDegrees);
		}

		TestEqual(TEXT("클래식 우클릭 첫 점사 1.9"), Errors[0], 1.9f, 1.0e-3f);
		TestEqual(TEXT("클래식 우클릭 두 번째 점사 약 2.5"), Errors[1], 2.5f, 0.01f);
		TestEqual(TEXT("클래식 우클릭 세 번째 점사 5.78"), Errors[2], 5.78f, 1.0e-3f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorWeaponAssetLoadTest, "Valor.Weapons.Assets.GeneratedAssetsLoad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorWeaponAssetLoadTest::RunTest(const FString& Parameters)
{
	// 무기 20종의 데이터 에셋(DA_<무기>)이 있고, 무기 데이터 에셋으로 제대로 로드되는지 확인한다.
	// 수치가 프리셋과 다르면 오류가 아니라 경고만 낸다: 에디터에서 튜닝한 값일 수 있기 때문이다(프리셋은 초기값일 뿐).
	for (const FValorWeaponPreset& Preset : ValorWeaponPresets::BuildAll())
	{
		const FString AssetName = FString::Printf(TEXT("DA_%s"), *Preset.WeaponId.ToString());
		const FString ObjectPath = FString::Printf(TEXT("/Game/Valor/DataAsset/%s.%s"), *AssetName, *AssetName);
		const UValorWeaponDataAsset* Asset = LoadObject<UValorWeaponDataAsset>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Asset)
		{
			AddError(FString::Printf(TEXT("%s 가 없거나 무기 데이터 에셋이 아니다(커맨드렛 ValorGenerateWeaponAssets로 생성)"), *ObjectPath));
			continue;
		}

		const FValorWeaponConfig& Config = Asset->WeaponConfig;
		TestEqual(FString::Printf(TEXT("%s: WeaponId = 에셋 이름"), *AssetName), Config.WeaponId.ToString(), Preset.WeaponId.ToString());
		TestTrue(FString::Printf(TEXT("%s: 탄창/발사 속도/피해 구간 유효"), *AssetName), Config.MagazineSize > 0 && Config.HipFire.FireRate > 0.0f && Config.DamageRanges.Num() > 0);

		const FString AssetValues = ValorPresetTests::DescribeKeyValues(Config);
		const FString PresetValues = ValorPresetTests::DescribeKeyValues(Preset.Config);
		if (AssetValues != PresetValues)
		{
			AddWarning(FString::Printf(TEXT("%s 수치가 프리셋과 다르다(에디터에서 튜닝했다면 정상)\n  에셋:   %s\n  프리셋: %s"), *AssetName, *AssetValues, *PresetValues));
		}
	}

	return true;
}

#endif

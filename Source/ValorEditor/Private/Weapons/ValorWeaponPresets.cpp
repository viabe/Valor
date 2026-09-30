#include "Weapons/ValorWeaponPresets.h"

// 수치 표기 규칙
// - 각도는 도(°), 시간은 초, 거리는 cm(피해 구간은 공식 표기대로 m를 받아 cm로 바꾼다).
// - 커브 X = 스프레이 진행도(0 = 첫 발, 1 = 두 번째 발 ...). "N번째 발"은 인덱스 N-1이다.
// - [공식] = valorant-api / 위키 표·패치 이력 / 공식 패치노트, [추정] = 공개 자료가 없어 정한 값(SourceNotes에도 같은 구분).

namespace ValorWeaponPresets
{
namespace
{
	struct FCurveKey
	{
		float X;
		float Y;
	};

	struct FDamageRow
	{
		float RangeEndMeters;
		float Head;
		float Body;
		float Leg;
	};

	// 반동 커브는 선형 보간(발 사이 중간값), "N번째 발에서 오차가 뛴다" 같은 계단형 공식 규칙은 RCIM_Constant로 채운다.
	void SetCurve(FRuntimeFloatCurve& Curve, std::initializer_list<FCurveKey> Keys, ERichCurveInterpMode InterpMode = RCIM_Linear)
	{
		FRichCurve* RichCurve = Curve.GetRichCurve();
		RichCurve->Reset();
		for (const FCurveKey& Key : Keys)
		{
			RichCurve->SetKeyInterpMode(RichCurve->AddKey(Key.X, Key.Y), InterpMode);
		}
	}

	FValorWeaponPreset MakeBase(const TCHAR* WeaponName, EValorWeaponCategory Category, int32 Cost, float EquipTimeSeconds)
	{
		FValorWeaponPreset Preset;
		Preset.WeaponId = FName(WeaponName);

		FValorWeaponConfig& Config = Preset.Config;
		Config.WeaponId = Preset.WeaponId;
		Config.DisplayName = FText::FromString(WeaponName);
		Config.Category = Category;
		Config.Cost = Cost;
		Config.EquipTimeSeconds = EquipTimeSeconds;
		Config.AnimationType = Category == EValorWeaponCategory::Sidearm ? EValorWeaponAnimationType::Sidearm : EValorWeaponAnimationType::Rifle;
		return Preset;
	}

	void SetAmmo(FValorWeaponConfig& Config, bool bAutomatic, int32 MagazineSize, int32 MaxReserveAmmo, float ReloadDuration)
	{
		Config.bAutomatic = bAutomatic;
		Config.MagazineSize = MagazineSize;
		Config.MaxReserveAmmo = MaxReserveAmmo;
		Config.ReloadDuration = ReloadDuration;
	}

	void SetFireMode(FValorFireModeStats& Mode, float FireRate, float FirstShotError, float MaxFiringError, float CrouchErrorMultiplier, float MoveSpeedMultiplier)
	{
		Mode.FireRate = FireRate;
		Mode.FirstShotError = FirstShotError;
		Mode.MaxFiringError = MaxFiringError;
		Mode.CrouchErrorMultiplier = CrouchErrorMultiplier;
		Mode.MoveSpeedMultiplier = MoveSpeedMultiplier;
	}

	// [추정] 카메라 킥은 순수 연출이라 공개 수치가 없다. 밴달 실측(힙 0.8° / ADS 0.35°, 0.025초)을 기준으로 총의 체급에 맞춰 정했다.
	void SetCameraKick(FValorFireModeStats& Mode, float PitchDegrees, float YawDegrees, float PeakTimeSeconds, float MaxKickDegrees)
	{
		Mode.CameraKick.PitchDegrees = PitchDegrees;
		Mode.CameraKick.YawDegrees = YawDegrees;
		Mode.CameraKick.PeakTimeSeconds = PeakTimeSeconds;
		Mode.CameraKick.MaxKickDegrees = MaxKickDegrees;
	}

	// ADS가 없는 무기: 현재 입력은 우클릭을 모두 정조준으로 처리하므로, 우클릭해도 힙과 똑같이 동작하게 힙 수치를 복사하고 줌을 끈다.
	// 힙파이어(카메라 킥 포함)를 다 채운 뒤에 호출해야 한다.
	void DisableADS(FValorWeaponConfig& Config)
	{
		Config.AltFireType = EValorAltFireType::None;
		Config.ADSZoomMultiplier = 1.0f;
		Config.AltFire = Config.HipFire;
	}

	void SetRecoilRules(FValorRecoilProfile& Profile, int32 ProtectedBulletCount, float YawSwitchChance, float YawSwitchTime, float MaxHorizontalRecoil, float GunRecoveryTime, float TapEfficiency)
	{
		Profile.ProtectedBulletCount = ProtectedBulletCount;
		Profile.YawSwitchChance = YawSwitchChance;
		Profile.YawSwitchTime = YawSwitchTime;
		Profile.MaxHorizontalRecoil = MaxHorizontalRecoil;
		Profile.GunRecoveryTime = GunRecoveryTime;
		Profile.TapEfficiency = TapEfficiency;
	}

	// 스프레이 패턴이 의미 없는 무기(저격총·펌프 산탄총·쇼티): 연사가 안 되거나 매우 느리고, 공식 탄퍼짐도 첫 발 = 최대다.
	// 수직·수평 반동과 탄퍼짐 증가를 0으로 두고, 쏠 때의 화면 반동은 카메라 킥(연출)으로만 보여 준다.
	void SetNoSpray(FValorRecoilProfile& Profile, float GunRecoveryTime)
	{
		SetCurve(Profile.VerticalRecoilCurve, {{0.0f, 0.0f}});
		SetCurve(Profile.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}});
		SetCurve(Profile.FiringErrorCurve, {{0.0f, 0.0f}});
		SetRecoilRules(Profile, 0, 0.0f, 0.6f, 0.0f, GunRecoveryTime, 1.0f);
	}

	// [공식] 위키 표 "Movement penalties"(앉아 이동 / 걷기 / 달리기 / 공중, +도).
	// [추정] 착지 오차: 라이플 공식값 7(= 공중 10의 70%)만 알려져 있어 다른 무기군은 같은 비율로 둔다.
	void SetMovementError(FValorWeaponConfig& Config, float CrouchMoving, float Walking, float Running, float Airborne)
	{
		FValorMovementAccuracyProfile& Accuracy = Config.MovementAccuracy;
		Accuracy.CrouchMovingError = CrouchMoving;
		Accuracy.WalkingError = Walking;
		Accuracy.RunningError = Running;
		Accuracy.AirborneError = Airborne;
		Accuracy.JumpLandError = Airborne * 0.7f;
	}

	// 반동 자세 배율. 달리기 수직 1.5는 6.11 이전 표준값(6.11에서 밴달·팬텀·스펙터만 1.8로 올림)이라 공개 수치가 없는 무기의 기본으로 쓴다.
	void SetStanceRecoil(FValorWeaponConfig& Config, float CrouchRecoilMultiplier, float RunningVerticalMultiplier, float RunningHorizontalMultiplier)
	{
		FValorMovementAccuracyProfile& Accuracy = Config.MovementAccuracy;
		Accuracy.CrouchRecoilMultiplier = CrouchRecoilMultiplier;
		Accuracy.RunningVerticalRecoilMultiplier = RunningVerticalMultiplier;
		Accuracy.RunningHorizontalRecoilMultiplier = RunningHorizontalMultiplier;
	}

	// 공식은 벽 관통 "등급"만 공개한다. cm·배율은 프로젝트 규칙(밴달 Medium 45cm ×0.7을 기준으로 위아래 등급을 정함).
	void SetPenetration(FValorWeaponConfig& Config, EValorWallPenetrationTier Tier)
	{
		Config.PenetrationTier = Tier;
		switch (Tier)
		{
		case EValorWallPenetrationTier::Low:
			Config.PenetrationDepthCm = 20.0f;
			Config.PenetrationDamageMultiplier = 0.5f;
			break;
		case EValorWallPenetrationTier::High:
			Config.PenetrationDepthCm = 90.0f;
			Config.PenetrationDamageMultiplier = 0.8f;
			break;
		case EValorWallPenetrationTier::Medium:
		default:
			Config.PenetrationDepthCm = 45.0f;
			Config.PenetrationDamageMultiplier = 0.7f;
			break;
		}
	}

	// [공식] valorant-api damageRanges(산탄총은 펠릿 1알 기준). 마지막 구간은 그 너머 전체에 적용된다.
	void SetDamage(FValorWeaponConfig& Config, std::initializer_list<FDamageRow> Rows)
	{
		Config.DamageRanges.Reset();
		for (const FDamageRow& Row : Rows)
		{
			FValorDamageRangeStep& Step = Config.DamageRanges.AddDefaulted_GetRef();
			Step.MaxDistanceCm = Row.RangeEndMeters * 100.0f;
			Step.HeadDamage = Row.Head;
			Step.BodyDamage = Row.Body;
			Step.LegDamage = Row.Leg;
		}
	}

	FString JoinNotes(std::initializer_list<const TCHAR*> Lines)
	{
		FString Notes;
		for (const TCHAR* Line : Lines)
		{
			if (!Notes.IsEmpty())
			{
				Notes += TEXT("\n");
			}
			Notes += Line;
		}
		return Notes;
	}

	// ---------------------------------------------------------------------------------------------
	// 권총(Sidearm) — 공통: 이동 속도 ×0.85(셰리프·쇼티 ×0.8), 우클릭 없음(클래식 제외)
	// ---------------------------------------------------------------------------------------------

	FValorWeaponPreset MakeClassic()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Classic"), EValorWeaponCategory::Sidearm, 0, 0.75f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, false, 12, 36, 1.75f);

		SetFireMode(Config.HipFire, 6.75f, 0.4f, 1.8f, 0.75f, 0.85f);
		SetCameraKick(Config.HipFire, 0.6f, 0.12f, 0.025f, 2.5f);

		// 우클릭: 3펠릿 산탄 점사(탄약 3발 소모), 초당 2.22회. 이동 오차는 좌클릭과 따로 공식 수치가 있다(3.09·9.10 패치).
		Config.AltFireType = EValorAltFireType::Shotgun;
		Config.ADSZoomMultiplier = 1.0f;
		SetFireMode(Config.AltFire, 2.22f, 1.9f, 5.78f, 0.9f, 0.85f);
		Config.AltFire.PelletCount = 3;
		Config.AltFire.AmmoPerShot = 3;
		Config.AltFire.RecoilMultiplier = 1.0f;
		Config.AltFire.CameraRecoilFollowRatio = 0.5f;
		Config.AltFire.bOverrideMovementError = true;
		Config.AltFire.MovementErrorOverride.CrouchMovingError = 0.0f;
		Config.AltFire.MovementErrorOverride.WalkingError = 0.6f;
		Config.AltFire.MovementErrorOverride.RunningError = 1.5f;
		Config.AltFire.MovementErrorOverride.AirborneError = 2.25f;
		SetCameraKick(Config.AltFire, 1.2f, 0.25f, 0.03f, 3.5f);

		// [추정] 좌클릭 연타 반동.
		SetCurve(Config.RecoilProfile.VerticalRecoilCurve, {{0.0f, 0.0f}, {1.0f, 0.5f}, {2.0f, 1.1f}, {3.0f, 1.7f}, {4.0f, 2.2f}, {6.0f, 3.0f}, {8.0f, 3.5f}, {11.0f, 3.8f}});
		SetCurve(Config.RecoilProfile.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}, {1.0f, 0.0f}, {2.0f, 0.3f}, {4.0f, 0.8f}, {8.0f, 1.2f}, {11.0f, 1.3f}});
		SetCurve(Config.RecoilProfile.FiringErrorCurve, {{0.0f, 0.0f}, {1.0f, 0.2f}, {2.0f, 0.4f}, {3.0f, 0.6f}, {4.0f, 0.8f}, {5.0f, 1.0f}});
		SetRecoilRules(Config.RecoilProfile, 1, 0.2f, 0.3f, 2.0f, 0.35f, 3.0f);

		// [공식] 우클릭 연속 점사 탄퍼짐: 첫 점사 1.9 → 두 번째 2.5 → 세 번째부터 6.0(2.0 패치, 현재 표 최대 5.78).
		// 인덱스 1 = (2.5-1.9)/(5.78-1.9) = 0.155. [추정] 우클릭 수직 반동, 회복 시간.
		Config.bUseSeparateAltFireRecoil = true;
		FValorRecoilProfile& AltRecoil = Config.AltFireRecoilProfile;
		SetCurve(AltRecoil.VerticalRecoilCurve, {{0.0f, 0.0f}, {1.0f, 1.0f}, {2.0f, 2.0f}, {3.0f, 2.5f}});
		SetCurve(AltRecoil.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}});
		SetCurve(AltRecoil.FiringErrorCurve, {{0.0f, 0.0f}, {1.0f, 0.155f}, {2.0f, 1.0f}}, RCIM_Constant);
		SetRecoilRules(AltRecoil, 0, 0.0f, 0.6f, 0.0f, 0.6f, 2.0f);

		SetMovementError(Config, 0.5f, 1.1f, 2.3f, 7.0f);
		SetStanceRecoil(Config, 0.85f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::Low);
		SetDamage(Config, {{30.0f, 78.0f, 26.0f, 22.1f}, {50.0f, 66.0f, 22.0f, 18.7f}});

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 0, 반자동 6.75발/초, 탄창 12, 장전 1.75초, 장착 0.75초, 첫 발 0.4°, 이동 ×0.85, 관통 Low, 피해 0~30m 78/26/22.1 · 30m~ 66/22/18.7"),
			TEXT("[공식] 우클릭(altShotgunStats): 3펠릿 산탄, 초당 2.22회 / 위키: 우클릭 첫 발 1.9° 최대 5.78°, 앉기 ×0.9, 이동 오차 0/0.6/1.5/2.25"),
			TEXT("[공식] 위키: 좌클릭 최대 탄퍼짐 1.8°, 앉기 ×0.75, 이동 오차 0.5/1.1/2.3/7, 예비 36 / 2.0 패치: 우클릭 연속 점사 탄퍼짐 1.9 → 2.5 → 6.0"),
			TEXT("[추정] 좌클릭 연타 반동 커브·회복 0.35초·탭 효율 3, 우클릭 수직 반동·회복 0.6초, 카메라 킥"),
			TEXT("[구현 예정] 우클릭 3펠릿 산탄(PelletCount/AmmoPerShot) — 현재는 우클릭이 1발 조준 사격으로 동작"),
		});
		return Preset;
	}

	FValorWeaponPreset MakeShorty()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Shorty"), EValorWeaponCategory::Sidearm, 300, 0.75f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, false, 2, 6, 1.75f);

		SetFireMode(Config.HipFire, 3.0f, 4.0f, 4.0f, 0.75f, 0.8f);
		Config.HipFire.PelletCount = 15;
		SetCameraKick(Config.HipFire, 2.0f, 0.3f, 0.035f, 4.0f);
		SetNoSpray(Config.RecoilProfile, 0.3f);

		SetMovementError(Config, 0.5f, 1.0f, 2.0f, 4.0f);
		SetStanceRecoil(Config, 0.85f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::Low);
		SetDamage(Config, {{7.0f, 22.0f, 11.0f, 9.35f}, {15.0f, 12.0f, 6.0f, 5.1f}, {50.0f, 6.0f, 3.0f, 2.55f}});
		DisableADS(Config);

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 300, 반자동 3발/초, 탄창 2, 장전 1.75초, 장착 0.75초, 펠릿 15, 탄퍼짐 4°, 이동 ×0.8, 관통 Low, 피해(펠릿당) 0~7m 22/11/9.35 · 7~15m 12/6/5.1 · 15m~ 6/3/2.55"),
			TEXT("[공식] 위키: 탄퍼짐 첫 발=최대 4°, 앉기 ×0.75, 이동 오차 0.5/1/2/4(12.09 산탄총 공통 변경), 예비 6"),
			TEXT("[추정] 스프레이 없음(느린 연사), 카메라 킥"),
			TEXT("[구현 예정] 15펠릿 산탄(PelletCount) — 현재는 1발만 판정"),
		});
		return Preset;
	}

	FValorWeaponPreset MakeFrenzy()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Frenzy"), EValorWeaponCategory::Sidearm, 450, 1.0f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, true, 15, 45, 1.5f);

		SetFireMode(Config.HipFire, 10.0f, 0.65f, 1.7f, 0.85f, 0.85f);
		SetCameraKick(Config.HipFire, 0.5f, 0.12f, 0.025f, 2.5f);

		// [공식] "Maximum spread and recoil reached at 5 bullets"(6.11) → 5번째 발(인덱스 4)에서 수직 반동·탄퍼짐이 최대, 이후 거의 유지.
		// [추정] 크기: AimFinder 상대 상승량 × 밴달 실측으로 5번째 발까지 약 4.6°.
		SetCurve(Config.RecoilProfile.VerticalRecoilCurve, {{0.0f, 0.0f}, {1.0f, 0.8f}, {2.0f, 1.9f}, {3.0f, 3.2f}, {4.0f, 4.6f}, {14.0f, 4.75f}});
		SetCurve(Config.RecoilProfile.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}, {2.0f, 0.0f}, {3.0f, 0.24f}, {4.0f, 0.72f}, {5.0f, 1.14f}, {6.0f, 1.44f}, {8.0f, 1.61f}, {12.0f, 1.68f}});
		SetCurve(Config.RecoilProfile.FiringErrorCurve, {{0.0f, 0.0f}, {1.0f, 0.25f}, {2.0f, 0.5f}, {3.0f, 0.75f}, {4.0f, 1.0f}});
		SetRecoilRules(Config.RecoilProfile, 3, 0.1f, 0.4f, 2.2f, 0.35f, 3.0f);

		SetMovementError(Config, 0.5f, 0.8f, 2.0f, 7.0f);
		SetStanceRecoil(Config, 0.85f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::Low);
		SetDamage(Config, {{20.0f, 78.0f, 26.0f, 22.1f}, {50.0f, 63.0f, 21.0f, 17.85f}});
		DisableADS(Config);

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 450, 자동 10발/초, 탄창 15, 장전 1.5초, 장착 1초, 첫 발 0.65°, 이동 ×0.85, 관통 Low, 피해 0~20m 78/26/22.1 · 20m~ 63/21/17.85"),
			TEXT("[공식] 위키: 최대 탄퍼짐 1.7°, 앉기 ×0.85, 이동 오차 0.5/0.8/2/7, 예비 45 / 6.11: 최대 탄퍼짐·반동 5발째 도달, 달리기 수직 반동 ×1.5"),
			TEXT("[추정] 반동 크기(AimFinder 상대값 × 밴달 실측, 약 4.6°에서 유지), 보호 탄 3(AimFinder), 수평 전환 0.4초/10%, 회복 0.35초, 탭 효율 3, 카메라 킥"),
		});
		return Preset;
	}

	FValorWeaponPreset MakeGhost()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Ghost"), EValorWeaponCategory::Sidearm, 500, 0.75f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, false, 13, 39, 1.5f);
		Config.bSilenced = true;

		SetFireMode(Config.HipFire, 6.75f, 0.3f, 1.65f, 0.75f, 0.85f);
		SetCameraKick(Config.HipFire, 0.55f, 0.1f, 0.025f, 2.5f);

		// [추정] 연타 반동(클래식보다 약간 안정적).
		SetCurve(Config.RecoilProfile.VerticalRecoilCurve, {{0.0f, 0.0f}, {1.0f, 0.45f}, {2.0f, 1.0f}, {3.0f, 1.5f}, {4.0f, 2.0f}, {6.0f, 2.7f}, {8.0f, 3.2f}, {12.0f, 3.5f}});
		SetCurve(Config.RecoilProfile.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}, {1.0f, 0.0f}, {2.0f, 0.25f}, {4.0f, 0.7f}, {8.0f, 1.0f}, {12.0f, 1.1f}});
		SetCurve(Config.RecoilProfile.FiringErrorCurve, {{0.0f, 0.0f}, {1.0f, 0.2f}, {2.0f, 0.4f}, {3.0f, 0.6f}, {4.0f, 0.8f}, {5.0f, 1.0f}});
		SetRecoilRules(Config.RecoilProfile, 1, 0.2f, 0.3f, 2.0f, 0.35f, 3.0f);

		SetMovementError(Config, 0.5f, 1.1f, 2.3f, 7.0f);
		SetStanceRecoil(Config, 0.85f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::Medium);
		SetDamage(Config, {{30.0f, 105.0f, 30.0f, 25.5f}, {50.0f, 87.5f, 25.0f, 21.25f}});
		DisableADS(Config);

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 500, 반자동 6.75발/초, 탄창 13, 장전 1.5초, 장착 0.75초, 첫 발 0.3°, 이동 ×0.85, 관통 Medium, 소음기, 피해 0~30m 105/30/25.5 · 30m~ 87.5/25/21.25"),
			TEXT("[공식] 위키: 최대 탄퍼짐 1.65°, 앉기 ×0.75, 이동 오차 0.5/1.1/2.3/7(9.10), 예비 39"),
			TEXT("[추정] 연타 반동 커브, 회복 0.35초, 탭 효율 3, 카메라 킥"),
			TEXT("[구현 예정] 소음기(적 시점 트레이서 숨김, 40m 밖 발사음 감쇠)"),
		});
		return Preset;
	}

	FValorWeaponPreset MakeBandit()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Bandit"), EValorWeaponCategory::Sidearm, 600, 0.75f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, false, 8, 24, 1.5f);

		SetFireMode(Config.HipFire, 5.1f, 0.275f, 1.97f, 0.75f, 0.85f);
		SetCameraKick(Config.HipFire, 0.9f, 0.15f, 0.03f, 3.0f);

		// [추정] 한 발 피해가 큰 권총이라 연타 반동을 고스트·클래식보다 크게 둔다.
		SetCurve(Config.RecoilProfile.VerticalRecoilCurve, {{0.0f, 0.0f}, {1.0f, 0.9f}, {2.0f, 1.9f}, {3.0f, 2.8f}, {4.0f, 3.5f}, {5.0f, 4.0f}, {7.0f, 4.5f}});
		SetCurve(Config.RecoilProfile.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}, {1.0f, 0.0f}, {2.0f, 0.3f}, {4.0f, 0.8f}, {7.0f, 1.1f}});
		SetCurve(Config.RecoilProfile.FiringErrorCurve, {{0.0f, 0.0f}, {1.0f, 0.25f}, {2.0f, 0.5f}, {3.0f, 0.75f}, {4.0f, 1.0f}});
		SetRecoilRules(Config.RecoilProfile, 1, 0.2f, 0.3f, 2.0f, 0.45f, 2.5f);

		SetMovementError(Config, 0.5f, 1.2f, 2.7f, 7.0f);
		SetStanceRecoil(Config, 0.85f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::Medium);
		SetDamage(Config, {{10.0f, 152.0f, 39.0f, 33.0f}, {30.0f, 128.0f, 39.0f, 33.0f}, {50.0f, 112.0f, 34.0f, 28.0f}});
		DisableADS(Config);

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 600, 반자동 5.1발/초, 탄창 8, 장전 1.5초, 장착 0.75초, 첫 발 0.275°, 이동 ×0.85, 관통 Medium, 피해 0~10m 152/39/33 · 10~30m 128/39/33 · 30m~ 112/34/28"),
			TEXT("[공식] 위키: 최대 탄퍼짐 1.97°, 앉기 ×0.75, 이동 오차 0.5/1.2/2.7/7, 예비 24 (12.00 추가 무기)"),
			TEXT("[추정] 연타 반동 커브, 회복 0.45초, 탭 효율 2.5, 카메라 킥"),
		});
		return Preset;
	}

	FValorWeaponPreset MakeSheriff()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Sheriff"), EValorWeaponCategory::Sidearm, 800, 1.0f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, false, 6, 24, 2.25f);

		SetFireMode(Config.HipFire, 4.0f, 0.25f, 2.75f, 0.75f, 0.8f);
		SetCameraKick(Config.HipFire, 2.0f, 0.3f, 0.035f, 5.0f);

		// [추정] 한 발 한 발 크게 튀는 리볼버: 연타하면 두 번째 발부터 확연히 위로 간다.
		SetCurve(Config.RecoilProfile.VerticalRecoilCurve, {{0.0f, 0.0f}, {1.0f, 1.8f}, {2.0f, 3.4f}, {3.0f, 4.6f}, {4.0f, 5.4f}, {5.0f, 5.8f}});
		SetCurve(Config.RecoilProfile.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}, {1.0f, 0.3f}, {2.0f, 0.8f}, {5.0f, 1.2f}});
		SetCurve(Config.RecoilProfile.FiringErrorCurve, {{0.0f, 0.0f}, {1.0f, 0.35f}, {2.0f, 0.7f}, {3.0f, 1.0f}});
		SetRecoilRules(Config.RecoilProfile, 1, 0.25f, 0.3f, 2.0f, 0.5f, 2.0f);

		SetMovementError(Config, 0.5f, 1.2f, 3.0f, 7.0f);
		SetStanceRecoil(Config, 0.85f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::High);
		SetDamage(Config, {{30.0f, 159.5f, 55.0f, 46.75f}, {50.0f, 145.0f, 50.0f, 42.5f}});
		DisableADS(Config);

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 800, 반자동 4발/초, 탄창 6, 장전 2.25초, 장착 1초, 첫 발 0.25°, 이동 ×0.8, 관통 High, 피해 0~30m 159.5/55/46.75 · 30m~ 145/50/42.5"),
			TEXT("[공식] 위키: 최대 탄퍼짐 2.75°, 앉기 ×0.75, 이동 오차 0.5/1.2/3/7, 예비 24"),
			TEXT("[추정] 연타 반동 커브(두 번째 발 약 1.8°), 회복 0.5초, 탭 효율 2, 카메라 킥(2°)"),
		});
		return Preset;
	}

	// ---------------------------------------------------------------------------------------------
	// 기관단총(SMG) — 공통: 이동 속도 ×0.85, ADS 1.15배 줌·ADS 이동 ×0.76, 이동 오차 0.15/1/2.5/10, 관통 Low
	// ---------------------------------------------------------------------------------------------

	FValorWeaponPreset MakeStinger()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Stinger"), EValorWeaponCategory::SMG, 1100, 0.75f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, true, 20, 60, 2.25f);
		Config.AltFireType = EValorAltFireType::ADS;
		Config.ADSZoomMultiplier = 1.15f;

		SetFireMode(Config.HipFire, 16.0f, 0.65f, 1.5f, 0.85f, 0.85f);
		SetCameraKick(Config.HipFire, 0.4f, 0.12f, 0.02f, 2.5f);

		// [공식] ADS = 4점사: 점사 안 18발/초, 평균 8.471발/초(2.118점사/초 → 점사 사이 0.25초).
		SetFireMode(Config.AltFire, 8.470589f, 0.35f, 2.74f, 0.75f, 0.76f);
		Config.AltFire.BurstCount = 4;
		Config.AltFire.BurstFireRate = 18.0f;
		SetCameraKick(Config.AltFire, 0.2f, 0.06f, 0.02f, 1.5f);

		// [추정] 연사 반동: AimFinder 상대 상승량 × 밴달 실측(보호 탄 없음, 약 6.7°까지). 2.03·11.08 "3발째 이후 더 가파르게"와 같은 모양.
		// [공식] 최대 탄퍼짐 1.5°에 6번째 발(인덱스 5)에서 도달(11.08 "accrued faster from 7 bullets → 6 bullets").
		SetCurve(Config.RecoilProfile.VerticalRecoilCurve, {{0.0f, 0.0f}, {1.0f, 0.43f}, {2.0f, 1.05f}, {3.0f, 1.83f}, {4.0f, 2.68f}, {5.0f, 3.37f}, {6.0f, 4.05f}, {7.0f, 4.8f}, {8.0f, 5.4f}, {9.0f, 5.84f}, {10.0f, 6.17f}, {12.0f, 6.59f}, {16.0f, 6.68f}, {19.0f, 6.75f}});
		SetCurve(Config.RecoilProfile.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}, {1.0f, 0.38f}, {2.8f, 1.16f}, {4.5f, 1.83f}, {6.2f, 2.31f}, {9.8f, 2.58f}, {16.8f, 2.69f}});
		SetCurve(Config.RecoilProfile.FiringErrorCurve, {{0.0f, 0.0f}, {1.0f, 0.2f}, {2.0f, 0.4f}, {3.0f, 0.6f}, {4.0f, 0.8f}, {5.0f, 1.0f}});
		SetRecoilRules(Config.RecoilProfile, 0, 0.1f, 0.4f, 3.2f, 0.4f, 3.0f);

		// 점사(ADS)는 반동 규칙이 따로다. [공식] 점사 회복 0.4초, "첫 점사 이후 수직 반동이 더 가파르고 오차가 더 붙는다"(2.03).
		// [추정] 크기: 첫 점사 4발은 약 1.1°까지 완만, 두 번째 점사부터 가파르게.
		Config.bUseSeparateAltFireRecoil = true;
		FValorRecoilProfile& AltRecoil = Config.AltFireRecoilProfile;
		SetCurve(AltRecoil.VerticalRecoilCurve, {{0.0f, 0.0f}, {1.0f, 0.3f}, {2.0f, 0.7f}, {3.0f, 1.1f}, {4.0f, 1.9f}, {5.0f, 2.6f}, {6.0f, 3.2f}, {7.0f, 3.7f}, {11.0f, 5.0f}, {19.0f, 6.0f}});
		SetCurve(AltRecoil.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}, {3.0f, 0.0f}, {4.0f, 0.4f}, {8.0f, 1.3f}, {12.0f, 1.8f}, {19.0f, 2.0f}});
		SetCurve(AltRecoil.FiringErrorCurve, {{0.0f, 0.0f}, {3.0f, 0.1f}, {4.0f, 0.5f}, {7.0f, 0.7f}, {8.0f, 1.0f}});
		SetRecoilRules(AltRecoil, 4, 0.1f, 0.4f, 2.5f, 0.4f, 3.0f);

		SetMovementError(Config, 0.15f, 1.0f, 2.5f, 10.0f);
		SetStanceRecoil(Config, 0.85f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::Low);
		SetDamage(Config, {{15.0f, 67.5f, 27.0f, 22.95f}, {50.0f, 57.0f, 23.0f, 19.0f}});

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 1100, 자동 16발/초, 탄창 20, 장전 2.25초, 장착 0.75초, 첫 발 0.65°, 이동 ×0.85, 관통 Low, 피해 0~15m 67.5/27/22.95 · 15m~ 57/23/19"),
			TEXT("[공식] ADS: 1.15배, 4점사(점사 안 18발/초, 평균 8.471발/초), 첫 발 0.35° 최대 2.74°, 앉기 ×0.75, 이동 ×0.76 / 위키: 힙 최대 1.5°(11.08: 6발째 도달), 앉기 ×0.85, 이동 오차 0.15/1/2.5/10, 예비 60"),
			TEXT("[공식] 2.03: 점사 회복 0.45 → 0.4초, 첫 점사 이후 수직 반동·오차 증가"),
			TEXT("[추정] 연사·점사 반동 크기(AimFinder 상대값 × 밴달 실측), 보호 탄 0(AimFinder), 수평 전환 0.4초/10%, 힙 회복 0.4초, 탭 효율 3, 달리기 반동 ×1.5, 카메라 킥"),
			TEXT("[구현 예정] ADS 4점사(BurstCount/BurstFireRate) — 현재 ADS는 평균 속도 8.47발/초 단발 연사로 동작"),
		});
		return Preset;
	}

	FValorWeaponPreset MakeSpectre()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Spectre"), EValorWeaponCategory::SMG, 1600, 0.75f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, true, 30, 90, 2.25f);
		Config.AltFireType = EValorAltFireType::ADS;
		Config.ADSZoomMultiplier = 1.15f;
		Config.bSilenced = true;

		SetFireMode(Config.HipFire, 13.333f, 0.4f, 1.4f, 0.85f, 0.85f);
		SetCameraKick(Config.HipFire, 0.45f, 0.12f, 0.02f, 2.5f);

		SetFireMode(Config.AltFire, 11.9997f, 0.25f, 1.25f, 0.85f, 0.76f);
		SetCameraKick(Config.AltFire, 0.22f, 0.06f, 0.02f, 1.5f);

		// [추정] 크기: AimFinder 상대 상승량(밴달의 약 60%) × 밴달 실측 → 수직 최대 약 5.1°, 수평 ±2.05°.
		// [공식] 보호 탄 5발, 수평 전환 0.28초, 탭 효율 3(11.08). 탄퍼짐이 3·6·8번째 발(인덱스 2·5·7)에서 커진다(4.0) → 계단형.
		SetCurve(Config.RecoilProfile.VerticalRecoilCurve, {{0.0f, 0.0f}, {1.0f, 0.35f}, {2.0f, 0.86f}, {3.0f, 1.52f}, {4.0f, 2.29f}, {5.0f, 3.1f}, {6.0f, 3.59f}, {7.0f, 4.11f}, {8.0f, 4.48f}, {9.0f, 4.71f}, {10.0f, 4.86f}, {12.0f, 4.98f}, {16.0f, 5.05f}, {29.0f, 5.15f}});
		SetCurve(Config.RecoilProfile.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}, {4.0f, 0.0f}, {5.0f, 0.29f}, {7.2f, 0.88f}, {9.5f, 1.39f}, {11.8f, 1.76f}, {16.2f, 1.97f}, {25.2f, 2.05f}});
		SetCurve(Config.RecoilProfile.FiringErrorCurve, {{0.0f, 0.0f}, {2.0f, 0.333f}, {5.0f, 0.667f}, {7.0f, 1.0f}}, RCIM_Constant);
		SetRecoilRules(Config.RecoilProfile, 5, 0.1f, 0.28f, 2.6f, 0.35f, 3.0f);

		// [공식] 달리기 수직 반동 ×1.8(6.11), 달리기·점프 수평 반동 ×1.5(4.0 이후 변경 없음).
		SetMovementError(Config, 0.15f, 1.0f, 2.5f, 10.0f);
		SetStanceRecoil(Config, 0.85f, 1.8f, 1.5f);
		SetPenetration(Config, EValorWallPenetrationTier::Low);
		SetDamage(Config, {{15.0f, 78.0f, 26.0f, 22.1f}, {30.0f, 66.0f, 22.0f, 18.7f}, {50.0f, 60.0f, 20.0f, 17.0f}});

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 1600, 자동 13.333발/초(ADS 11.9997), 탄창 30, 장전 2.25초, 장착 0.75초, 첫 발 0.4°(ADS 0.25°), 이동 ×0.85(ADS ×0.76), 줌 1.15배, 관통 Low, 소음기"),
			TEXT("[공식] 피해 0~15m 78/26/22.1 · 15~30m 66/22/18.7 · 30m~ 60/20/17 / 위키: 최대 탄퍼짐 1.4°(ADS 1.25°, 11.08: 1.5 → 1.4), 앉기 ×0.85, 이동 오차 0.15/1/2.5/10, 예비 90"),
			TEXT("[공식] 패치: 보호 탄 5·수평 전환 0.28초·탭 효율 3(11.08), 탄퍼짐 증가 지점 3·6·8번째 발(4.0), 달리기 수직 ×1.8(6.11)·수평 ×1.5(4.0)"),
			TEXT("[추정] 수직/수평 반동 크기(AimFinder 상대값 × 밴달 실측: 수직 최대 약 5.1°, 수평 ±2.05°), 탄퍼짐 단계 크기(1/3씩), 수평 전환 확률 10%, 회복 0.35초, 카메라 킥, ADS 반동 ×0.9"),
			TEXT("[구현 예정] 소음기(적 시점 트레이서 숨김, 40m 밖 발사음 감쇠)"),
		});
		return Preset;
	}

	// ---------------------------------------------------------------------------------------------
	// 산탄총(Shotgun) — 공통: 이동 속도 ×0.75, 관통 Low, 이동 오차 0.5/1/2/4(12.09 산탄총 공통 변경)
	// ---------------------------------------------------------------------------------------------

	FValorWeaponPreset MakeBucky()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Bucky"), EValorWeaponCategory::Shotgun, 850, 1.0f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, false, 5, 10, 2.5f);
		Config.ReloadPerRoundSeconds = 0.5f;

		SetFireMode(Config.HipFire, 1.1f, 3.0f, 3.0f, 0.9f, 0.75f);
		Config.HipFire.PelletCount = 15;
		SetCameraKick(Config.HipFire, 2.2f, 0.3f, 0.04f, 5.0f);

		// [공식] 우클릭 = 7.5m에서 터지는 캐니스터(히트스캔, 0.49), 5펠릿, 펠릿 퍼짐 2.0°(2.06).
		Config.AltFireType = EValorAltFireType::AirBurst;
		Config.ADSZoomMultiplier = 1.0f;
		Config.AirBurstDistanceCm = 750.0f;
		SetFireMode(Config.AltFire, 1.1f, 2.0f, 2.0f, 0.9f, 0.75f);
		Config.AltFire.PelletCount = 5;
		Config.AltFire.RecoilMultiplier = 1.0f;
		Config.AltFire.CameraRecoilFollowRatio = 0.5f;
		SetCameraKick(Config.AltFire, 2.2f, 0.3f, 0.04f, 5.0f);

		SetNoSpray(Config.RecoilProfile, 0.5f);

		SetMovementError(Config, 0.5f, 1.0f, 2.0f, 4.0f);
		SetStanceRecoil(Config, 0.85f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::Low);
		SetDamage(Config, {{8.0f, 34.0f, 17.0f, 14.0f}, {12.0f, 26.0f, 13.0f, 11.05f}, {50.0f, 18.0f, 9.0f, 7.65f}});

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 850, 반자동(펌프) 1.1발/초, 탄창 5, 장전 2.5초(위키: 셸당 0.5초), 장착 1초, 펠릿 15, 첫 발 3°, 이동 ×0.75, 관통 Low"),
			TEXT("[공식] 피해(펠릿당) 0~8m 34/17/14 · 8~12m 26/13/11.05 · 12m~ 18/9/7.65 / 우클릭(airBurstStats): 5펠릿, 7.5m에서 폭발, 펠릿 퍼짐 2.0°(2.06)"),
			TEXT("[공식] 위키 12.09: 최소 탄퍼짐 2.6 → 3.0, 이동 오차 0.5/1/2/4 (위키 표의 '최대 2.6°·달리기 0.2'는 12.09 이전 값이라 쓰지 않음), 앉기 ×0.9, 예비 10"),
			TEXT("[추정] 스프레이 없음(펌프 액션), 카메라 킥"),
			TEXT("[구현 예정] 15펠릿 산탄, 우클릭 캐니스터(7.5m 전에 맞으면 펠릿 1알 피해), 셸 단위 장전"),
		});
		return Preset;
	}

	FValorWeaponPreset MakeJudge()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Judge"), EValorWeaponCategory::Shotgun, 1850, 1.0f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, true, 5, 15, 2.2f);

		SetFireMode(Config.HipFire, 3.5f, 2.5f, 4.0f, 0.75f, 0.75f);
		Config.HipFire.PelletCount = 12;
		SetCameraKick(Config.HipFire, 1.6f, 0.3f, 0.035f, 4.0f);

		// [추정] 자동 산탄총 연사 반동.
		SetCurve(Config.RecoilProfile.VerticalRecoilCurve, {{0.0f, 0.0f}, {1.0f, 1.2f}, {2.0f, 2.2f}, {3.0f, 3.0f}, {4.0f, 3.5f}});
		SetCurve(Config.RecoilProfile.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}, {1.0f, 0.3f}, {4.0f, 0.8f}});
		SetCurve(Config.RecoilProfile.FiringErrorCurve, {{0.0f, 0.0f}, {1.0f, 0.4f}, {2.0f, 0.75f}, {3.0f, 1.0f}});
		SetRecoilRules(Config.RecoilProfile, 1, 0.25f, 0.3f, 1.5f, 0.5f, 2.0f);

		SetMovementError(Config, 0.5f, 1.0f, 2.0f, 4.0f);
		SetStanceRecoil(Config, 0.85f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::Low);
		SetDamage(Config, {{10.0f, 34.0f, 17.0f, 14.45f}, {15.0f, 20.0f, 10.0f, 8.5f}, {50.0f, 14.0f, 7.0f, 5.95f}});
		DisableADS(Config);

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 1850, 자동 3.5발/초, 탄창 5, 장전 2.2초, 장착 1초, 펠릿 12, 첫 발 2.5°(PC, 12.09), 이동 ×0.75, 관통 Low"),
			TEXT("[공식] 피해(펠릿당) 0~10m 34/17/14.45 · 10~15m 20/10/8.5 · 15m~ 14/7/5.95 / 위키: 최대 탄퍼짐 4°, 앉기 ×0.75, 이동 오차 0.5/1/2/4(12.09), 예비 15"),
			TEXT("[추정] 연사 반동 커브, 회복 0.5초, 탭 효율 2, 카메라 킥"),
			TEXT("[구현 예정] 12펠릿 산탄(PelletCount) — 현재는 1발만 판정"),
		});
		return Preset;
	}

	// ---------------------------------------------------------------------------------------------
	// 소총(Rifle) — 공통: 이동 속도 ×0.8, ADS 이동 ×0.76, 이동 오차 0.8/3/6/10, 앉기 ×0.85
	// ---------------------------------------------------------------------------------------------

	FValorWeaponPreset MakeBulldog()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Bulldog"), EValorWeaponCategory::Rifle, 2050, 1.0f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, true, 24, 72, 2.5f);
		Config.AltFireType = EValorAltFireType::ADS;
		Config.ADSZoomMultiplier = 1.25f;

		SetFireMode(Config.HipFire, 10.0f, 0.3f, 1.25f, 0.85f, 0.8f);
		SetCameraKick(Config.HipFire, 0.7f, 0.15f, 0.025f, 3.0f);

		// [공식] ADS = 3점사: 점사 안 13.333발/초, 평균 6.316발/초(2.105점사/초 → 점사 사이 0.25초). 점사 회복 0.35초(4.0) = 힙과 같아 반동 규칙은 공유한다.
		SetFireMode(Config.AltFire, 6.315715f, 0.1f, 1.5f, 0.75f, 0.76f);
		Config.AltFire.BurstCount = 3;
		Config.AltFire.BurstFireRate = 13.333f;
		SetCameraKick(Config.AltFire, 0.35f, 0.08f, 0.025f, 2.0f);

		// [추정] 크기: AimFinder 상대 상승량(밴달의 약 88%) × 밴달 실측 → 수직 최대 약 7.9°, 수평 ±2.2°.
		SetCurve(Config.RecoilProfile.VerticalRecoilCurve, {{0.0f, 0.0f}, {1.0f, 0.52f}, {2.0f, 1.25f}, {3.0f, 2.21f}, {4.0f, 3.3f}, {5.0f, 4.43f}, {6.0f, 5.49f}, {7.0f, 6.29f}, {8.0f, 6.88f}, {9.0f, 7.24f}, {10.0f, 7.47f}, {12.0f, 7.67f}, {16.0f, 7.77f}, {23.0f, 7.89f}});
		SetCurve(Config.RecoilProfile.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}, {5.0f, 0.0f}, {6.0f, 0.31f}, {7.8f, 0.95f}, {9.5f, 1.5f}, {11.2f, 1.89f}, {14.8f, 2.11f}, {21.8f, 2.2f}});
		SetCurve(Config.RecoilProfile.FiringErrorCurve, {{0.0f, 0.0f}, {1.0f, 0.12f}, {2.0f, 0.27f}, {3.0f, 0.42f}, {4.0f, 0.57f}, {5.0f, 0.72f}, {6.0f, 0.84f}, {7.0f, 0.94f}, {8.0f, 1.0f}});
		SetRecoilRules(Config.RecoilProfile, 6, 0.1f, 0.6f, 3.0f, 0.35f, 4.0f);

		SetMovementError(Config, 0.8f, 3.0f, 6.0f, 10.0f);
		SetStanceRecoil(Config, 0.85f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::Medium);
		SetDamage(Config, {{50.0f, 115.5f, 35.0f, 29.75f}});

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 2050, 자동 10발/초, 탄창 24, 장전 2.5초, 장착 1초, 첫 발 0.3°, 이동 ×0.8, 관통 Medium, 피해 전 구간 115.5/35/29.75"),
			TEXT("[공식] ADS: 1.25배, 3점사(점사 안 13.333발/초, 평균 6.316발/초), 첫 발 0.1° 최대 1.5°, 앉기 ×0.75, 이동 ×0.76 / 위키: 힙 최대 1.25°, 앉기 ×0.85, 이동 오차 0.8/3/6/10, 예비 72"),
			TEXT("[공식] 패치: 회복 0.35초(0.50, 점사 4.0), 수평 전환 0.6초·확률 10%(11.08)"),
			TEXT("[추정] 반동 크기(AimFinder 상대값 × 밴달 실측), 보호 탄 6(AimFinder), 탭 효율 4, 달리기 반동 ×1.5, 카메라 킥"),
			TEXT("[구현 예정] ADS 3점사(BurstCount/BurstFireRate) — 현재 ADS는 평균 속도 6.32발/초 단발 연사로 동작"),
		});
		return Preset;
	}

	FValorWeaponPreset MakeGuardian()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Guardian"), EValorWeaponCategory::Rifle, 2250, 1.0f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, false, 12, 36, 2.5f);
		Config.AltFireType = EValorAltFireType::ADS;
		Config.ADSZoomMultiplier = 1.5f;

		SetFireMode(Config.HipFire, 5.25f, 0.1f, 1.58f, 0.85f, 0.8f);
		SetCameraKick(Config.HipFire, 1.1f, 0.2f, 0.03f, 3.5f);

		// [공식] ADS 발사 속도 감소 없음(4.0 "Removed firing rate penalty on ADS"), ADS 첫 발 0°.
		SetFireMode(Config.AltFire, 5.25f, 0.0f, 1.48f, 0.85f, 0.76f);
		SetCameraKick(Config.AltFire, 0.6f, 0.1f, 0.03f, 2.5f);

		// [공식] "Added an extra bullet before it enters a recovery curve"(4.0) → 두 번째 발까지 첫 발 정확도 유지(추정 해석).
		// [추정] 연타 반동 크기.
		SetCurve(Config.RecoilProfile.VerticalRecoilCurve, {{0.0f, 0.0f}, {1.0f, 0.8f}, {2.0f, 1.8f}, {3.0f, 2.9f}, {4.0f, 3.9f}, {6.0f, 5.4f}, {8.0f, 6.2f}, {11.0f, 6.6f}});
		SetCurve(Config.RecoilProfile.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}, {1.0f, 0.0f}, {2.0f, 0.3f}, {4.0f, 0.8f}, {8.0f, 1.3f}, {11.0f, 1.5f}});
		SetCurve(Config.RecoilProfile.FiringErrorCurve, {{0.0f, 0.0f}, {1.0f, 0.0f}, {2.0f, 0.25f}, {3.0f, 0.5f}, {4.0f, 0.75f}, {5.0f, 1.0f}});
		SetRecoilRules(Config.RecoilProfile, 1, 0.15f, 0.4f, 2.0f, 0.35f, 3.0f);

		SetMovementError(Config, 0.8f, 3.0f, 6.0f, 10.0f);
		SetStanceRecoil(Config, 0.85f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::High);
		SetDamage(Config, {{50.0f, 195.0f, 65.0f, 48.75f}});

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 2250, 반자동 5.25발/초(ADS 동일), 탄창 12, 장전 2.5초, 장착 1초, 첫 발 0.1°, 이동 ×0.8(ADS ×0.76), 줌 1.5배, 관통 High, 피해 전 구간 195/65/48.75"),
			TEXT("[공식] 위키: 최대 탄퍼짐 1.58°(ADS 첫 발 0° 최대 1.48°), 앉기 ×0.85, 이동 오차 0.8/3/6/10, 예비 36 / 패치: 회복 0.35초(0.50), '3발 뒤 회복 0.2925'(1.08), 회복 곡선 전 1발 추가(4.0)"),
			TEXT("[추정] 연타 반동 커브, 탄퍼짐 증가(2번째 발까지 유지), 수평 전환 0.4초/15%, 탭 효율 3, 달리기 반동 ×1.5, 카메라 킥"),
		});
		return Preset;
	}

	FValorWeaponPreset MakePhantom()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Phantom"), EValorWeaponCategory::Rifle, 2900, 1.0f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, true, 30, 60, 2.5f);
		Config.AltFireType = EValorAltFireType::ADS;
		Config.ADSZoomMultiplier = 1.25f;
		Config.bSilenced = true;

		SetFireMode(Config.HipFire, 11.0f, 0.2f, 0.9f, 0.85f, 0.8f);
		SetCameraKick(Config.HipFire, 0.65f, 0.12f, 0.025f, 3.0f);

		SetFireMode(Config.AltFire, 9.9f, 0.11f, 0.91f, 0.85f, 0.76f);
		SetCameraKick(Config.AltFire, 0.3f, 0.07f, 0.025f, 2.0f);

		// [추정] 크기: AimFinder 상대 상승량(밴달의 약 80~90%) × 밴달 실측 → 수직 최대 약 8.0°(8발째 7.1°), 수평 ±2.4°.
		// [공식] 보호 탄 8발 → 수평은 9번째 발(인덱스 8)부터 벌어진다. 수평 전환 0.6초·10%(11.08), 회복 0.35초·탭 효율 4(0.50).
		SetCurve(Config.RecoilProfile.VerticalRecoilCurve, {{0.0f, 0.0f}, {1.0f, 0.48f}, {2.0f, 1.16f}, {3.0f, 2.06f}, {4.0f, 3.1f}, {5.0f, 4.21f}, {6.0f, 5.27f}, {7.0f, 6.35f}, {8.0f, 7.13f}, {9.0f, 7.46f}, {10.0f, 7.67f}, {12.0f, 7.8f}, {16.0f, 7.91f}, {29.0f, 8.05f}});
		SetCurve(Config.RecoilProfile.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}, {7.0f, 0.0f}, {8.0f, 0.33f}, {10.0f, 1.02f}, {12.0f, 1.62f}, {14.0f, 2.05f}, {18.0f, 2.28f}, {26.0f, 2.38f}});
		SetCurve(Config.RecoilProfile.FiringErrorCurve, {{0.0f, 0.0f}, {1.0f, 0.1f}, {2.0f, 0.22f}, {3.0f, 0.35f}, {4.0f, 0.48f}, {5.0f, 0.6f}, {6.0f, 0.72f}, {7.0f, 0.83f}, {8.0f, 0.92f}, {9.0f, 1.0f}});
		SetRecoilRules(Config.RecoilProfile, 8, 0.1f, 0.6f, 3.0f, 0.35f, 4.0f);

		SetMovementError(Config, 0.8f, 3.0f, 6.0f, 10.0f);
		SetStanceRecoil(Config, 0.85f, 1.8f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::Medium);
		SetDamage(Config, {{20.0f, 156.0f, 39.0f, 33.15f}, {50.0f, 140.0f, 35.0f, 29.75f}});

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 2900, 자동 11발/초(ADS 9.9), 탄창 30, 장전 2.5초, 장착 1초, 첫 발 0.2°(ADS 0.11°), 이동 ×0.8(ADS ×0.76), 줌 1.25배, 관통 Medium, 소음기"),
			TEXT("[공식] 피해 0~20m 156/39/33.15 · 20m~ 140/35/29.75(9.10) / 위키: 최대 탄퍼짐 0.9°(ADS 0.91°), 앉기 ×0.85, 이동 오차 0.8/3/6/10, 예비 60(6.11)"),
			TEXT("[공식] 패치: 보호 탄 8·수평 전환 0.6초·확률 10%(11.08), 회복 0.35초·탭 효율 4(0.50), 달리기 수직 반동 ×1.8(6.11)"),
			TEXT("[추정] 수직/수평 반동 크기(AimFinder 상대값 × 밴달 실측: 수직 최대 약 8.0°, 수평 ±2.4°), 탄퍼짐 증가 곡선(9발째 최대), 카메라 킥, ADS 반동 ×0.9"),
			TEXT("[구현 예정] 소음기(적 시점 트레이서 숨김, 40m 밖 발사음 감쇠)"),
		});
		return Preset;
	}

	FValorWeaponPreset MakeVandal()
	{
		// FValorWeaponConfig 기본값 = 밴달(영상 실측으로 맞춘 반동 포함). 여기서는 식별 정보와 출처만 채운다.
		FValorWeaponPreset Preset = MakeBase(TEXT("Vandal"), EValorWeaponCategory::Rifle, 2900, 1.0f);
		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 2900, 자동 9.75발/초(ADS 8.775), 탄창 25, 장전 2.5초, 장착 1초, 첫 발 0.25°(ADS 0.1575°), 이동 ×0.8(ADS ×0.76), 줌 1.25배, 관통 Medium, 피해 전 구간 160/40/34"),
			TEXT("[공식] 위키: 최대 탄퍼짐 1.0°(ADS 1.02°), 앉기 ×0.85, 이동 오차 0.8/3/6/10, 예비 50(6.11) / 패치: 보호 탄 6·수평 전환 0.6초·확률 10%(11.08), 회복 0.375초·탭 효율 6(0.50), 달리기 수직 ×1.8(6.11)"),
			TEXT("[실측] 무보정 힙파이어 영상 2개 프레임 분석(2026-09-29): 수직 최대 약 8.95°, 수평 ±2.8°, 힙 카메라 추종 0.5, 카메라 킥 0.8°/0.025초 — Docs/VandalRecoil.md"),
			TEXT("[추정] 탄퍼짐 증가 곡선(8발째 최대), ADS 반동 ×0.9, 정지 Error Power 1.0"),
		});
		return Preset;
	}

	FValorWeaponPreset MakeWarden()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Warden"), EValorWeaponCategory::Rifle, 2900, 1.0f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, true, 18, 36, 2.5f);
		Config.AltFireType = EValorAltFireType::ADS;
		Config.ADSZoomMultiplier = 2.0f;

		// [공식] 첫 발 0.15°(ADS 0.04°). [추정] 최대 탄퍼짐은 공개 전이라 밴달 값(1.0° / ADS 1.02°)을 임시로 쓴다.
		SetFireMode(Config.HipFire, 6.5f, 0.15f, 1.0f, 0.85f, 0.8f);
		SetCameraKick(Config.HipFire, 1.0f, 0.18f, 0.025f, 3.5f);

		SetFireMode(Config.AltFire, 6.5f, 0.04f, 1.02f, 0.85f, 0.76f);
		SetCameraKick(Config.AltFire, 0.5f, 0.1f, 0.025f, 2.5f);

		// [추정] 반동 규칙·커브는 공개 자료가 없어(2026-09-22 출시) 밴달 값을 임시로 그대로 둔다(RecoilProfile 기본값 = 밴달).

		SetMovementError(Config, 0.8f, 3.0f, 6.0f, 10.0f);
		SetStanceRecoil(Config, 0.85f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::Medium);
		SetDamage(Config, {{50.0f, 200.0f, 50.0f, 42.0f}});

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] 13.06 패치노트(2026-09-22): 자동 소총, 가격 2900, 발사 속도 6.5, 탄창 18, 예비 36, 장전 2.5초, 장착 1초, 2배 조준경, 관통 Medium, 피해 전 구간 200/50/42"),
			TEXT("[공식] valorant-api(2026-09): 첫 발 0.15°(ADS 0.04°), 이동 ×0.8(ADS ×0.76), ADS 발사 속도 6.5 (일부 기사는 ADS 92%라고 함 — 확인 필요)"),
			TEXT("[추정] 반동 커브·보호 탄·회복·탭 효율 = 밴달 값 임시 사용, 최대 탄퍼짐 = 밴달 값, 이동 오차 = 소총 공통 값, 달리기 반동 ×1.5, 카메라 킥"),
		});
		return Preset;
	}

	// ---------------------------------------------------------------------------------------------
	// 저격총(Sniper) — 공통: 조준 시 탄퍼짐 0°, 이동 오차 7.5/10/15/20, 앉기 ×0.9, 스프레이 없음
	// ---------------------------------------------------------------------------------------------

	FValorWeaponPreset MakeMarshal()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Marshal"), EValorWeaponCategory::Sniper, 950, 1.25f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, false, 5, 15, 2.5f);
		Config.ReloadPerRoundSeconds = 0.5f;
		Config.AltFireType = EValorAltFireType::ADS;
		Config.ADSZoomMultiplier = 3.5f;

		SetFireMode(Config.HipFire, 1.5f, 1.0f, 1.0f, 0.9f, 0.8f);
		SetCameraKick(Config.HipFire, 1.5f, 0.25f, 0.04f, 5.0f);

		// [공식] 줌 시 발사 속도 80%(1.2), 줌 이동 속도 90%(2.03).
		SetFireMode(Config.AltFire, 1.2f, 0.0f, 0.0f, 0.9f, 0.9f);
		SetCameraKick(Config.AltFire, 1.2f, 0.2f, 0.04f, 5.0f);

		SetNoSpray(Config.RecoilProfile, 0.5f);

		SetMovementError(Config, 7.5f, 10.0f, 15.0f, 20.0f);
		SetStanceRecoil(Config, 0.85f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::Medium);
		SetDamage(Config, {{50.0f, 202.0f, 101.0f, 85.85f}});

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 950, 반자동 1.5발/초(줌 1.2), 탄창 5, 장전 2.5초(위키: 발당 0.5초), 장착 1.25초, 비조준 탄퍼짐 1°, 이동 ×0.8(줌 ×0.9), 줌 3.5배, 관통 Medium, 피해 202/101/85.85"),
			TEXT("[공식] 위키: 줌 탄퍼짐 0°, 앉기 ×0.9, 이동 오차 7.5/10/15/20, 예비 15"),
			TEXT("[추정] 스프레이 없음, 카메라 킥 / [구현 예정] 발 단위 장전, 발사 후 자동 재조준"),
		});
		return Preset;
	}

	FValorWeaponPreset MakeOutlaw()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Outlaw"), EValorWeaponCategory::Sniper, 2400, 1.25f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, false, 2, 10, 3.8f);
		Config.AltFireType = EValorAltFireType::ADS;
		Config.ADSZoomMultiplier = 3.5f;

		SetFireMode(Config.HipFire, 2.75f, 3.5f, 3.5f, 0.9f, 0.8f);
		SetCameraKick(Config.HipFire, 2.0f, 0.3f, 0.04f, 5.0f);

		SetFireMode(Config.AltFire, 2.75f, 0.0f, 0.0f, 0.9f, 0.8f);
		SetCameraKick(Config.AltFire, 1.6f, 0.25f, 0.04f, 5.0f);

		SetNoSpray(Config.RecoilProfile, 0.3f);

		SetMovementError(Config, 7.5f, 10.0f, 15.0f, 20.0f);
		SetStanceRecoil(Config, 0.85f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::High);
		SetDamage(Config, {{50.0f, 238.0f, 140.0f, 119.0f}});

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 2400, 반자동 2.75발/초(줌 동일), 탄창 2(쌍열), 장전 3.8초(위키: 1발만 넣으면 2.3초), 장착 1.25초, 비조준 탄퍼짐 3.5°, 이동 ×0.8(줌 ×0.8), 줌 3.5배, 관통 High"),
			TEXT("[공식] 피해 238/140/119 / 위키: 줌 탄퍼짐 0°, 앉기 ×0.9, 이동 오차 7.5/10/15/20, 예비 10"),
			TEXT("[추정] 스프레이 없음, 카메라 킥 / [구현 예정] 1발 장전 2.3초(부분 장전)"),
		});
		return Preset;
	}

	FValorWeaponPreset MakeOperator()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Operator"), EValorWeaponCategory::Sniper, 4700, 1.5f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, false, 5, 10, 3.7f);
		Config.AltFireType = EValorAltFireType::ADS;
		Config.ADSZoomMultiplier = 2.5f;
		Config.SecondaryADSZoomMultiplier = 5.0f;

		SetFireMode(Config.HipFire, 0.6f, 5.0f, 5.0f, 0.9f, 0.76f);
		SetCameraKick(Config.HipFire, 2.5f, 0.35f, 0.05f, 6.0f);

		SetFireMode(Config.AltFire, 0.6f, 0.0f, 0.0f, 0.9f, 0.72f);
		SetCameraKick(Config.AltFire, 2.0f, 0.3f, 0.05f, 6.0f);

		SetNoSpray(Config.RecoilProfile, 1.0f);

		// [공식] 1.09: 부정확해지기 시작하는 속도 30% → 15%(더 빨리 부정확해지고, 멈출 때 더 늦게 정확해진다).
		SetMovementError(Config, 7.5f, 10.0f, 15.0f, 15.0f);
		Config.MovementAccuracy.DeadzoneSpeedRatio = 0.15f;
		SetStanceRecoil(Config, 0.85f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::High);
		SetDamage(Config, {{50.0f, 255.0f, 150.0f, 120.0f}});

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 4700, 반자동 0.6발/초, 탄창 5, 장전 3.7초, 장착 1.5초, 비조준 탄퍼짐 5°, 이동 ×0.76(줌 ×0.72), 2단 줌 2.5배/5배, 관통 High, 피해 255/150/120"),
			TEXT("[공식] 위키: 줌 탄퍼짐 0°, 앉기 ×0.9, 이동 오차 7.5/10/15/15, 예비 10 / 1.09: 이동 데드존 30% → 15%"),
			TEXT("[추정] 스프레이 없음, 카메라 킥 / [구현 예정] 2단 줌(SecondaryADSZoomMultiplier)"),
		});
		return Preset;
	}

	// ---------------------------------------------------------------------------------------------
	// 기관총(Heavy) — 공통: 이동 속도 ×0.76, ADS 1.15배, 이동 오차 0.4/3/6.5/10, 앉기 ×0.6(탄퍼짐·반동 40% 감소)
	// ---------------------------------------------------------------------------------------------

	FValorWeaponPreset MakeAres()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Ares"), EValorWeaponCategory::Heavy, 1600, 1.25f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, true, 50, 100, 3.25f);
		Config.AltFireType = EValorAltFireType::ADS;
		Config.ADSZoomMultiplier = 1.15f;

		// [공식] 쏠수록 정확해지는 총: 첫 발 1.0° → 13발 뒤 0.7°(4.01). MaxFiringError에 "최소 탄퍼짐"을 넣으면 같은 보간으로 줄어든다.
		SetFireMode(Config.HipFire, 13.0f, 1.0f, 0.7f, 0.6f, 0.76f);
		SetCameraKick(Config.HipFire, 0.45f, 0.12f, 0.02f, 2.5f);

		SetFireMode(Config.AltFire, 13.0f, 0.9f, 0.55f, 0.6f, 0.76f);
		SetCameraKick(Config.AltFire, 0.25f, 0.07f, 0.02f, 1.5f);

		// [추정] 크기: AimFinder 상대 상승량(밴달의 약 47~55%) × 밴달 실측 → 수직 최대 약 4.3°, 수평 ±2.5°.
		SetCurve(Config.RecoilProfile.VerticalRecoilCurve, {{0.0f, 0.0f}, {1.0f, 0.31f}, {2.0f, 0.76f}, {3.0f, 1.37f}, {4.0f, 2.09f}, {5.0f, 2.53f}, {6.0f, 2.93f}, {7.0f, 3.36f}, {8.0f, 3.67f}, {9.0f, 3.87f}, {10.0f, 4.0f}, {12.0f, 4.11f}, {16.0f, 4.17f}, {24.0f, 4.23f}, {49.0f, 4.3f}});
		SetCurve(Config.RecoilProfile.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}, {3.0f, 0.0f}, {4.0f, 0.35f}, {7.0f, 1.08f}, {10.0f, 1.71f}, {13.0f, 2.17f}, {19.0f, 2.42f}, {31.0f, 2.52f}});
		SetCurve(Config.RecoilProfile.FiringErrorCurve, {{0.0f, 0.0f}, {13.0f, 1.0f}});
		SetRecoilRules(Config.RecoilProfile, 4, 0.1f, 0.6f, 3.0f, 0.4f, 3.0f);

		SetMovementError(Config, 0.4f, 3.0f, 6.5f, 10.0f);
		SetStanceRecoil(Config, 0.6f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::High);
		SetDamage(Config, {{30.0f, 75.0f, 30.0f, 25.5f}, {50.0f, 70.0f, 28.0f, 23.8f}});

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 1600, 자동 13발/초(ADS 동일), 탄창 50, 장전 3.25초, 장착 1.25초, 첫 발 1.0°(ADS 0.9°), 이동 ×0.76(ADS ×0.76), 줌 1.15배, 관통 High"),
			TEXT("[공식] 피해 0~30m 75/30/25.5 · 30m~ 70/28/23.8 / 위키: 최소 탄퍼짐 0.7°(ADS 0.55°, 4.01: 13발 뒤 도달), 앉기 ×0.6(9.05: 40%로 복귀), 이동 오차 0.4/3/6.5/10, 예비 100"),
			TEXT("[추정] 반동 크기(AimFinder 상대값 × 밴달 실측), 보호 탄 4(AimFinder), 수평 전환 0.6초/10%, 회복 0.4초, 탭 효율 3, 달리기 반동 ×1.5, 카메라 킥"),
		});
		return Preset;
	}

	FValorWeaponPreset MakeOdin()
	{
		FValorWeaponPreset Preset = MakeBase(TEXT("Odin"), EValorWeaponCategory::Heavy, 3200, 1.25f);
		FValorWeaponConfig& Config = Preset.Config;
		SetAmmo(Config, true, 100, 200, 5.0f);
		Config.AltFireType = EValorAltFireType::ADS;
		Config.ADSZoomMultiplier = 1.15f;

		// [공식] 힙파이어는 12발/초에서 시작해 계속 쏘면 15.6발/초까지 오른다. [추정] 최고 속도까지 1초.
		SetFireMode(Config.HipFire, 12.0f, 0.8f, 1.3f, 0.6f, 0.76f);
		Config.HipFire.SpinUpMaxFireRate = 15.6f;
		Config.HipFire.SpinUpTimeSeconds = 1.0f;
		SetCameraKick(Config.HipFire, 0.4f, 0.12f, 0.02f, 2.5f);

		// [공식] ADS는 처음부터 최고 속도 15.6발/초("Starts firing at maximum fire rate").
		SetFireMode(Config.AltFire, 15.6f, 0.79f, 1.36f, 0.6f, 0.76f);
		SetCameraKick(Config.AltFire, 0.22f, 0.06f, 0.02f, 1.5f);

		// [추정] 크기: AimFinder 상대 상승량(밴달의 약 40%) × 밴달 실측 → 수직 최대 약 3.7°, 수평 ±3.5°(긴 탄창 동안 넓게).
		SetCurve(Config.RecoilProfile.VerticalRecoilCurve, {{0.0f, 0.0f}, {1.0f, 0.27f}, {2.0f, 0.66f}, {3.0f, 1.18f}, {4.0f, 1.78f}, {5.0f, 2.15f}, {6.0f, 2.49f}, {7.0f, 2.85f}, {8.0f, 3.12f}, {9.0f, 3.28f}, {10.0f, 3.39f}, {12.0f, 3.48f}, {16.0f, 3.53f}, {24.0f, 3.58f}, {99.0f, 3.7f}});
		SetCurve(Config.RecoilProfile.HorizontalRecoilAmplitudeCurve, {{0.0f, 0.0f}, {3.0f, 0.0f}, {4.0f, 0.49f}, {9.0f, 1.5f}, {14.0f, 2.38f}, {19.0f, 3.01f}, {29.0f, 3.36f}, {49.0f, 3.5f}});
		SetCurve(Config.RecoilProfile.FiringErrorCurve, {{0.0f, 0.0f}, {2.0f, 0.2f}, {4.0f, 0.45f}, {6.0f, 0.65f}, {8.0f, 0.82f}, {10.0f, 0.93f}, {12.0f, 1.0f}});
		SetRecoilRules(Config.RecoilProfile, 4, 0.1f, 0.6f, 4.0f, 0.4f, 3.0f);

		SetMovementError(Config, 0.4f, 3.0f, 6.5f, 10.0f);
		SetStanceRecoil(Config, 0.6f, 1.5f, 1.0f);
		SetPenetration(Config, EValorWallPenetrationTier::High);
		SetDamage(Config, {{30.0f, 95.0f, 38.0f, 32.3f}, {50.0f, 77.5f, 31.0f, 26.35f}});

		Preset.SourceNotes = JoinNotes({
			TEXT("[공식] valorant-api(2026-09): 가격 3200, 자동 12발/초 → 15.6(ROFIncrease, ADS는 처음부터 15.6), 탄창 100, 장전 5초, 장착 1.25초, 첫 발 0.8°(ADS 0.79°), 이동 ×0.76, 줌 1.15배, 관통 High"),
			TEXT("[공식] 피해 0~30m 95/38/32.3 · 30m~ 77.5/31/26.35 / 위키: 최대 탄퍼짐 1.3°(ADS 1.36°), 앉기 ×0.6, 이동 오차 0.4/3/6.5/10, 예비 200 / 0.50: 8발 이후 수평 반동 감소"),
			TEXT("[추정] 가속 시간 1초, 반동 크기(AimFinder 상대값 × 밴달 실측), 보호 탄 4(AimFinder), 수평 전환 0.6초/10%, 회복 0.4초, 탭 효율 3, 달리기 반동 ×1.5, 카메라 킥"),
			TEXT("[구현 예정] 힙파이어 발사 속도 가속(SpinUpMaxFireRate/SpinUpTimeSeconds) — 현재는 12발/초 고정"),
		});
		return Preset;
	}
}

TArray<FValorWeaponPreset> BuildAll()
{
	TArray<FValorWeaponPreset> Presets;
	Presets.Reserve(20);

	Presets.Add(MakeClassic());
	Presets.Add(MakeShorty());
	Presets.Add(MakeFrenzy());
	Presets.Add(MakeGhost());
	Presets.Add(MakeBandit());
	Presets.Add(MakeSheriff());

	Presets.Add(MakeStinger());
	Presets.Add(MakeSpectre());

	Presets.Add(MakeBucky());
	Presets.Add(MakeJudge());

	Presets.Add(MakeBulldog());
	Presets.Add(MakeGuardian());
	Presets.Add(MakePhantom());
	Presets.Add(MakeVandal());
	Presets.Add(MakeWarden());

	Presets.Add(MakeMarshal());
	Presets.Add(MakeOutlaw());
	Presets.Add(MakeOperator());

	Presets.Add(MakeAres());
	Presets.Add(MakeOdin());
	return Presets;
}
}

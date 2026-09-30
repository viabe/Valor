#include "Weapons/ValorSpraySimulation.h"

#include "Weapons/Data/ValorWeaponDataAsset.h"

namespace ValorSpray
{
namespace Private
{
	float EvaluateCurve(const FRuntimeFloatCurve& Curve, float X, float DefaultValue)
	{
		const FRichCurve* RichCurve = Curve.GetRichCurveConst();
		if (!RichCurve || RichCurve->GetNumKeys() == 0)
		{
			return DefaultValue;
		}

		return RichCurve->Eval(X, DefaultValue);
	}

	const FValorFireModeStats& GetFireMode(const FValorWeaponConfig& Config, bool bIsADS)
	{
		return bIsADS ? Config.AltFire : Config.HipFire;
	}

	// 우클릭이 별도 반동 규칙을 쓰는 무기(클래식 우클릭, 스팅어 점사)만 ADS에서 AltFireRecoilProfile을 쓴다.
	// 그 외(밴달·팬텀 등)는 힙과 같은 패턴에 AltFire.RecoilMultiplier만 곱한다.
	const FValorRecoilProfile& GetRecoilProfile(const FValorWeaponConfig& Config, bool bIsADS)
	{
		return bIsADS && Config.bUseSeparateAltFireRecoil ? Config.AltFireRecoilProfile : Config.RecoilProfile;
	}

	struct FRecoveryInfo
	{
		// 0 = 아직 연사 중(회복 없음), 1 = 완전 회복(다음 발은 첫 발 취급).
		float Alpha = 1.0f;

		// 연사 여유 시간(발사 간격 × RecoveryGraceMultiplier). 이 시간까지는 "연사가 이어지는 중"으로 본다.
		float Grace = 0.0f;

		// 마지막 발 이후 경과 시간.
		float Elapsed = 0.0f;
	};

	// 마지막 발 이후 회복 상태. 연사 여유까지는 Alpha=0으로 유지해 풀오토가 중간에 회복되는 일이 없게 한다.
	FRecoveryInfo ComputeRecovery(const FValorRecoilProfile& Profile, const FValorSprayState& State, float FireInterval, double Now)
	{
		FRecoveryInfo Info;
		Info.Grace = FireInterval * FMath::Max(Profile.RecoveryGraceMultiplier, 1.0f);
		if (!State.bHasFired)
		{
			return Info;
		}

		const float RecoveryTime = FMath::Max(Profile.GunRecoveryTime, Info.Grace + 0.01f);
		Info.Elapsed = FMath::Max(0.0f, static_cast<float>(Now - State.LastShotTime));
		Info.Alpha = FMath::Clamp((Info.Elapsed - Info.Grace) / (RecoveryTime - Info.Grace), 0.0f, 1.0f);
		return Info;
	}

	// 수평 반동 이동: 정해진 쪽(YawSide)의 진폭을 목표로, "한쪽 끝 → 반대쪽 끝"을 정확히 YawSwitchTime초에 가는 속도로 움직인다.
	// 11.08 패치노트 정의 그대로다 — "Yaw switch time: how quickly the gun swaps horizontal recoil from one side to the other".
	// 영상에서도 카메라 좌우가 튀지 않고 약 0.4~0.6초에 걸쳐 반대편으로 부드럽게 넘어갔다.
	float StepYawTowardSide(const FValorRecoilProfile& Profile, float CurrentYaw, int32 YawSide, float SprayIndex, float MoveSeconds)
	{
		if (YawSide == 0 || MoveSeconds <= 0.0f)
		{
			return CurrentYaw;
		}

		const float Amplitude = FMath::Clamp(EvaluateCurve(Profile.HorizontalRecoilAmplitudeCurve, SprayIndex, 0.0f), 0.0f, Profile.MaxHorizontalRecoil);
		const float Target = static_cast<float>(YawSide) * Amplitude;
		const float Speed = 2.0f * FMath::Max(Amplitude, 0.1f) / FMath::Max(Profile.YawSwitchTime, 0.01f);
		const float MaxStep = Speed * MoveSeconds;
		return CurrentYaw + FMath::Clamp(Target - CurrentYaw, -MaxStep, MaxStep);
	}

	// 단조 증가 커브의 역함수: Curve(x) >= TargetY 를 만족하는 가장 작은 x(0 ≤ x ≤ MaxX). 이분 탐색이라 보간 방식과 무관하다.
	float InverseMonotonicCurve(const FRuntimeFloatCurve& Curve, float TargetY, float MaxX)
	{
		float Low = 0.0f;
		float High = FMath::Max(MaxX, 0.0f);
		if (TargetY <= EvaluateCurve(Curve, 0.0f, 0.0f))
		{
			return 0.0f;
		}

		if (TargetY >= EvaluateCurve(Curve, High, 0.0f))
		{
			return High;
		}

		for (int32 Iteration = 0; Iteration < 24; ++Iteration)
		{
			const float Mid = (Low + High) * 0.5f;
			if (EvaluateCurve(Curve, Mid, 0.0f) < TargetY)
			{
				Low = Mid;
			}
			else
			{
				High = Mid;
			}
		}

		return High;
	}

	// 회복을 반영한 "다음 발"의 스프레이 진행도와 수평 반동(배율 적용 전).
	//   - 풀오토(Alpha=0): LastIndex + 1 → 패턴을 한 발씩 그대로 진행.
	//   - 회복 중 재사격: 직전 발이 남긴 증분이 1 → 1/TapEfficiency로 줄고(탭/버스트가 유리한 이유),
	//     반동 "각도"가 (1-Alpha)만큼 줄어든 지점에 해당하는 진행도에서 이어간다.
	//   - 완전 회복(Alpha=1): 0 → 첫 발 정확도.
	// 왜 진행도가 아니라 각도를 줄이나: 실제 게임 영상에서 사격 후 카메라가 처음부터 일정한 속도로 내려왔다.
	//   진행도를 선형으로 줄이면 패턴 상단(수직이 거의 안 늘어나는 구간)에서는 한동안 각도가 그대로라 카메라가 머물다가 급락한다.
	// 수평은 연사가 이어지는 동안(연사 여유 시간까지)만 목표 쪽으로 움직이고, 그 뒤로는 같은 비율로 회복된다.
	// 시간에 대해 연속이므로 같은 식을 매 프레임 평가하면 카메라가 발 사이에도 부드럽게 움직이고 원위치로 돌아온다.
	void ProjectSpray(const FValorRecoilProfile& Profile, const FValorSprayState& State, const FRecoveryInfo& Recovery, float& OutSprayIndex, float& OutYaw)
	{
		if (!State.bHasFired || Recovery.Alpha >= 1.0f)
		{
			OutSprayIndex = 0.0f;
			OutYaw = 0.0f;
			return;
		}

		const float Accrual = FMath::Lerp(1.0f, 1.0f / FMath::Max(Profile.TapEfficiency, 1.0f), Recovery.Alpha);
		const float Keep = 1.0f - Recovery.Alpha;
		const float NextIndexBeforeRecovery = State.LastShotSprayIndex + Accrual;
		const float PitchBeforeRecovery = EvaluateCurve(Profile.VerticalRecoilCurve, NextIndexBeforeRecovery, 0.0f);
		if (Recovery.Alpha <= 0.0f)
		{
			OutSprayIndex = NextIndexBeforeRecovery;
		}
		else if (PitchBeforeRecovery > KINDA_SMALL_NUMBER)
		{
			OutSprayIndex = InverseMonotonicCurve(Profile.VerticalRecoilCurve, PitchBeforeRecovery * Keep, NextIndexBeforeRecovery);
		}
		else
		{
			// 수직 반동이 없는 무기(커브가 0)는 각도로 역산할 수 없으므로 진행도 자체를 줄인다.
			OutSprayIndex = NextIndexBeforeRecovery * Keep;
		}

		const float MovedYaw = StepYawTowardSide(Profile, State.LastShotYaw, State.YawSide, NextIndexBeforeRecovery, FMath::Min(Recovery.Elapsed, Recovery.Grace));
		OutYaw = FMath::Clamp(MovedYaw * Keep, -Profile.MaxHorizontalRecoil, Profile.MaxHorizontalRecoil);
	}

	struct FStanceModifiers
	{
		float PitchMultiplier = 1.0f;
		float YawMultiplier = 1.0f;
		float ErrorMultiplier = 1.0f;
		float MovementError = 0.0f;
		float ErrorPower = 1.0f;
	};

	// 자세 → 반동/탄퍼짐 배율. 발로란트 규칙:
	//  - 이동 오차는 "더해진다"(additive). 속도가 데드존(최고 속도의 27.5%, 오퍼레이터 15%)을 넘으면 걷기/달리기 오차까지 선형으로 오른다.
	//  - 앉아서 멈춰 있으면 탄퍼짐 × 발사 모드별 배율(밴달 0.85), 반동 × 무기별 배율(밴달 0.85).
	//  - 달리며 쏘면 수직 반동 최대 ×1.8(6.11, 밴달·팬텀·스펙터), 수평은 무기별(스펙터 ×1.5).
	//  - ADS 반동 배율은 앉기 배율과 곱해진다("multiplicative with crouch").
	FStanceModifiers ComputeStanceModifiers(const FValorWeaponConfig& Config, const FValorShooterStance& Stance)
	{
		const FValorMovementAccuracyProfile& Accuracy = Config.MovementAccuracy;
		const FValorFireModeStats& FireMode = GetFireMode(Config, Stance.bIsADS);

		// 이동 오차 값: 보통은 무기 공통 값, 모드별 공식 수치가 따로 있는 경우(클래식 우클릭)만 모드 값.
		FValorMovementErrorValues Errors;
		if (FireMode.bOverrideMovementError)
		{
			Errors = FireMode.MovementErrorOverride;
		}
		else
		{
			Errors.CrouchMovingError = Accuracy.CrouchMovingError;
			Errors.WalkingError = Accuracy.WalkingError;
			Errors.RunningError = Accuracy.RunningError;
			Errors.AirborneError = Accuracy.AirborneError;
		}

		FStanceModifiers Modifiers;
		float MovingAlpha = 0.0f;
		float RunAlpha = 0.0f;

		if (Stance.bIsAirborne)
		{
			Modifiers.MovementError = Errors.AirborneError;
			MovingAlpha = 1.0f;
			RunAlpha = 1.0f;
		}
		else
		{
			const float Speed = Stance.HorizontalSpeed;
			const float DeadzoneSpeed = Accuracy.DeadzoneSpeedRatio * Stance.RunSpeed;
			if (Speed > DeadzoneSpeed)
			{
				if (Stance.bIsCrouched)
				{
					MovingAlpha = FMath::Clamp((Speed - DeadzoneSpeed) / FMath::Max(Stance.CrouchSpeed - DeadzoneSpeed, 1.0f), 0.0f, 1.0f);
					Modifiers.MovementError = Errors.CrouchMovingError * MovingAlpha;
				}
				else if (Speed <= Stance.WalkSpeed)
				{
					MovingAlpha = FMath::Clamp((Speed - DeadzoneSpeed) / FMath::Max(Stance.WalkSpeed - DeadzoneSpeed, 1.0f), 0.0f, 1.0f);
					Modifiers.MovementError = Errors.WalkingError * MovingAlpha;
				}
				else
				{
					MovingAlpha = 1.0f;
					RunAlpha = FMath::Clamp((Speed - Stance.WalkSpeed) / FMath::Max(Stance.RunSpeed - Stance.WalkSpeed, 1.0f), 0.0f, 1.0f);
					Modifiers.MovementError = FMath::Lerp(Errors.WalkingError, Errors.RunningError, RunAlpha);
				}
			}

			// 착지 직후 일정 시간 동안은 고정 오차를 더한다(1.09: 점진 감소가 아니라 이진(on/off) 방식).
			if (Stance.TimeSinceLanded < Accuracy.JumpLandErrorDuration)
			{
				Modifiers.MovementError += Accuracy.JumpLandError;
				MovingAlpha = 1.0f;
			}
		}

		const bool bCrouchedAndStationary = Stance.bIsCrouched && !Stance.bIsAirborne && MovingAlpha <= 0.0f;
		const float CrouchRecoil = bCrouchedAndStationary ? Accuracy.CrouchRecoilMultiplier : 1.0f;

		Modifiers.PitchMultiplier = FireMode.RecoilMultiplier * CrouchRecoil * FMath::Lerp(1.0f, Accuracy.RunningVerticalRecoilMultiplier, RunAlpha);
		Modifiers.YawMultiplier = FireMode.RecoilMultiplier * CrouchRecoil * FMath::Lerp(1.0f, Accuracy.RunningHorizontalRecoilMultiplier, RunAlpha);
		Modifiers.ErrorMultiplier = bCrouchedAndStationary ? FireMode.CrouchErrorMultiplier : 1.0f;
		Modifiers.ErrorPower = FMath::Lerp(Accuracy.StandingErrorPower, Accuracy.MovingErrorPower, MovingAlpha);
		return Modifiers;
	}

	float EvaluateSprayError(const FValorWeaponConfig& Config, bool bIsADS, float SprayIndex)
	{
		const FValorFireModeStats& FireMode = GetFireMode(Config, bIsADS);
		const float ErrorAlpha = FMath::Clamp(EvaluateCurve(GetRecoilProfile(Config, bIsADS).FiringErrorCurve, SprayIndex, 0.0f), 0.0f, 1.0f);
		return FMath::Lerp(FireMode.FirstShotError, FireMode.MaxFiringError, ErrorAlpha);
	}
}

float GetFireInterval(const FValorWeaponConfig& Config, bool bIsADS)
{
	return 1.0f / FMath::Max(Private::GetFireMode(Config, bIsADS).FireRate, 0.1f);
}

bool IsNewSpray(const FValorWeaponConfig& Config, bool bIsADS, const FValorSprayState& State, double ShotTime)
{
	return Private::ComputeRecovery(Private::GetRecoilProfile(Config, bIsADS), State, GetFireInterval(Config, bIsADS), ShotTime).Alpha >= 1.0f;
}

FValorSprayEvaluation Evaluate(const FValorWeaponConfig& Config, const FValorShooterStance& Stance, const FValorSprayState& State, double Now)
{
	const FValorRecoilProfile& Profile = Private::GetRecoilProfile(Config, Stance.bIsADS);
	const Private::FRecoveryInfo Recovery = Private::ComputeRecovery(Profile, State, GetFireInterval(Config, Stance.bIsADS), Now);

	float SprayIndex = 0.0f;
	float RawYaw = 0.0f;
	Private::ProjectSpray(Profile, State, Recovery, SprayIndex, RawYaw);

	const Private::FStanceModifiers Modifiers = Private::ComputeStanceModifiers(Config, Stance);
	const FValorFireModeStats& FireMode = Private::GetFireMode(Config, Stance.bIsADS);

	FValorSprayEvaluation Result;
	Result.SprayIndex = SprayIndex;
	Result.RecoilPitchDegrees = Private::EvaluateCurve(Profile.VerticalRecoilCurve, SprayIndex, 0.0f) * Modifiers.PitchMultiplier;
	Result.RecoilYawDegrees = RawYaw * Modifiers.YawMultiplier;
	Result.SprayErrorDegrees = Private::EvaluateSprayError(Config, Stance.bIsADS, SprayIndex) * Modifiers.ErrorMultiplier;
	Result.RestErrorDegrees = FireMode.FirstShotError * Modifiers.ErrorMultiplier;
	Result.MovementErrorDegrees = Modifiers.MovementError;
	Result.FiringErrorDegrees = Result.SprayErrorDegrees + Result.MovementErrorDegrees;
	return Result;
}

FValorComputedShotData AdvanceShot(const FValorWeaponConfig& Config, const FValorShooterStance& Stance, FValorSprayState& State, double ShotTime, int32 ShotSeed)
{
	const FValorRecoilProfile& Profile = Private::GetRecoilProfile(Config, Stance.bIsADS);
	const Private::FRecoveryInfo Recovery = Private::ComputeRecovery(Profile, State, GetFireInterval(Config, Stance.bIsADS), ShotTime);

	float SprayIndex = 0.0f;
	float RawYaw = 0.0f;
	if (Recovery.Alpha >= 1.0f)
	{
		// 새 스프레이: 완전히 회복됐으므로 첫 발 정확도에서 다시 시작하고 수평 방향도 다시 정한다.
		State.YawSide = 0;
		State.ShotsInSpray = 0;
	}
	else
	{
		// 직전 발 이후 경과 시간만큼 수평이 목표 쪽으로 움직인 위치 + 회복 반영(Evaluate와 같은 식 → 예측/표시/판정 일치).
		Private::ProjectSpray(Profile, State, Recovery, SprayIndex, RawYaw);
	}

	// 난수 소비 순서를 고정해야 서버/클라가 같은 결과를 낸다: [0] 탄퍼짐 반경, [1] 탄퍼짐 방향, [2] 수평 방향 결정/전환.
	FRandomStream ShotStream(ShotSeed);
	const float ErrorRadiusRandom = ShotStream.FRand();
	const float ErrorAngleRandom = ShotStream.FRand();
	const float YawRandom = ShotStream.FRand();

	// 보호 탄이 끝나면 수평 방향을 정하고, 그 뒤로는 매 발 확률적으로 반대편으로 전환한다.
	// 이번 발은 지금 위치(RawYaw)로 나가고, 바뀐 방향은 다음 발까지의 이동부터 반영된다(반동은 발사 "뒤"에 생긴다).
	if (SprayIndex + KINDA_SMALL_NUMBER >= static_cast<float>(Profile.ProtectedBulletCount))
	{
		if (State.YawSide == 0)
		{
			// "the direction of its horizontal spray is random, meaning it can go either left or right"
			State.YawSide = YawRandom < 0.5f ? -1 : 1;
		}
		else if (YawRandom < Profile.YawSwitchChance)
		{
			State.YawSide = -State.YawSide;
		}
	}

	const Private::FStanceModifiers Modifiers = Private::ComputeStanceModifiers(Config, Stance);

	FValorComputedShotData Shot;
	Shot.ShotIndexInSpray = State.ShotsInSpray;
	Shot.SprayIndex = SprayIndex;
	Shot.RecoilPitchDegrees = Private::EvaluateCurve(Profile.VerticalRecoilCurve, SprayIndex, 0.0f) * Modifiers.PitchMultiplier;
	Shot.RecoilYawDegrees = RawYaw * Modifiers.YawMultiplier;
	Shot.FiringErrorDegrees = Private::EvaluateSprayError(Config, Stance.bIsADS, SprayIndex) * Modifiers.ErrorMultiplier + Modifiers.MovementError;
	Shot.ErrorPower = Modifiers.ErrorPower;
	Shot.ErrorRadiusRandom = ErrorRadiusRandom;
	Shot.ErrorAngleRandom = ErrorAngleRandom;

	State.LastShotTime = ShotTime;
	State.LastShotSprayIndex = SprayIndex;
	State.LastShotYaw = RawYaw;
	State.ShotsInSpray++;
	State.bHasFired = true;
	return Shot;
}

FVector ComputeShotDirection(const FRotator& AimRotation, const FValorComputedShotData& Shot)
{
	// 1) 반동: 조준 기준 로컬 공간에서 위(+Pitch)/오른쪽(+Yaw)으로 회전한다.
	//    월드 Pitch/Yaw에 그냥 더하면 위/아래를 크게 볼 때 수평 반동 크기가 왜곡되므로 쿼터니언으로 합성한다.
	const FQuat AimQuat = AimRotation.Quaternion();
	const FQuat RecoilQuat = FRotator(Shot.RecoilPitchDegrees, Shot.RecoilYawDegrees, 0.0f).Quaternion();

	// 2) 탄퍼짐: 반경 = 오차 × U^ErrorPower(중심 편향), 방향 = 균일한 각도. 반동이 옮겨 놓은 지점을 중심으로 흩어진다.
	const float ErrorRadius = Shot.FiringErrorDegrees * FMath::Pow(FMath::Clamp(Shot.ErrorRadiusRandom, 0.0f, 1.0f), Shot.ErrorPower);
	const float ErrorAngle = Shot.ErrorAngleRandom * UE_TWO_PI;
	const FQuat ErrorQuat = FRotator(ErrorRadius * FMath::Sin(ErrorAngle), ErrorRadius * FMath::Cos(ErrorAngle), 0.0f).Quaternion();

	return (AimQuat * RecoilQuat * ErrorQuat).GetForwardVector();
}

int32 MakeShotSeed(int32 WeaponSeed, int32 ShotCounter)
{
	// HashCombine은 엔진이 하위 호환을 보장하는 고정 해시라 서버/클라/플랫폼 간에 같은 값을 낸다.
	// 인접한 발 번호끼리도 시드가 잘 흩어져서 FRandomStream(LCG)의 "인접 시드 상관" 문제를 피한다.
	return static_cast<int32>(HashCombine(static_cast<uint32>(WeaponSeed), static_cast<uint32>(ShotCounter)));
}
}

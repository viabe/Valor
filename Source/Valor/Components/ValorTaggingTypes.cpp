#include "Components/ValorTaggingTypes.h"

namespace
{
	uint8 QuantizeStrength(float Strength)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FMath::Clamp(Strength, 0.0f, 1.0f) * 255.0f), 0, 255));
	}

	float StrengthFromQuantized(uint8 Quantized)
	{
		return static_cast<float>(Quantized) / 255.0f;
	}
}

float ValorTagging::ComputeEnvelope(float ElapsedSeconds, const FValorTaggingConfig& Config)
{
	if (ElapsedSeconds < 0.0f || ElapsedSeconds >= Config.EndSeconds)
	{
		return 0.0f;
	}
	if (ElapsedSeconds < Config.RampInSeconds)
	{
		return Config.RampInSeconds > 0.0f ? ElapsedSeconds / Config.RampInSeconds : 1.0f;
	}
	if (ElapsedSeconds <= Config.FullSlowUntilSeconds)
	{
		return 1.0f;
	}
	const float RecoverSeconds = Config.EndSeconds - Config.FullSlowUntilSeconds;
	return RecoverSeconds > 0.0f ? 1.0f - ((ElapsedSeconds - Config.FullSlowUntilSeconds) / RecoverSeconds) : 0.0f;
}

float ValorTagging::ComputeSpeedMultiplier(const FValorTagState& State, double ServerTime, const FValorTaggingConfig& Config)
{
	if (State.StrengthQuantized == 0)
	{
		return 1.0f;
	}
	const float Elapsed = static_cast<float>(ServerTime - static_cast<double>(State.StartServerTime));
	return 1.0f - (State.GetStrength() * ComputeEnvelope(Elapsed, Config));
}

FValorTagState ValorTagging::AddHit(const FValorTagState& Current, double ServerNow, float Strength, const FValorTaggingConfig& Config)
{
	const float Elapsed = static_cast<float>(ServerNow - static_cast<double>(Current.StartServerTime));
	const bool bActive = Current.StrengthQuantized > 0 && Elapsed >= 0.0f && Elapsed < Config.EndSeconds;
	const float CurrentEnvelope = bActive ? ComputeEnvelope(Elapsed, Config) : 0.0f;

	FValorTagState Result;
	const uint8 NewStrength = QuantizeStrength(Strength);
	Result.StrengthQuantized = bActive ? FMath::Max(Current.StrengthQuantized, NewStrength) : NewStrength;
	// 걸리는 구간의 같은 높이에서 다시 시작한다: 모양(지금 - 새 시작) = 지금 모양.
	Result.StartServerTime = static_cast<float>(ServerNow - static_cast<double>(CurrentEnvelope * Config.RampInSeconds));
	Result.Sequence = static_cast<uint8>(Current.Sequence + 1);
	return Result;
}

FValorTagMoveClock ValorTagging::AcknowledgeOnMoveClock(const FValorTagMoveClock& Current, const FValorTagState& State, float MoveTime, const FValorTaggingConfig& Config)
{
	const float CurrentEnvelope = Current.StrengthQuantized > 0 ? ComputeEnvelope(MoveTime - Current.StartMoveTime, Config) : 0.0f;

	FValorTagMoveClock Result;
	Result.StartMoveTime = MoveTime - (CurrentEnvelope * Config.RampInSeconds);
	Result.StrengthQuantized = State.StrengthQuantized;
	Result.Sequence = State.Sequence;
	return Result;
}

float ValorTagging::ComputeMoveClockMultiplier(const FValorTagMoveClock& Clock, float MoveTime, const FValorTaggingConfig& Config)
{
	if (Clock.StrengthQuantized == 0)
	{
		return 1.0f;
	}
	return 1.0f - (StrengthFromQuantized(Clock.StrengthQuantized) * ComputeEnvelope(MoveTime - Clock.StartMoveTime, Config));
}

float ValorTagging::ComputeAllowedMoveClockMultiplier(const FValorTagMoveClock& Clock, float MoveTime, float ToleranceSeconds, const FValorTaggingConfig& Config)
{
	// 모양이 "내려갔다 유지 후 올라가는" 골짜기라 [이동 시각 - 여유, 이동 시각 + 여유] 구간의 최댓값은 양 끝 중 하나다.
	return FMath::Max(ComputeMoveClockMultiplier(Clock, MoveTime - ToleranceSeconds, Config), ComputeMoveClockMultiplier(Clock, MoveTime + ToleranceSeconds, Config));
}

float ValorTagging::SelectStrength(bool bPellets, float NearestHitDistanceCm, bool bAllPenetrated, const FValorTaggingConfig& Config)
{
	float Strength = Config.StandardStrength;
	if (bPellets && NearestHitDistanceCm > Config.FarPelletDistanceCm)
	{
		Strength = FMath::Min(Strength, Config.FarPelletStrength);
	}
	if (bAllPenetrated)
	{
		Strength = FMath::Min(Strength, Config.WallPenetrationStrength);
	}
	return Strength;
}

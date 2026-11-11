#include "ValorMatchTypes.h"

#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Player/ValorPlayerState.h"

namespace ValorTeams
{
	EValorTeam GetOpposingTeam(EValorTeam Team)
	{
		switch (Team)
		{
		case EValorTeam::TeamA: return EValorTeam::TeamB;
		case EValorTeam::TeamB: return EValorTeam::TeamA;
		default: return EValorTeam::None;
		}
	}

	EValorTeam GetTeamOf(const AActor* Actor)
	{
		const AValorPlayerState* PlayerState = Cast<AValorPlayerState>(Actor);
		if (!PlayerState)
		{
			if (const APawn* Pawn = Cast<APawn>(Actor))
			{
				PlayerState = Pawn->GetPlayerState<AValorPlayerState>();
			}
			else if (const AController* Controller = Cast<AController>(Actor))
			{
				PlayerState = Controller->GetPlayerState<AValorPlayerState>();
			}
		}

		return PlayerState ? PlayerState->GetTeam() : EValorTeam::None;
	}

	bool AreAllies(const AActor* A, const AActor* B)
	{
		const EValorTeam TeamOfA = GetTeamOf(A);
		return TeamOfA != EValorTeam::None && TeamOfA == GetTeamOf(B);
	}
}

namespace ValorMatchRules
{
	int32 ComputeRoundReward(bool bWon, int32 LossStreakIncludingThisRound)
	{
		if (bWon)
		{
			return 3000;
		}

		const int32 Streak = FMath::Clamp(LossStreakIncludingThisRound, 1, 3);
		return 1900 + ((Streak - 1) * 500);
	}

	int32 ClampCredits(int32 Credits, int32 MaxCredits)
	{
		return FMath::Clamp(Credits, 0, MaxCredits);
	}

	EValorTeam GetMatchWinner(int32 ScoreA, int32 ScoreB, int32 RoundsToWin)
	{
		const int32 High = FMath::Max(ScoreA, ScoreB);
		const int32 Low = FMath::Min(ScoreA, ScoreB);
		const bool bOvertime = Low >= RoundsToWin - 1;
		const bool bWon = bOvertime ? (High - Low >= 2) : (High >= RoundsToWin);
		if (!bWon)
		{
			return EValorTeam::None;
		}

		return ScoreA > ScoreB ? EValorTeam::TeamA : EValorTeam::TeamB;
	}

	bool IsOvertimeRound(int32 RoundNumber, int32 RoundsPerHalf)
	{
		return RoundNumber > RoundsPerHalf * 2;
	}

	EValorTeam GetAttackingTeam(int32 RoundNumber, int32 RoundsPerHalf)
	{
		if (IsOvertimeRound(RoundNumber, RoundsPerHalf))
		{
			const int32 OvertimeIndex = RoundNumber - (RoundsPerHalf * 2) - 1;
			return (OvertimeIndex % 2 == 0) ? EValorTeam::TeamA : EValorTeam::TeamB;
		}

		return RoundNumber <= RoundsPerHalf ? EValorTeam::TeamA : EValorTeam::TeamB;
	}

	int32 ComputeEconomyResetCredits(int32 StartingCredits, bool bShopHasSidearms, int32 CheapestWeaponCost)
	{
		if (bShopHasSidearms || CheapestWeaponCost <= 0)
		{
			return StartingCredits;
		}

		return FMath::Max(StartingCredits, CheapestWeaponCost);
	}

	bool IsEconomyResetRound(int32 RoundNumber, int32 RoundsPerHalf)
	{
		return RoundNumber == 1 || RoundNumber == RoundsPerHalf + 1;
	}
}

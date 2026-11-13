#pragma once

#include "CoreMinimal.h"
#include "ValorMatchTypes.generated.h"

class AActor;
class AController;

// 팀. 공격·수비는 팀이 아니라 "이번 라운드에 팀이 맡은 쪽"이다(전반 12라운드 뒤 서로 바뀐다).
UENUM(BlueprintType)
enum class EValorTeam : uint8
{
	None,
	TeamA,
	TeamB
};

UENUM(BlueprintType)
enum class EValorTeamSide : uint8
{
	None,
	Attack,
	Defense
};

// 라운드 진행 단계(서버가 정하고 GameState로 모두에게 복제).
UENUM(BlueprintType)
enum class EValorRoundPhase : uint8
{
	// 데스매치이거나, 경기 모드가 첫 라운드를 시작하기 전(접속 대기). 자유롭게 움직이고 쏠 수 있다.
	WaitingToStart,
	// 구매 단계: 스폰에서 움직이지 못하고(스폰 장벽 대신) 쏠 수 없다. 상점이 열린다.
	BuyPhase,
	// 교전.
	Combat,
	// 스파이크 설치 뒤(폭발까지 45초). 남은 시간은 폭발 시각이다.
	SpikePlanted,
	// 라운드 결과가 정해진 뒤 잠시(교전은 계속 가능, 결과는 바뀌지 않음).
	RoundEnd,
	// 경기 종료. 잠시 뒤 새 경기를 시작한다.
	MatchEnd
};

UENUM(BlueprintType)
enum class EValorRoundEndReason : uint8
{
	None,
	Elimination,
	TimeExpired,
	SpikeDetonated,
	SpikeDefused
};

namespace ValorTeams
{
	// 상대 팀. None은 None.
	VALOR_API EValorTeam GetOpposingTeam(EValorTeam Team);

	// 액터(캐릭터·컨트롤러·PlayerState)의 팀. PlayerState가 없으면(테스트 더미 등) None.
	VALOR_API EValorTeam GetTeamOf(const AActor* Actor);

	// 둘 다 팀이 있고 같은 팀이면 아군. 자기 자신은 아군이 아니다(호출 쪽에서 따로 거른다).
	VALOR_API bool AreAllies(const AActor* A, const AActor* B);
}

// 경기 규칙의 순수 계산(서버 게임 모드와 테스트가 같이 쓴다). 값은 발로란트 경쟁전 기준이다.
namespace ValorMatchRules
{
	// 라운드 보상. 이긴 팀 3000. 진 팀은 연패 수(이번 패배 포함, 1부터)에 따라 1900 / 2400 / 2900(3연패 이상).
	VALOR_API int32 ComputeRoundReward(bool bWon, int32 LossStreakIncludingThisRound);

	// 크레딧 상한(9000)으로 자른다.
	VALOR_API int32 ClampCredits(int32 Credits, int32 MaxCredits = 9000);

	// 경기 승자. 먼저 RoundsToWin(13)을 이기면 승리. 둘 다 RoundsToWin-1(12) 이상이 되면 연장전이라 2점 차가 나야 끝난다.
	VALOR_API EValorTeam GetMatchWinner(int32 ScoreA, int32 ScoreB, int32 RoundsToWin = 13);

	// 이 라운드(1부터)가 연장전인가. 전반·후반 RoundsPerHalf(12)씩을 넘긴 라운드다.
	VALOR_API bool IsOvertimeRound(int32 RoundNumber, int32 RoundsPerHalf = 12);

	// 이 라운드에 공격하는 팀. 전반은 TeamA, 후반은 TeamB, 연장전은 매 라운드 번갈아(첫 연장 라운드는 TeamA).
	VALOR_API EValorTeam GetAttackingTeam(int32 RoundNumber, int32 RoundsPerHalf = 12);

	// 경제 초기화 라운드(1라운드, 후반 첫 라운드)의 시작 크레딧. 상점에 권총이 없으면(권총 에셋 전) 800으로는 실드밖에 못 사므로
	// 가장 싼 총(CheapestWeaponCost)을 하나 살 수 있을 만큼으로 올린다. 권총이 상점에 들어오면 원래 값(StartingCredits)이다.
	VALOR_API int32 ComputeEconomyResetCredits(int32 StartingCredits, bool bShopHasSidearms, int32 CheapestWeaponCost);

	// 전멸 판정의 승자. 설치 전: 먼저 전멸한 팀의 상대가 이긴다. 설치 뒤: 수비 전멸이면 공격 승, 공격 전멸은 끝나지 않는다
	// (수비가 해체해야 이기고, 못 하면 폭발로 공격 승). 끝나지 않으면 None.
	VALOR_API EValorTeam GetEliminationWinner(bool bAttackersEliminated, bool bDefendersEliminated, bool bSpikePlanted, EValorTeam AttackingTeam);

	// 이 라운드 시작에 경제를 초기화하는가(1라운드, 후반 첫 라운드). 초기화하면 크레딧이 시작값이 되고 들고 있던 장비도 없어진다.
	VALOR_API bool IsEconomyResetRound(int32 RoundNumber, int32 RoundsPerHalf = 12);
}

// 스파이크 설치·해체 진행 계산(서버 스파이크와 테스트가 같이 쓴다). 값은 발로란트 기준: 설치 4초, 해체 7초(절반 3.5초 지점 저장).
namespace ValorSpikeRules
{
	// 설치: 누르고 있고 조건이 맞으면 쌓이고, 끊기면 처음부터.
	VALOR_API float AdvancePlant(float Progress, float DeltaSeconds, bool bPlanting);

	// 해체: 누르고 있으면 쌓이고, 끊기면 저장 지점(절반을 넘겼으면 절반)으로 돌아간다. InOutCheckpoint를 갱신한다.
	VALOR_API float AdvanceDefuse(float Progress, float& InOutCheckpoint, float DeltaSeconds, bool bDefusing, float DefuseSeconds);
}

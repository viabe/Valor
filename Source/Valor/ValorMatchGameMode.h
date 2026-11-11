#pragma once

#include "CoreMinimal.h"
#include "ValorGameMode.h"
#include "ValorMatchTypes.h"
#include "ValorMatchGameMode.generated.h"

class AValorGameState;
class AValorPlayerState;

/**
 * 경쟁전 규칙(서버 전용): 두 팀, 라운드 상태 머신, 경제(크레딧·상점). 스파이크는 다음 단계에서 붙인다.
 *
 * 라운드 흐름: 접속 대기(PreMatchDelay) → [구매 30초 → 교전 100초 → 라운드 종료 7초] 반복 → 경기 종료 15초 → 새 경기.
 *   - 라운드 시작: 모든 플레이어를 팀 스폰에 새로 스폰한다. 살아남은 사람은 총·실드를 그대로 가져가고, 죽은 사람은 기본 무장(칼, 권총 BP가 생기면 클래식)이다.
 *   - 승리 조건: 상대 팀 전멸, 또는 시간 종료(수비 승). 스파이크 설치·해체·폭발은 다음 단계.
 *   - 13승 선승, 12라운드 뒤 공수 교대(경제 초기화), 12:12면 연장전(2점 차, 매 라운드 공수 교대, 크레딧 5000).
 * 경제: 시작 800, 승리 3000, 패배 1900/2400/2900(연패), 적 처치 200, 상한 9000. 구매 단계에서만 사고, 이번 라운드에 산 것은 되팔 수 있다.
 * 왜 GameMode인가: 규칙은 서버만 알고 바꾼다. 결과(단계·점수·크레딧·팀)만 GameState/PlayerState로 복제하고,
 *   클라는 그 값으로 화면(HUD)과 예측(구매 단계 이동·발사 금지)을 맞춘다.
 * 트레이드오프:
 *   - 구매 단계에도 움직이고 쏠 수 있다(발로란트). 맵에 스폰 장벽이 아직 없으므로 지금은 구매 단계에 상대 스폰까지 갈 수 있다.
 *   - 죽은 뒤에는 시체 시점에 머문다(아군 관전은 이후).
 *   - 팀별 스폰이 없는 맵(태그 없는 PlayerStart)은 시작점을 서로 먼 두 무리로 나누고, 하나뿐이면 그 앞쪽에 상대 스폰을 만든다.
 */
UCLASS()
class VALOR_API AValorMatchGameMode : public AValorGameMode
{
	GENERATED_BODY()

public:
	AValorMatchGameMode();

	// URL 옵션으로 단계 시간을 바꿀 수 있다(개발·자동 테스트용): ?BuyTime=5?RoundTime=20?RoundEndTime=3?MatchEndTime=5?PreMatchTime=1
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;
	virtual void Logout(AController* Exiting) override;
	virtual void SetPlayerDefaults(APawn* PlayerPawn) override;

	// === 상점(서버). PlayerController의 서버 RPC가 부른다. 성공하면 true ===
	bool TryBuyWeapon(AController* Buyer, TSubclassOf<class AValorWeaponBase> WeaponClass);
	bool TryBuyShield(AController* Buyer, int32 ShieldIndex);
	bool TrySellWeapon(AController* Buyer, TSubclassOf<class AValorWeaponBase> WeaponClass);
	bool TrySellShield(AController* Buyer, int32 ShieldIndex);

	// 개발용: 지금 단계를 바로 끝낸다(단계 타이머가 끝난 것과 같다).
	void DebugSkipPhase();

	// 팀 배정(테스트용 공개): 인원이 적은 팀, 같으면 TeamA.
	static EValorTeam PickTeamForNewPlayer(int32 CountA, int32 CountB);

	// 태그 없는 시작점 나누기(테스트용 공개): 서로 가장 먼 두 점을 기준으로 가까운 쪽 무리(0 또는 1)에 넣는다.
	static void SplitSpawnGroups(const TArray<FVector>& Locations, TArray<int32>& OutGroups);

protected:
	virtual float GetRespawnDelayAfterDeath() const override { return -1.0f; }
	virtual void OnCharacterKilled(AValorCharacter* Victim, AController* VictimController, AValorPlayerState* VictimState, AValorPlayerState* KillerState, bool bSelfKill) override;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Match|Timing", meta=(ClampMin="0.0"))
	float PreMatchDelaySeconds = 3.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Match|Timing", meta=(ClampMin="1.0"))
	float BuyPhaseSeconds = 30.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Match|Timing", meta=(ClampMin="1.0"))
	float RoundSeconds = 100.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Match|Timing", meta=(ClampMin="1.0"))
	float RoundEndSeconds = 7.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Match|Timing", meta=(ClampMin="1.0"))
	float MatchEndSeconds = 15.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Match|Rounds", meta=(ClampMin="1"))
	int32 RoundsPerHalf = 12;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Match|Rounds", meta=(ClampMin="1"))
	int32 RoundsToWin = 13;

	// 경제 초기화 라운드(1라운드, 후반 첫 라운드)의 시작 크레딧. 상점에 권총이 없는 동안(권총 에셋 전)은
	// 가장 싼 총을 하나 살 수 있도록 그 값까지 올려 준다(ValorMatchRules::ComputeEconomyResetCredits, 지금 버키 850).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Match|Economy", meta=(ClampMin="0"))
	int32 StartingCredits = 800;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Match|Economy", meta=(ClampMin="0"))
	int32 OvertimeCredits = 5000;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Match|Economy", meta=(ClampMin="0"))
	int32 KillReward = 200;

	// 구매 단계에 이동을 묶는다(기본 끔: 발로란트처럼 구매 단계에도 움직인다. 스폰 장벽이 없는 맵에서 돌진을 막고 싶을 때 켠다).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Match")
	bool bFreezePlayersDuringBuyPhase = false;

	// 구매 단계에 사격을 막는다(기본 끔).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Match")
	bool bBlockFireDuringBuyPhase = false;

	// 라운드 시작에 바닥의 총(떨어진 총, 맵에 놓인 총 모두)을 없앤다. 맵에 놓인 총은 경제를 깨므로 경기 모드에서는 지운다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Match")
	bool bRemoveWeaponPickupsEachRound = true;

	// 팀별 스폰: PlayerStart의 Player Start Tag가 이 이름이면 그쪽 스폰이다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Match|Spawn")
	FName AttackStartTag = TEXT("Attack");

	UPROPERTY(EditDefaultsOnly, Category="Valor|Match|Spawn")
	FName DefenseStartTag = TEXT("Defense");

	// 시작점이 하나뿐인 맵에서 상대 스폰을 만들 거리(그 시작점의 앞쪽).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Match|Spawn", meta=(ClampMin="200.0"))
	float FallbackSideSeparationCm = 2000.0f;

	// 같은 스폰에 선 팀원 사이 간격(옆으로).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Match|Spawn", meta=(ClampMin="60.0"))
	float TeammateSpawnSpacingCm = 120.0f;

private:
	AValorGameState* GetValorGameState() const;
	void GetMatchPlayers(TArray<AController*>& OutControllers) const;
	int32 CountPlayers(EValorTeam Team) const;
	int32 CountAlivePlayers(EValorTeam Team) const;

	void SetPhase(EValorRoundPhase NewPhase, float DurationSeconds);
	void HandlePhaseTimerExpired();
	void BeginRound();
	void StartCombatPhase();
	void EndRound(EValorTeam Winner, EValorRoundEndReason Reason);
	void ResetMatch();
	void CheckElimination();

	// 스폰
	void RebuildSpawnBases();
	FTransform FindSpawnTransform(EValorTeamSide Side, int32 IndexInSide) const;
	void SpawnForRound(AController* Controller, int32 IndexInSide, bool bKeepLoadout);

	// 상점 공통 검사: 구매 단계 + 살아 있는 캐릭터 + PlayerState.
	bool GetBuyContext(AController* Buyer, class AValorCharacter*& OutCharacter, AValorPlayerState*& OutPlayerState) const;
	void SetShield(class AValorCharacter& Character, float Shield, float MaxShield) const;
	void RefundWeaponPurchaseInSlot(class AValorCharacter& Character, AValorPlayerState& PlayerState, uint8 SlotValue);

	FTimerHandle PhaseTimerHandle;
	EValorRoundPhase CurrentPhase = EValorRoundPhase::WaitingToStart;
	bool bMatchScheduled = false;
	int32 RoundNumber = 0;
	int32 ScoreA = 0;
	int32 ScoreB = 0;
	int32 LossStreakA = 0;
	int32 LossStreakB = 0;

	// 라운드 시작 때 팀에 사람이 있었나(혼자 연습할 때 빈 팀을 전멸로 치지 않는다).
	bool bTeamAHadPlayers = false;
	bool bTeamBHadPlayers = false;

	TArray<FTransform> AttackSpawnBases;
	TArray<FTransform> DefenseSpawnBases;
};

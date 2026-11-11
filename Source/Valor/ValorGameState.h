#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "ValorMatchTypes.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"
#include "ValorGameState.generated.h"

class AValorWeaponBase;

// 킬피드 한 줄. 이름은 문자열로 보내 처치자/피해자가 접속을 끊어 PlayerState가 사라져도 줄이 깨지지 않게 한다.
USTRUCT()
struct FValorKillFeedEntry
{
	GENERATED_BODY()

	// 비어 있으면 처치자 없음(환경 피해).
	UPROPERTY()
	FString KillerName;

	UPROPERTY()
	FString VictimName;

	// 처치 무기. 표시 이름은 클라가 클래스 CDO의 데이터 에셋에서 읽는다(FText 대신 NetGUID 하나).
	UPROPERTY()
	TSubclassOf<AValorWeaponBase> WeaponClass;

	UPROPERTY()
	bool bHeadshot = false;

	// 벽을 관통한 탄으로 처치(월뱅).
	UPROPERTY()
	bool bWallbang = false;

	// 킬피드 이름 색(내 팀 / 상대 팀)용.
	UPROPERTY()
	EValorTeam KillerTeam = EValorTeam::None;

	UPROPERTY()
	EValorTeam VictimTeam = EValorTeam::None;
};

// 상점의 실드 상품(클래스 기본값으로 정하므로 모든 머신에 같다).
USTRUCT()
struct FValorShieldOffer
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category="Valor|Shop")
	FString DisplayName;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Shop", meta=(ClampMin="0"))
	int32 Cost = 400;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Shop", meta=(ClampMin="0.0"))
	float ShieldAmount = 25.0f;
};

// 상점의 총 한 칸(실행 중 카탈로그에서 만든다).
struct FValorShopWeaponEntry
{
	TSubclassOf<AValorWeaponBase> WeaponClass;
	FString DisplayName;
	EValorWeaponCategory Category = EValorWeaponCategory::Rifle;
	int32 Cost = 0;
};

/**
 * 매치 공용 상태: 킬피드, 라운드 진행(단계·남은 시간·점수·공격 팀·라운드 결과), 상점 목록.
 *
 * 왜 GameState인가: GameState는 모든 클라에 항상 관련(relevant)한 유일한 액터라, 맵 어디서 일어난 킬이든 모두에게 보낼 수 있고,
 *   라운드 상태도 모두가 같은 값을 본다. 규칙(언제 단계를 넘기고 누가 이기나)은 서버 GameMode가 정하고 여기에는 결과만 쓴다.
 * 네트워크:
 *   - 킬 1건 = NetMulticast Reliable 1회. 목록은 복제하지 않는다(늦게 접속한 사람에게 지난 킬피드는 필요 없다).
 *   - 라운드 상태는 작은 값 몇 개를 프로퍼티 복제한다. 단계 끝 시각은 서버 월드 시간이라 클라가 서버 시간 추정치로 남은 시간을 센다
 *     (매 초 타이머를 복제하지 않는다).
 *   - 상점 목록은 복제하지 않는다. 클래스 기본값(총 BP 경로, 실드 상품)으로 서버와 클라가 각자 같은 목록을 만든다.
 */
UCLASS()
class VALOR_API AValorGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AValorGameState();

	// 로컬에 쌓인 킬피드 한 줄(받은 시각 포함). HUD가 시간이 지난 줄을 흐리게/숨긴다.
	struct FKillFeedRecord
	{
		FValorKillFeedEntry Entry;

		// 받은 순간 무기 클래스에서 꺼낸 표시 이름. 기록은 GC가 추적하지 않는 배열이라 클래스 포인터는 다시 읽지 않는다.
		FString WeaponDisplayName;
		double LocalTime = 0.0;
	};

	// 서버 전용 진입점: GameMode가 킬을 확정하면 호출한다.
	void BroadcastKill(const FValorKillFeedEntry& Entry);

	const TArray<FKillFeedRecord>& GetKillFeed() const { return KillFeed; }

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// === 라운드 상태(서버 GameMode가 쓴다) ===
	void SetRoundPhase(EValorRoundPhase NewPhase, float DurationSeconds);
	void SetRoundNumber(int32 NewRoundNumber, EValorTeam NewAttackingTeam);
	void SetTeamScores(int32 NewScoreA, int32 NewScoreB);
	void SetRoundResult(EValorTeam Winner, EValorRoundEndReason Reason);
	void SetMatchWinner(EValorTeam Winner);
	void SetUsesRounds(bool bNewUsesRounds) { bUsesRounds = bNewUsesRounds; }
	void SetFreezeDuringBuyPhase(bool bNewFreeze) { bFreezeDuringBuyPhase = bNewFreeze; }
	void SetBlockFireDuringBuyPhase(bool bNewBlock) { bBlockFireDuringBuyPhase = bNewBlock; }

	// === 라운드 상태 읽기(모든 머신) ===
	bool UsesRounds() const { return bUsesRounds; }
	EValorRoundPhase GetRoundPhase() const { return RoundPhase; }
	int32 GetRoundNumber() const { return RoundNumber; }
	EValorTeam GetAttackingTeam() const { return AttackingTeam; }
	EValorTeamSide GetSideOfTeam(EValorTeam Team) const;
	int32 GetTeamScore(EValorTeam Team) const;
	EValorTeam GetLastRoundWinner() const { return LastRoundWinner; }
	EValorRoundEndReason GetLastRoundEndReason() const { return LastRoundEndReason; }
	EValorTeam GetMatchWinner() const { return MatchWinner; }

	// 이번 단계 남은 시간(초). 시간 제한이 없으면 -1.
	float GetPhaseTimeRemaining() const;

	// 지금 총을 쏠 수 있는가(경기 종료에는 못 쏜다. 구매 단계는 게임 모드 설정에 따름, 기본은 쏠 수 있음). 클라 예측과 서버 검증이 같은 함수를 쓴다.
	bool IsWeaponFireAllowed() const;

	// 지금 이동이 묶였는가(구매 단계에 게임 모드가 켠 경우만, 기본은 안 묶음). 이동 예측(최대 속도)에 쓰이므로 서버와 클라가 같은 복제값을 본다.
	bool IsMovementLocked() const;

	// 상점이 열려 있는가(구매 단계).
	bool IsBuyPhase() const { return bUsesRounds && RoundPhase == EValorRoundPhase::BuyPhase; }

	// === 상점 ===
	const TArray<FValorShopWeaponEntry>& GetShopWeapons() const { return ShopWeapons; }
	const TArray<FValorShieldOffer>& GetShopShields() const { return ShopShields; }
	const FValorShopWeaponEntry* FindShopWeapon(const UClass* WeaponClass) const;

protected:
	UFUNCTION(NetMulticast, Reliable)
	void MulticastAddKillFeedEntry(const FValorKillFeedEntry& Entry);

	// 로컬에 기억할 최대 줄 수(오래된 줄부터 버린다).
	UPROPERTY(EditDefaultsOnly, Category="Valor|KillFeed", meta=(ClampMin="1"))
	int32 MaxKillFeedEntries = 6;

	// 상점에 올릴 총 BP. 에셋이 없는 경로는 건너뛴다(권총 BP는 아직 없음).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Shop")
	TArray<TSoftClassPtr<AValorWeaponBase>> ShopWeaponClasses;

	// 보조무기(권총) 구매 허용. 권총 에셋이 들어오기 전까지 끈다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Shop")
	bool bAllowSidearmPurchases = false;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Shop")
	TArray<FValorShieldOffer> ShopShields;

	UPROPERTY(Replicated)
	bool bUsesRounds = false;

	UPROPERTY(Replicated)
	bool bFreezeDuringBuyPhase = false;

	UPROPERTY(Replicated)
	bool bBlockFireDuringBuyPhase = false;

	UPROPERTY(Replicated)
	EValorRoundPhase RoundPhase = EValorRoundPhase::WaitingToStart;

	// 이번 단계가 끝나는 서버 월드 시간. 0 이하면 시간 제한 없음.
	UPROPERTY(Replicated)
	float PhaseEndServerTime = 0.0f;

	UPROPERTY(Replicated)
	int32 RoundNumber = 0;

	UPROPERTY(Replicated)
	EValorTeam AttackingTeam = EValorTeam::TeamA;

	UPROPERTY(Replicated)
	int32 ScoreA = 0;

	UPROPERTY(Replicated)
	int32 ScoreB = 0;

	UPROPERTY(Replicated)
	EValorTeam LastRoundWinner = EValorTeam::None;

	UPROPERTY(Replicated)
	EValorRoundEndReason LastRoundEndReason = EValorRoundEndReason::None;

	UPROPERTY(Replicated)
	EValorTeam MatchWinner = EValorTeam::None;

private:
	void BuildShopCatalog();

	TArray<FKillFeedRecord> KillFeed;

	// UPROPERTY가 아니므로 클래스는 ShopWeaponClassRefs로 붙잡아 GC에서 지킨다.
	TArray<FValorShopWeaponEntry> ShopWeapons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UClass>> ShopWeaponClassRefs;
};

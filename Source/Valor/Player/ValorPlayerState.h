#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystem/ValorAgentTypes.h"
#include "ValorMatchTypes.h"
#include "ValorPlayerState.generated.h"

class AValorWeaponBase;

// 이번 라운드 구매 한 건. 구매 단계 안에서 되팔기(환불)와, 같은 슬롯 총을 다시 살 때 자동 환불에 쓴다.
USTRUCT()
struct FValorRoundPurchase
{
	GENERATED_BODY()

	// 총 구매면 그 클래스, 실드 구매면 nullptr.
	UPROPERTY()
	TSubclassOf<AValorWeaponBase> WeaponClass;

	// 실드 구매면 상점 실드 목록 번호, 아니면 INDEX_NONE.
	UPROPERTY()
	int32 ShieldIndex = INDEX_NONE;

	// 스킬 충전 구매면 스킬 칸(EValorAbilitySlot), 아니면 INDEX_NONE.
	UPROPERTY()
	int32 AbilitySlot = INDEX_NONE;

	UPROPERTY()
	int32 Cost = 0;

	// 실드 되팔기 때 되돌릴 값(서버만 쓴다).
	UPROPERTY(NotReplicated)
	float PreviousShield = 0.0f;

	UPROPERTY(NotReplicated)
	float PreviousMaxShield = 0.0f;
};

// 살아남아 다음 라운드로 가져가는 장비(서버 전용). 라운드 시작에 캐릭터를 새로 스폰하면서 다시 지급한다.
struct FValorCarriedLoadout
{
	TSubclassOf<AValorWeaponBase> PrimaryClass;
	TSubclassOf<AValorWeaponBase> SecondaryClass;
	float Shield = 0.0f;
	float MaxShield = 0.0f;
	bool bValid = false;
};

/**
 * 플레이어별 상태: 전적(처치/사망), 팀, 크레딧. 캐릭터가 죽고 다시 태어나도 유지돼야 하므로 Pawn이 아니라 PlayerState에 둔다.
 *
 * 네트워크: 전적·팀·크레딧은 모두에게 복제한다(점수판, 팀 색, 아군 구매 화면). 값은 GameMode(서버)만 바꾼다.
 *   이번 라운드 구매 목록은 본인에게만 복제한다(상점에서 "되팔기"로 표시).
 *   서버 setter는 값을 바꾸면 ForceNetUpdate로 바로 보낸다(엔진 기본 주기 1초를 기다리면 구매·요원 선택이 한 박자 늦게 보인다).
 * 트레이드오프: 크레딧이 적에게도 복제된다(발로란트는 적 경제를 직접 보여 주지 않는다). 팀별 복제 필터는 매치 규칙이 굳은 뒤에 넣는다.
 */
UCLASS()
class VALOR_API AValorPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AValorPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 서버 전용.
	void AddKill(bool bHeadshot);
	void AddDeath();
	void ResetScore();

	UFUNCTION(BlueprintPure, Category="Valor|Score")
	int32 GetKills() const { return Kills; }

	UFUNCTION(BlueprintPure, Category="Valor|Score")
	int32 GetDeaths() const { return Deaths; }

	UFUNCTION(BlueprintPure, Category="Valor|Score")
	int32 GetHeadshotKills() const { return HeadshotKills; }

	// 어시스트·전투 점수(점수판). 서버 전용 변경.
	UFUNCTION(BlueprintPure, Category="Valor|Score")
	int32 GetAssists() const { return Assists; }

	UFUNCTION(BlueprintPure, Category="Valor|Score")
	int32 GetCombatScore() const { return CombatScore; }

	void AddAssist();
	void AddCombatScore(int32 Points);

	// 이번 라운드(경기 모드) 또는 이번 목숨(데스매치) 처치 수. 연속 처치 추가 점수용, 서버 전용(복제하지 않음).
	int32 AddRoundKill() { return ++RoundKills; }
	void ResetRoundKills() { RoundKills = 0; }

	// === 팀 ===
	UFUNCTION(BlueprintPure, Category="Valor|Match")
	EValorTeam GetTeam() const { return Team; }

	// 서버 전용.
	void SetTeam(EValorTeam NewTeam);

	// === 경제 ===
	UFUNCTION(BlueprintPure, Category="Valor|Match")
	int32 GetCredits() const { return Credits; }

	// 서버 전용. 상한(9000)으로 자른다.
	void SetCredits(int32 NewCredits);
	void AddCredits(int32 Amount);

	const TArray<FValorRoundPurchase>& GetRoundPurchases() const { return RoundPurchases; }
	bool HasPurchasedWeaponThisRound(const UClass* WeaponClass) const;
	bool HasPurchasedShieldThisRound(int32 ShieldIndex) const;
	bool HasPurchasedAbilityThisRound(EValorAbilitySlot Slot) const;
	bool TakeAbilityPurchase(EValorAbilitySlot Slot, FValorRoundPurchase& OutPurchase);

	// 서버 전용.
	void AddRoundPurchase(const FValorRoundPurchase& Purchase);
	// 가장 최근 일치 구매를 꺼내 지운다. 없으면 false.
	bool TakeWeaponPurchase(const UClass* WeaponClass, FValorRoundPurchase& OutPurchase);
	bool TakeShieldPurchase(int32 ShieldIndex, FValorRoundPurchase& OutPurchase);
	void ClearRoundPurchases();

	// 서버 전용: 다음 라운드로 가져갈 장비.
	FValorCarriedLoadout CarriedLoadout;

	// === 요원·스킬 ===
	// 스킬 충전과 궁극기 포인트는 캐릭터가 아니라 여기 둔다(죽고 다시 스폰돼도 남는다).
	TSubclassOf<UValorAgentDefinition> GetAgentClass() const { return AgentClass; }
	const UValorAgentDefinition* GetAgent() const;
	int32 GetAbilityCharges(EValorAbilitySlot Slot) const;
	int32 GetUltimatePoints() const { return UltimatePoints; }

	// 서버 전용.
	void SetAgentClass(TSubclassOf<UValorAgentDefinition> NewAgentClass);
	void SetAbilityCharges(EValorAbilitySlot Slot, int32 Charges);
	void SetUltimatePoints(int32 Points);

	// 요원 선택 단계에서 확정했나(확정한 요원은 같은 팀이 고를 수 없다).
	bool IsAgentLocked() const { return bAgentLocked; }
	void SetAgentLocked(bool bLocked);

protected:
	// 서버에서 복제 값이 바뀌었을 때 부른다: 1초 주기를 기다리지 않고 다음 네트워크 틱에 보낸다.
	void NotifyReplicatedStateChanged();

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Score")
	int32 Kills = 0;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Score")
	int32 Deaths = 0;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Score")
	int32 HeadshotKills = 0;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Score")
	int32 Assists = 0;

	// 경기 전체 전투 점수 합(ACS = 합 / 라운드 수).
	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Score")
	int32 CombatScore = 0;

	int32 RoundKills = 0;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Match")
	EValorTeam Team = EValorTeam::None;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Match")
	int32 Credits = 0;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Match")
	TArray<FValorRoundPurchase> RoundPurchases;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Agent")
	TSubclassOf<UValorAgentDefinition> AgentClass;

	// 스킬 칸별 충전 수(EValorAbilitySlot 순서). 모두에게 복제한다(아군 화면·관전 표시).
	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Agent")
	TArray<int32> AbilityCharges;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Agent")
	int32 UltimatePoints = 0;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Agent")
	bool bAgentLocked = false;
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
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
};

/**
 * 매치 공용 상태. 지금은 킬피드만 담당한다(라운드/점수/팀은 이후 매치 규칙과 함께 확장).
 *
 * 왜 GameState인가: GameState는 모든 클라에 항상 관련(relevant)한 유일한 액터라, 맵 어디서 일어난 킬이든 모두에게 보낼 수 있다.
 * 네트워크: 킬 1건 = NetMulticast Reliable 1회. 킬은 드물고(초당 몇 건 이하) 빠지면 킬피드가 거짓말을 하므로 신뢰성으로 보낸다.
 *   목록 자체는 복제하지 않는다 — 나중에 접속한 사람에게 지난 킬피드는 필요 없고, 배열 복제보다 사건 전송이 싸다.
 * 트레이드오프: 늦게 접속한 클라는 접속 전 킬을 못 본다(발로란트 킬피드도 실시간 표시용이라 문제 없음).
 */
UCLASS()
class VALOR_API AValorGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
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

protected:
	UFUNCTION(NetMulticast, Reliable)
	void MulticastAddKillFeedEntry(const FValorKillFeedEntry& Entry);

	// 로컬에 기억할 최대 줄 수(오래된 줄부터 버린다).
	UPROPERTY(EditDefaultsOnly, Category="Valor|KillFeed", meta=(ClampMin="1"))
	int32 MaxKillFeedEntries = 6;

private:
	TArray<FKillFeedRecord> KillFeed;
};

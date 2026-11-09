#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ValorPlayerState.generated.h"

/**
 * 플레이어별 전적(처치/사망). 캐릭터가 죽고 다시 태어나도 유지돼야 하므로 Pawn이 아니라 PlayerState에 둔다.
 *
 * 네트워크: 모든 클라에 복제한다(점수판·킬피드 색 구분에 필요). 값은 GameMode(서버)만 바꾼다.
 *   int32 몇 개라 변경 시에만 보내는 프로퍼티 복제 비용은 무시할 만하다.
 * 트레이드오프: 어시스트·경제(크레딧)·팀은 라운드/매치 규칙과 함께 들어올 예정이라 아직 없다.
 */
UCLASS()
class VALOR_API AValorPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 서버 전용.
	void AddKill(bool bHeadshot);
	void AddDeath();

	UFUNCTION(BlueprintPure, Category="Valor|Score")
	int32 GetKills() const { return Kills; }

	UFUNCTION(BlueprintPure, Category="Valor|Score")
	int32 GetDeaths() const { return Deaths; }

	UFUNCTION(BlueprintPure, Category="Valor|Score")
	int32 GetHeadshotKills() const { return HeadshotKills; }

protected:
	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Score")
	int32 Kills = 0;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Score")
	int32 Deaths = 0;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Score")
	int32 HeadshotKills = 0;
};

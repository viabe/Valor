#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ValorGameMode.generated.h"

class AValorCharacter;
class AValorWeaponBase;
struct FValorDamageEvent;

/**
 * 게임 규칙(서버에만 존재). 지금 규칙은 데스매치형 기본 루프: 처치 → 기록 → 킬피드 → 몇 초 뒤 부활.
 *
 * 흐름(서버): 캐릭터 AttributeSet이 체력 0을 알림 → AValorCharacter::Die가 사망을 확정 → HandleCharacterKilled가
 *   전적(PlayerState)·킬피드(GameState 멀티캐스트)·사망 정보(피해자 컨트롤러)를 처리하고 부활 타이머를 건다 →
 *   RespawnPlayer가 시체를 남긴 채 빙의를 풀고 적에게서 가장 먼 PlayerStart에 새 캐릭터를 스폰한다.
 * 왜 GameMode인가: 킬 크레딧과 부활 규칙은 모드마다 다르다(데스매치/스파이크 러시/경쟁전). 캐릭터는 "죽었다"만 알리고
 *   규칙은 여기서 바꾸면 되므로, 이후 라운드제 모드는 이 클래스를 상속하거나 교체하면 된다.
 * 네트워크: GameMode는 서버 전용이라 복제가 없다. 결과는 PlayerState(전적), GameState(킬피드), PlayerController(사망 화면)로 나간다.
 * 트레이드오프: 리스폰 타이머를 컨트롤러 약참조로만 들고 있어 취소 API가 없다(라운드제에서는 라운드 상태 머신이 대신 관리할 것).
 */
UCLASS()
class VALOR_API AValorGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AValorGameMode();

	// 서버 전용: 캐릭터가 사망을 확정한 직후 호출한다.
	virtual void HandleCharacterKilled(AValorCharacter* Victim, const FValorDamageEvent& KillingEvent);

	// 스폰 지점 선택 규칙(테스트용으로 공개): 위협(살아 있는 적) 중 가장 가까운 것까지의 거리가 최대인 후보.
	// 위협이 없거나 후보가 없으면 INDEX_NONE(호출 쪽이 무작위/기본 규칙을 쓴다).
	static int32 PickSafestSpawnIndex(const TArray<FVector>& CandidateLocations, const TArray<FVector>& ThreatLocations);

	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override;
	virtual void SetPlayerDefaults(APawn* PlayerPawn) override;

protected:
	// 이 처치 뒤 부활까지 시간(초). 음수면 부활하지 않는다(라운드제: 다음 라운드 시작에 다시 스폰).
	virtual float GetRespawnDelayAfterDeath() const { return RespawnDelay; }

	// 전적·킬피드·사망 정보를 처리한 뒤 부르는 규칙 훅(서버). 라운드제 모드가 처치 보상·전멸 판정을 여기서 한다.
	virtual void OnCharacterKilled(AValorCharacter* Victim, AController* VictimController, class AValorPlayerState* VictimState, class AValorPlayerState* KillerState, bool bSelfKill) {}

	// 사망 후 부활까지 시간(초).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Respawn", meta=(ClampMin="0.0"))
	float RespawnDelay = 3.0f;

	// 부활한 뒤 이전 시체를 남겨 둘 시간(초). 0이면 부활과 동시에 지운다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Respawn", meta=(ClampMin="0.0"))
	float CorpseLifeSpanAfterRespawn = 5.0f;

	// 스폰할 때 주무기 칸에 넣고 들고 시작할 무기(데스매치 기본 지급). 비우면 칼(과 보조무기)로 시작해 바닥 총을 주워야 한다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Loadout")
	TSubclassOf<AValorWeaponBase> StartingWeaponClass;

	// 스폰할 때 보조무기 칸에 넣을 무기(발로란트 기본 지급 = 클래식). 소프트 참조인 이유: 클래식 BP(메시 연결)는 아직 없을 수 있고,
	// 하드 참조(FClassFinder)는 에셋이 없으면 시작할 때마다 오류를 남긴다. 에셋이 있을 때만 불러 지급한다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Loadout")
	TSoftClassPtr<AValorWeaponBase> StartingSecondaryWeaponClass;

	// 스폰할 때 채워 줄 실드(발로란트 중형 실드 = 50). 0이면 실드 없이 시작한다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Loadout", meta=(ClampMin="0.0"))
	float SpawnShield = 50.0f;

private:
	void RespawnPlayer(TWeakObjectPtr<AController> ControllerToRespawn);

	// 기본 보조무기 클래스를 (있으면) 한 번 불러 캐시한다.
	TSubclassOf<AValorWeaponBase> ResolveStartingSecondaryWeaponClass();

	UPROPERTY(Transient)
	TSubclassOf<AValorWeaponBase> ResolvedStartingSecondaryWeaponClass;

	bool bStartingSecondaryResolved = false;
};

#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/PlayerController.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"
#include "ValorPlayerController.generated.h"

class AValorWeaponBase;
class USoundBase;

// 서버가 확정한 명중 1건(사수에게 보냄). 한 발이 한 대상에게 준 피해의 합(산탄은 펠릿 합산)이다.
USTRUCT()
struct FValorHitConfirm
{
	GENERATED_BODY()

	// 실드 + 체력에서 실제로 깎인 양(남은 체력보다 큰 피해는 잘린 값).
	UPROPERTY()
	float Damage = 0.0f;

	// 대표 부위(산탄은 가장 치명적인 펠릿).
	UPROPERTY()
	EValorHitZone HitZone = EValorHitZone::None;

	UPROPERTY()
	bool bKilled = false;
};

// 내가 죽었을 때의 정보(피해자에게 보냄). 사망 화면이 읽는다.
USTRUCT()
struct FValorDeathInfo
{
	GENERATED_BODY()

	// 비어 있으면 처치자 없음(낙사 등 이후 환경 피해).
	UPROPERTY()
	FString KillerName;

	// 처치 무기 클래스(총 또는 칼). 이름(FText) 대신 클래스 참조를 보내 NetGUID 하나로 끝내고, 클라가 CDO에서 이름을 읽는다.
	UPROPERTY()
	TSubclassOf<AActor> WeaponClass;

	UPROPERTY()
	bool bHeadshot = false;

	// 부활 시각(서버 월드 시간). 클라는 GameState의 서버 시간 추정치와 비교해 남은 시간을 표시한다.
	UPROPERTY()
	float RespawnServerTime = 0.0f;
};

/**
 * 플레이어 컨트롤러: 입력 모드 설정 + "이 플레이어 화면에만" 필요한 전투 피드백의 수신 창구.
 *
 * 왜 컨트롤러인가: 명중 확인·피격 방향·사망 정보는 그 플레이어 한 명에게만 의미가 있다. 컨트롤러는 소유 연결에만 존재하는
 *   액터이므로 Client RPC가 정확히 그 플레이어에게만 가고, 캐릭터가 죽어 다른 캐릭터로 바뀌어도(리스폰) 그대로 남는다.
 * 네트워크:
 *   - 명중 확인/피격 방향: Client Unreliable. 연사 중 매 발 오는 순수 연출이라 유실돼도 다음 발이 덮는다.
 *   - 사망 정보: Client Reliable. 한 번뿐이고 빠지면 사망 화면이 비므로 신뢰성으로 보낸다.
 * 트레이드오프: HUD가 매 프레임 여기 값을 읽는 폴링 방식이다. 이벤트 구독보다 단순하고, 값이 몇 개뿐이라 비용이 없다.
 */
UCLASS()
class VALOR_API AValorPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// 피격 방향 표시 1건(로컬 전용 기록).
	struct FDamageIndicator
	{
		FVector SourceLocation = FVector::ZeroVector;
		double LocalTime = 0.0;
		float Damage = 0.0f;
	};

	UFUNCTION(Client, Unreliable)
	void ClientNotifyHitConfirmed(const FValorHitConfirm& HitConfirm);

	UFUNCTION(Client, Unreliable)
	void ClientNotifyDamageTaken(FVector_NetQuantize SourceLocation, float Damage);

	UFUNCTION(Client, Reliable)
	void ClientNotifyKilled(const FValorDeathInfo& DeathInfo);

	// 섬광에 눈이 부심(서버가 시선·가림으로 시간을 정해 이 플레이어에게만 보낸다). 화면 연출만 한다.
	UFUNCTION(Client, Reliable)
	void ClientFlashed(float Seconds);

	// 지금 눈부심 세기(0~1). 처음 70%는 하얗게 가득, 남은 시간에 걸쳐 걷힌다.
	float GetFlashAlpha() const;

	// === 상점 ===
	// 구매 메뉴(로컬 화면). 구매 단계에 살아 있을 때만 열린다. 열려 있으면 마우스 커서가 보이고 시점 회전이 멈춘다.
	void ToggleBuyMenu();
	void SetBuyMenuOpen(bool bOpen);
	bool IsBuyMenuOpen() const { return bBuyMenuOpen; }

	// 서버 RPC: 규칙 검사(구매 단계·크레딧·상점 목록)는 서버 게임 모드가 한다. 실패는 조용히 무시한다(크레딧 복제로 결과가 보인다).
	UFUNCTION(Server, Reliable)
	void ServerBuyWeapon(TSubclassOf<AValorWeaponBase> WeaponClass);

	UFUNCTION(Server, Reliable)
	void ServerSellWeapon(TSubclassOf<AValorWeaponBase> WeaponClass);

	UFUNCTION(Server, Reliable)
	void ServerBuyShield(int32 ShieldIndex);

	UFUNCTION(Server, Reliable)
	void ServerSellShield(int32 ShieldIndex);

	// 요원 선택 단계: 고르기·확정(규칙 검사는 서버 게임 모드).
	UFUNCTION(Server, Reliable)
	void ServerSelectAgent(TSubclassOf<UValorAgentDefinition> AgentClass);

	UFUNCTION(Server, Reliable)
	void ServerLockAgent();

	// 요원 선택 창이 열려 있나(선택 단계 동안 자동으로 열리고 커서가 보인다).
	bool IsAgentSelectOpen() const { return bAgentSelectOpen; }

	// 스킬 충전 구매·되팔기(칸 번호 = EValorAbilitySlot).
	UFUNCTION(Server, Reliable)
	void ServerBuyAbility(uint8 Slot);

	UFUNCTION(Server, Reliable)
	void ServerSellAbility(uint8 Slot);

	virtual void PlayerTick(float DeltaTime) override;

	// === 개발용 콘솔 명령(경기 모드 테스트). 출시 빌드에서는 서버가 무시한다 ===
	// ValorCredits 9000: 내 크레딧을 정한다.
	UFUNCTION(Exec)
	void ValorCredits(int32 Amount);

	// ValorSkipPhase: 지금 단계(대기·구매·교전·라운드 종료·경기 종료)를 바로 끝낸다. 교전을 건너뛰면 시간 종료(수비 승)로 처리된다.
	UFUNCTION(Exec)
	void ValorSkipPhase();

	UFUNCTION(Server, Reliable)
	void ServerDebugSetCredits(int32 Amount);

	UFUNCTION(Server, Reliable)
	void ServerDebugSkipPhase();

	// ValorAddBot [A|B]: 봇(허수아비)을 팀에 넣는다(생략하면 인원이 적은 팀).
	UFUNCTION(Exec)
	void ValorAddBot(const FString& Team);

	// ValorKillMe: 내 캐릭터를 즉사시킨다(관전 확인용).
	UFUNCTION(Exec)
	void ValorKillMe();

	// ValorUltPoints 6: 내 궁극기 포인트를 정한다.
	UFUNCTION(Exec)
	void ValorUltPoints(int32 Points);

	// ValorAgent Flare: 요원을 바꾼다(요원 선택 화면 전 개발용).
	UFUNCTION(Exec)
	void ValorAgent(const FString& AgentName);

	UFUNCTION(Server, Reliable)
	void ServerDebugSetAgent(const FString& AgentName);

	UFUNCTION(Server, Reliable)
	void ServerDebugSetUltimatePoints(int32 Points);

	UFUNCTION(Server, Reliable)
	void ServerDebugAddBot(uint8 Team);

	UFUNCTION(Server, Reliable)
	void ServerDebugKillMe();

	// === 관전(로컬 화면 전용) ===
	// 경기 모드에서 죽으면 DeathCamSeconds 동안 시체 시점을 보여 준 뒤 살아 있는 아군 뒤 3인칭 카메라로 넘어간다.
	// 왼쪽 클릭 = 다음 아군, 오른쪽 클릭 = 이전 아군. 보던 아군이 죽으면 다음 아군으로, 아군이 없으면 시체 시점으로 돌아간다.
	// 네트워크: 카메라는 이 화면에만 있으면 되므로 복제하지 않는다. 아군 위치·시선(조준 피치)은 이미 복제되는 캐릭터 값을 쓴다.
	bool IsSpectating() const { return SpectateTarget.IsValid(); }
	class AValorCharacter* GetSpectateTarget() const { return SpectateTarget.Get(); }
	void SpectateNext();
	void SpectatePrevious();

	// === HUD가 읽는 값(로컬 월드 시간 기준) ===
	const FValorHitConfirm& GetLastHitConfirm() const { return LastHitConfirm; }
	double GetLastHitConfirmTime() const { return LastHitConfirmTime; }
	const TArray<FDamageIndicator>& GetDamageIndicators() const { return DamageIndicators; }
	const FValorDeathInfo& GetLastDeathInfo() const { return LastDeathInfo; }
	bool HasDeathInfo() const { return bHasDeathInfo; }

protected:
	virtual void BeginPlay() override;
	virtual void AcknowledgePossession(APawn* InPawn) override;

	// 명중 확인음(선택). 비워 두면 소리 없이 히트마커만 그린다. 발로란트처럼 헤드샷은 따로 구분할 수 있다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Feedback")
	TObjectPtr<USoundBase> HitConfirmSound;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Feedback")
	TObjectPtr<USoundBase> HeadshotConfirmSound;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Feedback")
	TObjectPtr<USoundBase> KillConfirmSound;

	// 죽은 뒤 관전으로 넘어가기 전 시체 시점 시간(초).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Spectate", meta=(ClampMin="0.0"))
	float DeathCamSeconds = 2.0f;

	// 관전 카메라: 아군 눈 뒤로 이만큼, 오른쪽으로 ShoulderOffset만큼(어깨 너머 시점).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Spectate", meta=(ClampMin="50.0"))
	float SpectateArmLength = 230.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Spectate")
	float SpectateShoulderOffset = 40.0f;

	// 동시에 기억할 피격 방향 표시 수. 오래된 것부터 버린다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Feedback", meta=(ClampMin="1"))
	int32 MaxDamageIndicators = 8;

private:
	FValorHitConfirm LastHitConfirm;
	double LastHitConfirmTime = -1000.0;

	TArray<FDamageIndicator> DamageIndicators;

	// UPROPERTY: 안에 든 무기 클래스 참조를 GC가 추적하게 한다(사망 화면이 읽는 동안 클래스가 내려가지 않게).
	UPROPERTY(Transient)
	FValorDeathInfo LastDeathInfo;

	bool bHasDeathInfo = false;

	bool bBuyMenuOpen = false;
	bool bAgentSelectOpen = false;

	// 캔버스 HUD 클릭 영역용 커서 모드(구매 메뉴·요원 선택 창이 같이 쓴다).
	void ApplyCursorMode(bool bCursor);

	double FlashStartTime = -1.0;
	float FlashSeconds = 0.0f;

	// 관전
	void CycleSpectate(int32 Direction);
	void SetSpectateTarget(class AValorCharacter* NewTarget);
	void StopSpectating();
	void UpdateSpectateCamera(float DeltaTime, bool bSnap);
	void GetSpectateCandidates(TArray<class AValorCharacter*>& OutCandidates) const;

	TWeakObjectPtr<class AValorCharacter> SpectateTarget;

	UPROPERTY(Transient)
	TObjectPtr<class ACameraActor> SpectateCamera;

	// 관전을 시작할 로컬 시각(죽은 뒤 DeathCamSeconds). 음수면 예약 없음.
	double SpectateStartTime = -1.0;

	double GetLocalTime() const;
	void ClearDeathFeedback();
};

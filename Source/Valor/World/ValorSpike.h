#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValorSpike.generated.h"

class AValorCharacter;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EValorSpikeState : uint8
{
	Carried,
	Dropped,
	Planted,
	Defused,
	Detonated
};

/**
 * 스파이크(라운드마다 하나, 서버가 스폰). 공격 팀 한 명이 들고 시작한다.
 *
 * 상태: 소지 → (소지자 사망) 떨어짐 → (공격 팀이 밟으면) 소지 → (지점 안에서 4 키 4초) 설치 → (수비가 4 키 7초) 해체 / (45초) 폭발.
 * 입력: 4 키를 누르고 있는 동안 이동 예측 플래그(UValorCharacterMovementComponent::WantsToUseSpike)가 켜진다.
 *   서버와 소유 클라가 같은 플래그 + 같은 복제 상태(스파이크 상태·위치·소지자, 지점 상자)로 "지금 쓸 수 있나"(CanBeUsedBy)를 계산해
 *   설치·해체 중에는 둘 다 이동을 0으로 묶는다(예측과 서버 결과가 같다). 진행은 서버 틱이 쌓고 복제한다(HUD 진행 막대).
 * 규칙 결과(설치 보너스, 라운드 종료)는 경기 게임 모드가 정한다. 이 액터는 상태와 진행만 관리한다.
 * 트레이드오프: 모델·이펙트·소리가 없다(엔진 기본 원기둥). 소지 중에는 숨긴다(발로란트는 등에 멘다).
 */
UCLASS()
class VALOR_API AValorSpike : public AActor
{
	GENERATED_BODY()

public:
	AValorSpike();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// === 서버 ===
	void GiveTo(AValorCharacter* NewCarrier);
	void DropAt(const FVector& Location);
	// 폭발: 반경 안의 살아 있는 캐릭터를 모두 죽인다.
	void Detonate(float RadiusCm);

	// === 모든 머신 ===
	EValorSpikeState GetState() const { return State; }
	AValorCharacter* GetCarrier() const { return Carrier; }
	AValorCharacter* GetDefuser() const { return Defuser; }
	float GetPlantProgress() const { return PlantProgress; }
	float GetDefuseProgress() const { return DefuseProgress; }
	float GetDefuseCheckpoint() const { return DefuseCheckpoint; }
	float GetPlantSeconds() const { return PlantSeconds; }
	float GetDefuseSeconds() const { return DefuseSeconds; }
	FName GetPlantedSiteName() const { return PlantedSiteName; }

	// 이 캐릭터가 지금 4 키로 스파이크를 쓸 수 있나(설치: 소지자 + 지점 안 + 땅 위, 해체: 설치됨 + 수비 + 가까움). 이동 고정 예측에 쓴다.
	bool CanBeUsedBy(const AValorCharacter& Character) const;

	// 이 캐릭터가 지금 스파이크를 쓰는 중인가(4 키를 누르고 있고 쓸 수 있다).
	static bool IsCharacterUsingSpike(const AValorCharacter& Character);

	static AValorSpike* FindSpike(const UWorld* World);

protected:
	UPROPERTY(EditDefaultsOnly, Category="Valor|Spike", meta=(ClampMin="0.1"))
	float PlantSeconds = 4.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Spike", meta=(ClampMin="0.1"))
	float DefuseSeconds = 7.0f;

	// 해체할 수 있는 거리(스파이크 중심에서 캐릭터 중심까지 수평).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Spike", meta=(ClampMin="10.0"))
	float DefuseRadiusCm = 150.0f;

	// 떨어진 스파이크를 공격 팀이 주울 거리.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Spike", meta=(ClampMin="10.0"))
	float PickupRadiusCm = 100.0f;

	UPROPERTY(VisibleAnywhere, Category="Valor|Spike")
	TObjectPtr<UStaticMeshComponent> Mesh;

private:
	void TickServer(float DeltaSeconds);
	void PlantAt(AValorCharacter& Planter);
	void RefreshVisibility();
	FVector FindGroundBelow(const FVector& Location) const;

	UFUNCTION()
	void OnRep_State();

	UPROPERTY(ReplicatedUsing=OnRep_State)
	EValorSpikeState State = EValorSpikeState::Carried;

	UPROPERTY(ReplicatedUsing=OnRep_State)
	TObjectPtr<AValorCharacter> Carrier;

	UPROPERTY(Replicated)
	TObjectPtr<AValorCharacter> Defuser;

	UPROPERTY(Replicated)
	float PlantProgress = 0.0f;

	UPROPERTY(Replicated)
	float DefuseProgress = 0.0f;

	UPROPERTY(Replicated)
	float DefuseCheckpoint = 0.0f;

	UPROPERTY(Replicated)
	FName PlantedSiteName;
};

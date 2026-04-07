#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ValorCharacterStateComponent.generated.h"

UENUM(BlueprintType)
enum class EValorMovementState : uint8
{
	Idle,
	Walking,
	Moving,
	Crouching,
	Jumping
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FValorMovementStateChangedSignature, EValorMovementState, EValorMovementState);

UCLASS(ClassGroup=(Valor), meta=(BlueprintSpawnableComponent))
class VALOR_API UValorCharacterStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UValorCharacterStateComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 서버가 최종 판정한 현재 이동 상태를 반환한다.
	EValorMovementState GetCurrentMovementState() const { return CurrentMovementState; }

	// 서버가 점프 요청을 승인했을 때 호출해 Jumping 상태 전환을 앞당긴다.
	void NotifyJumpRequested();

	// 서버에서 이동 상태를 다시 계산한다.
	void ServerEvaluateMovementState();

	FValorMovementStateChangedSignature OnMovementStateChanged;

protected:
	UPROPERTY(ReplicatedUsing=OnRep_CurrentMovementState, VisibleInstanceOnly, Category="Valor|State")
	EValorMovementState CurrentMovementState = EValorMovementState::Idle;

	UFUNCTION()
	void OnRep_CurrentMovementState(EValorMovementState PreviousMovementState);

private:
	void SetCurrentMovementState(EValorMovementState NewMovementState);
	EValorMovementState DetermineMovementState() const;

	// 점프 키가 눌린 직후에는 아직 Falling 판정이 아니어도 Jumping으로 유지하기 위한 서버 전용 플래그다.
	bool bServerJumpRequested = false;
};

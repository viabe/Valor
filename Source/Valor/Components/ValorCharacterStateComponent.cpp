#include "ValorCharacterStateComponent.h"

#include "../ValorCharacter.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

UValorCharacterStateComponent::UValorCharacterStateComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UValorCharacterStateComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UValorCharacterStateComponent, CurrentMovementState);
}

void UValorCharacterStateComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ServerEvaluateMovementState();
	}
}

void UValorCharacterStateComponent::NotifyJumpRequested()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	bServerJumpRequested = true;
	SetCurrentMovementState(EValorMovementState::Jumping);
}

void UValorCharacterStateComponent::ServerEvaluateMovementState()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (const UCharacterMovementComponent* MovementComponent = OwnerCharacter->GetCharacterMovement())
		{
			if (MovementComponent->IsMovingOnGround())
			{
				bServerJumpRequested = false;
			}
		}
	}

	SetCurrentMovementState(DetermineMovementState());
}

void UValorCharacterStateComponent::OnRep_CurrentMovementState(EValorMovementState PreviousMovementState)
{
	OnMovementStateChanged.Broadcast(PreviousMovementState, CurrentMovementState);
}

void UValorCharacterStateComponent::SetCurrentMovementState(EValorMovementState NewMovementState)
{
	if (CurrentMovementState == NewMovementState)
	{
		return;
	}

	const EValorMovementState PreviousMovementState = CurrentMovementState;
	CurrentMovementState = NewMovementState;
	OnMovementStateChanged.Broadcast(PreviousMovementState, CurrentMovementState);
}

EValorMovementState UValorCharacterStateComponent::DetermineMovementState() const
{
	const AValorCharacter* OwnerCharacter = Cast<AValorCharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		return EValorMovementState::Idle;
	}

	const UCharacterMovementComponent* MovementComponent = OwnerCharacter->GetCharacterMovement();
	if (!MovementComponent)
	{
		return EValorMovementState::Idle;
	}

	if (MovementComponent->IsFalling() || bServerJumpRequested)
	{
		return EValorMovementState::Jumping;
	}

	if (MovementComponent->IsCrouching())
	{
		return EValorMovementState::Crouching;
	}

	if (OwnerCharacter->GetVelocity().SizeSquared2D() > FMath::Square(5.0f))
	{
		if (OwnerCharacter->IsWalkInputActive())
		{
			return EValorMovementState::Walking;
		}

		return EValorMovementState::Moving;
	}

	return EValorMovementState::Idle;
}

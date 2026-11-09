#include "Components/ValorCharacterMovementComponent.h"

#include "Components/ValorCombatComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "ValorCharacter.h"

// =====================================================================================================
// 순수 계산
// =====================================================================================================

namespace ValorMovement
{
	uint8 QuantizeSpeedMultiplier(float Multiplier)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FMath::Clamp(Multiplier, 0.0f, 1.0f) * 255.0f), 1, 255));
	}

	float DequantizeSpeedMultiplier(uint8 Quantized)
	{
		return static_cast<float>(Quantized) / 255.0f;
	}

	float ComputeMaxGroundSpeed(float BaseRunSpeed, float EquipMultiplier, bool bWantsToWalk, bool bIsCrouching, float WalkSpeedRatio, float CrouchSpeedRatio)
	{
		const float RunSpeed = BaseRunSpeed * EquipMultiplier;
		if (bIsCrouching)
		{
			return RunSpeed * CrouchSpeedRatio;
		}

		return bWantsToWalk ? RunSpeed * WalkSpeedRatio : RunSpeed;
	}

	uint8 ResolveClaimedSpeedMultiplier(uint8 Claimed, uint8 ServerCurrent, uint8 ServerPrevious, double SecondsSinceServerChange, float GraceSeconds)
	{
		const bool bWithinGrace = SecondsSinceServerChange >= 0.0 && SecondsSinceServerChange <= GraceSeconds;
		const uint8 AllowedMax = bWithinGrace ? FMath::Max(ServerCurrent, ServerPrevious) : ServerCurrent;
		return FMath::Min(Claimed, AllowedMax);
	}
}

// =====================================================================================================
// 저장된 이동(소유 클라)
// =====================================================================================================

void FValorSavedMove::Clear()
{
	Super::Clear();

	bSavedWantsToWalk = false;
	SavedEquipSpeedQuantized = 255;
}

uint8 FValorSavedMove::GetCompressedFlags() const
{
	uint8 Flags = Super::GetCompressedFlags();
	if (bSavedWantsToWalk)
	{
		Flags |= FLAG_Custom_0;
	}

	return Flags;
}

bool FValorSavedMove::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	// 속도가 다른 두 이동을 하나로 합치면 서버가 한 속도로 계산해 클라 결과와 달라진다.
	const FValorSavedMove* NewValorMove = static_cast<const FValorSavedMove*>(NewMove.Get());
	if (bSavedWantsToWalk != NewValorMove->bSavedWantsToWalk || SavedEquipSpeedQuantized != NewValorMove->SavedEquipSpeedQuantized)
	{
		return false;
	}

	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

bool FValorSavedMove::IsImportantMove(const FSavedMovePtr& LastAckedMove) const
{
	// 속도 상태가 바뀐 이동은 유실되면 서버가 다른 속도로 계산하므로, 확인될 때까지 다음 패킷에 다시 실어 보낸다(엔진의 "중요한 이동").
	if (const FValorSavedMove* AckedValorMove = static_cast<const FValorSavedMove*>(LastAckedMove.Get()))
	{
		if (bSavedWantsToWalk != AckedValorMove->bSavedWantsToWalk || SavedEquipSpeedQuantized != AckedValorMove->SavedEquipSpeedQuantized)
		{
			return true;
		}
	}

	return Super::IsImportantMove(LastAckedMove);
}

void FValorSavedMove::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

	if (const UValorCharacterMovementComponent* MovementComponent = C ? Cast<UValorCharacterMovementComponent>(C->GetCharacterMovement()) : nullptr)
	{
		bSavedWantsToWalk = MovementComponent->bWantsToWalk;
		SavedEquipSpeedQuantized = MovementComponent->EquipSpeedQuantized;
	}
}

void FValorSavedMove::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);

	// 보정 후 재시뮬레이션: 그때 쓴 속도를 그대로 복원한다(지금 상태가 아니라). 걷기는 압축 플래그로 복원된다.
	if (UValorCharacterMovementComponent* MovementComponent = C ? Cast<UValorCharacterMovementComponent>(C->GetCharacterMovement()) : nullptr)
	{
		MovementComponent->EquipSpeedQuantized = SavedEquipSpeedQuantized;
	}
}

FValorNetworkPredictionData_Client::FValorNetworkPredictionData_Client(const UCharacterMovementComponent& ClientMovement)
	: Super(ClientMovement)
{
}

FSavedMovePtr FValorNetworkPredictionData_Client::AllocateNewMove()
{
	return FSavedMovePtr(new FValorSavedMove());
}

// =====================================================================================================
// 이동 RPC 데이터
// =====================================================================================================

void FValorCharacterNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	Super::ClientFillNetworkMoveData(ClientMove, MoveType);

	EquipSpeedQuantized = static_cast<const FValorSavedMove&>(ClientMove).SavedEquipSpeedQuantized;
}

bool FValorCharacterNetworkMoveData::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	Super::Serialize(CharacterMovement, Ar, PackageMap, MoveType);

	// 대부분의 시간(칼, 또는 같은 총)에는 값이 같지만 이동 하나당 1바이트라 항상 보낸다(조건 비트를 두는 것보다 단순하다).
	Ar << EquipSpeedQuantized;
	return !Ar.IsError();
}

FValorCharacterNetworkMoveDataContainer::FValorCharacterNetworkMoveDataContainer()
{
	NewMoveData = &ValorMoveData[0];
	PendingMoveData = &ValorMoveData[1];
	OldMoveData = &ValorMoveData[2];
}

// =====================================================================================================
// 이동 컴포넌트
// =====================================================================================================

UValorCharacterMovementComponent::UValorCharacterMovementComponent()
{
	SetNetworkMoveDataContainer(ValorMoveDataContainer);

	// 엔진 기본 속도 값도 맞춰 둔다(GetMaxSpeed를 거치지 않고 MaxWalkSpeed를 직접 읽는 코드·애니메이션을 위해). 실제 제한은 GetMaxSpeed가 한다.
	MaxWalkSpeed = BaseRunSpeed;
	MaxWalkSpeedCrouched = BaseRunSpeed * CrouchSpeedRatio;
}

void UValorCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// 이번 틱의 이동을 만들기 전에 배율을 정한다. 원격 소유 클라의 이동을 받는 서버만은 이동 데이터(MoveAutonomous)에서 정한다.
	const bool bServerSimulatingRemoteClient = CharacterOwner && CharacterOwner->HasAuthority() && !CharacterOwner->IsLocallyControlled();
	if (!bServerSimulatingRemoteClient)
	{
		RefreshEquipSpeedFromCombatState();
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

float UValorCharacterMovementComponent::GetMaxSpeed() const
{
	const float EquipMultiplier = ValorMovement::DequantizeSpeedMultiplier(EquipSpeedQuantized);

	switch (MovementMode)
	{
	case MOVE_Walking:
	case MOVE_NavWalking:
		return ValorMovement::ComputeMaxGroundSpeed(BaseRunSpeed, EquipMultiplier, bWantsToWalk, IsCrouching(), WalkSpeedRatio, CrouchSpeedRatio);

	case MOVE_Falling:
		// 공중에서는 앉기 비율을 쓰지 않는다(점프 중 웅크려도 공중 속도가 줄지 않는다). 걷기·무기 배율은 유지된다.
		return ValorMovement::ComputeMaxGroundSpeed(BaseRunSpeed, EquipMultiplier, bWantsToWalk, false, WalkSpeedRatio, CrouchSpeedRatio);

	default:
		return Super::GetMaxSpeed();
	}
}

void UValorCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	bWantsToWalk = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

FNetworkPredictionData_Client* UValorCharacterMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		UValorCharacterMovementComponent* MutableThis = const_cast<UValorCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FValorNetworkPredictionData_Client(*this);
	}

	return ClientPredictionData;
}

void UValorCharacterMovementComponent::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel)
{
	// 서버(원격 클라의 이동 재시뮬레이션)와 클라(보정 후 재시뮬레이션) 모두, 처리 중인 이동 데이터가 그 이동에 쓴 배율을 들고 있다.
	if (const FValorCharacterNetworkMoveData* MoveData = static_cast<const FValorCharacterNetworkMoveData*>(GetCurrentNetworkMoveData()))
	{
		if (CharacterOwner && CharacterOwner->HasAuthority())
		{
			UpdateServerSpeedHistory();
			const UWorld* World = GetWorld();
			const double SecondsSinceChange = World ? World->GetTimeSeconds() - ServerSpeedChangeTime : 0.0;
			EquipSpeedQuantized = ValorMovement::ResolveClaimedSpeedMultiplier(MoveData->EquipSpeedQuantized, ServerAuthoritativeSpeed, ServerPreviousSpeed, SecondsSinceChange, SpeedChangeGraceSeconds);
		}
		else
		{
			EquipSpeedQuantized = MoveData->EquipSpeedQuantized;
		}
	}

	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

UValorCombatComponent* UValorCharacterMovementComponent::GetCombatComponent() const
{
	const AValorCharacter* ValorCharacter = Cast<AValorCharacter>(CharacterOwner);
	return ValorCharacter ? ValorCharacter->GetCombatComponent() : nullptr;
}

void UValorCharacterMovementComponent::RefreshEquipSpeedFromCombatState()
{
	const UValorCombatComponent* CombatComponent = GetCombatComponent();
	EquipSpeedQuantized = ValorMovement::QuantizeSpeedMultiplier(CombatComponent ? CombatComponent->GetEquipMoveSpeedMultiplier() : 1.0f);
}

void UValorCharacterMovementComponent::UpdateServerSpeedHistory()
{
	const UValorCombatComponent* CombatComponent = GetCombatComponent();
	const uint8 CurrentSpeed = ValorMovement::QuantizeSpeedMultiplier(CombatComponent ? CombatComponent->GetEquipMoveSpeedMultiplier() : 1.0f);
	if (CurrentSpeed == ServerAuthoritativeSpeed)
	{
		return;
	}

	ServerPreviousSpeed = ServerAuthoritativeSpeed;
	ServerAuthoritativeSpeed = CurrentSpeed;

	const UWorld* World = GetWorld();
	ServerSpeedChangeTime = World ? World->GetTimeSeconds() : 0.0;
}

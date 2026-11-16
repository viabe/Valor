#include "Components/ValorCharacterMovementComponent.h"
#include "ValorGameState.h"
#include "ValorCharacter.h"
#include "World/ValorSpike.h"

#include "Components/ValorCombatComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerState.h"
#include "ValorCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogValorMovement, Log, All);

namespace
{
#if !UE_BUILD_SHIPPING
	// 개발 확인용(Valor.Tools.TagNet): 소유 클라가 피격 감속을 이동 예측에 넣지 않는다 = 감속을 무시하는 조작 흉내. 서버가 잘라 보정하는지 본다.
	TAutoConsoleVariable<int32> CVarValorIgnoreTagPrediction(
		TEXT("Valor.Debug.IgnoreTagPrediction"),
		0,
		TEXT("개발 확인용: 1이면 소유 클라가 피격 감속을 이동 예측에 넣지 않는다(서버 검증 확인)."),
		ECVF_Cheat);
#endif
}

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
	bSavedWantsToUseSpike = false;
	SavedEquipSpeedQuantized = 255;
	SavedTagAckSequence = 0;
}

uint8 FValorSavedMove::GetCompressedFlags() const
{
	uint8 Flags = Super::GetCompressedFlags();
	if (bSavedWantsToWalk)
	{
		Flags |= FLAG_Custom_0;
	}
	if (bSavedWantsToUseSpike)
	{
		Flags |= FLAG_Custom_1;
	}

	return Flags;
}

bool FValorSavedMove::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	// 속도가 다른 두 이동을 하나로 합치면 서버가 한 속도로 계산해 클라 결과와 달라진다.
	const FValorSavedMove* NewValorMove = static_cast<const FValorSavedMove*>(NewMove.Get());
	// 감속 순번이 바뀐 이동도 합치지 않는다: 서버가 그 이동의 시각으로 감속 시계를 시작해야 클라와 같다.
	if (bSavedWantsToWalk != NewValorMove->bSavedWantsToWalk || bSavedWantsToUseSpike != NewValorMove->bSavedWantsToUseSpike || SavedEquipSpeedQuantized != NewValorMove->SavedEquipSpeedQuantized
		|| SavedTagAckSequence != NewValorMove->SavedTagAckSequence)
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
		if (bSavedWantsToWalk != AckedValorMove->bSavedWantsToWalk || bSavedWantsToUseSpike != AckedValorMove->bSavedWantsToUseSpike || SavedEquipSpeedQuantized != AckedValorMove->SavedEquipSpeedQuantized
			|| SavedTagAckSequence != AckedValorMove->SavedTagAckSequence)
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
		bSavedWantsToUseSpike = MovementComponent->bWantsToUseSpike;
		SavedEquipSpeedQuantized = MovementComponent->EquipSpeedQuantized;
		SavedTagAckSequence = MovementComponent->TagMoveClock.Sequence;
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
	TagAckSequence = static_cast<const FValorSavedMove&>(ClientMove).SavedTagAckSequence;
}

bool FValorCharacterNetworkMoveData::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	Super::Serialize(CharacterMovement, Ar, PackageMap, MoveType);

	// 대부분의 시간(칼, 또는 같은 총)에는 값이 같지만 이동 하나당 1바이트라 항상 보낸다(조건 비트를 두는 것보다 단순하다).
	Ar << EquipSpeedQuantized;
	Ar << TagAckSequence;
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
	const bool bPredictingClient = CharacterOwner && !CharacterOwner->HasAuthority() && CharacterOwner->IsLocallyControlled();
	if (bPredictingClient)
	{
		// 새 감속 순번을 처음 보는 이동에서 감속 시계를 시작한다(그 이동에 받은 순번이 실려 서버도 같은 시각에 시작한다).
		LastPredictedMoveTime = PredictNextMoveTime(DeltaTime);
		const AValorCharacter* ValorOwner = Cast<AValorCharacter>(CharacterOwner);
		const FValorTagState* State = ValorOwner ? &ValorOwner->GetTagState() : nullptr;
		bool bAcknowledge = State && State->Sequence != TagMoveClock.Sequence;
#if !UE_BUILD_SHIPPING
		// 감속 무시 조작 흉내: 받지도, 적용하지도 않는다.
		bAcknowledge &= CVarValorIgnoreTagPrediction.GetValueOnGameThread() <= 0;
#endif
		if (bAcknowledge)
		{
			TagMoveClock = ValorTagging::AcknowledgeOnMoveClock(TagMoveClock, *State, LastPredictedMoveTime, Tagging);
		}
	}
	if (!bServerSimulatingRemoteClient)
	{
		RefreshEquipSpeedFromCombatState();
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

float UValorCharacterMovementComponent::GetMaxSpeed() const
{
	// 구매 단계(게임 모드가 켠 경우만, 기본 끔): 수평 이동을 묶는다. 복제된 GameState 단계를 서버와 소유 클라가 같이 보므로 이동 예측이 맞는다
	// (단계가 바뀌는 순간의 몇 프레임 차이는 보정으로 끝난다).
	const UWorld* World = GetWorld();
	const AValorGameState* ValorGameState = World ? World->GetGameState<AValorGameState>() : nullptr;
	if (ValorGameState && ValorGameState->IsMovementLocked() && (MovementMode == MOVE_Walking || MovementMode == MOVE_NavWalking || MovementMode == MOVE_Falling))
	{
		return 0.0f;
	}

	// 스파이크 설치·해체 중에는 제자리에 묶인다(발로란트). 4 키 플래그 + 복제된 스파이크 상태로 서버와 소유 클라가 같이 판단한다.
	const AValorCharacter* ValorOwner = Cast<AValorCharacter>(CharacterOwner);
	if (bWantsToUseSpike && ValorOwner && AValorSpike::IsCharacterUsingSpike(*ValorOwner))
	{
		return 0.0f;
	}

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
	bWantsToUseSpike = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
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
			const double ServerNow = World ? World->GetTimeSeconds() : 0.0;
			const double SecondsSinceChange = ServerNow - ServerSpeedChangeTime;

			// 허용 최대 = 장비 배율 허용치 × 감속 배율 허용치. 클라 주장은 이 값으로 자른다(더 느린 주장은 그대로).
			const uint8 AllowedEquip = ValorMovement::ResolveClaimedSpeedMultiplier(255, ServerAuthoritativeSpeed, ServerPreviousSpeed, SecondsSinceChange, SpeedChangeGraceSeconds);
			const float AllowedTag = ResolveServerTagForMove(ClientTimeStamp, MoveData->TagAckSequence, ServerNow);
			const uint8 AllowedMax = ValorMovement::QuantizeSpeedMultiplier(ValorMovement::DequantizeSpeedMultiplier(AllowedEquip) * AllowedTag);
			if (MoveData->EquipSpeedQuantized > AllowedMax)
			{
				UE_LOG(LogValorMovement, Verbose, TEXT("[이동 검증] %s 속도 주장 잘림: 주장 %d > 허용 %d(장비 %d × 감속 %.3f), 이동 시각 %.3f, 감속 시계 시작 %.3f(순번 %d, 클라 받음 %d)"),
					*GetNameSafe(CharacterOwner), MoveData->EquipSpeedQuantized, AllowedMax, AllowedEquip, AllowedTag, ClientTimeStamp, TagMoveClock.StartMoveTime,
					TagMoveClock.Sequence, MoveData->TagAckSequence);
			}
			EquipSpeedQuantized = FMath::Min(MoveData->EquipSpeedQuantized, AllowedMax);
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
	const float EquipMultiplier = CombatComponent ? CombatComponent->GetEquipMoveSpeedMultiplier() : 1.0f;
	EquipSpeedQuantized = ValorMovement::QuantizeSpeedMultiplier(EquipMultiplier * GetTagSpeedMultiplier());
}

float UValorCharacterMovementComponent::PredictNextMoveTime(float DeltaTime) const
{
	// 엔진(FNetworkPredictionData_Client_Character::UpdateTimeStampAndDeltaTime)과 같은 순서: 일정 시간마다 되돌린 뒤 이번 틱 시간을 더한다.
	FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	if (!ClientData)
	{
		return 0.0f;
	}
	float TimeStamp = ClientData->CurrentTimeStamp;
	if (TimeStamp > MinTimeBetweenTimeStampResets)
	{
		TimeStamp -= MinTimeBetweenTimeStampResets;
	}
	return TimeStamp + DeltaTime;
}

float UValorCharacterMovementComponent::ResolveServerTagForMove(float ClientTimeStamp, uint8 ClientAckSequence, double ServerNow)
{
	const AValorCharacter* ValorOwner = Cast<AValorCharacter>(CharacterOwner);
	if (!ValorOwner)
	{
		return 1.0f;
	}

	const FValorTagState& State = ValorOwner->GetTagState();
	if (State.Sequence != ServerSeenTagSequence)
	{
		ServerSeenTagSequence = State.Sequence;
		ServerTagChangeTime = ServerNow;
	}

	if (TagMoveClock.Sequence != State.Sequence)
	{
		// 클라가 이 이동에서 받았다(정상) 또는 너무 오래 받지 않았다(강제): 이 이동의 시각으로 시계를 시작한다.
		const bool bClientAcknowledged = ClientAckSequence == State.Sequence;
		const bool bAckOverdue = ServerNow - ServerTagChangeTime > TagAckMaxDelaySeconds;
		if (bClientAcknowledged || bAckOverdue)
		{
			if (!bClientAcknowledged)
			{
				UE_LOG(LogValorMovement, Verbose, TEXT("[이동 검증] %s 감속을 %.2f초 안에 받지 않아 강제로 시작(이동 시각 %.3f)"), *GetNameSafe(CharacterOwner), TagAckMaxDelaySeconds, ClientTimeStamp);
			}
			TagMoveClock = ValorTagging::AcknowledgeOnMoveClock(TagMoveClock, State, ClientTimeStamp, Tagging);
		}
	}

	return ValorTagging::ComputeAllowedMoveClockMultiplier(TagMoveClock, ClientTimeStamp, TagMoveTimeToleranceSeconds, Tagging);
}

float UValorCharacterMovementComponent::GetTagSpeedMultiplier() const
{
	const AValorCharacter* ValorOwner = Cast<AValorCharacter>(CharacterOwner);
	const UWorld* World = GetWorld();
	if (!ValorOwner || !World)
	{
		return 1.0f;
	}

	// 소유 클라(이동을 예측하는 원격 클라): 이동 시계.
	if (!ValorOwner->HasAuthority())
	{
		return ValorTagging::ComputeMoveClockMultiplier(TagMoveClock, LastPredictedMoveTime, Tagging);
	}

	// 서버가 직접 움직이는 캐릭터(리슨 호스트 자신, 봇): 서버 시간.
	return ValorTagging::ComputeSpeedMultiplier(ValorOwner->GetTagState(), World->GetTimeSeconds(), Tagging);
}

void UValorCharacterMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation, FVector NewVelocity,
	UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection)
{
	++ClientCorrectionCount;
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	UE_LOG(LogValorMovement, Verbose, TEXT("[이동 보정] %s 보정 받음: 이동 시각 %.3f, 서버 시간 추정 %.3f, 감속 배율 %.3f, 속도 배율 %d, 위치 차이 %.1fcm"),
		*GetNameSafe(CharacterOwner), TimeStamp, GameState ? GameState->GetServerWorldTimeSeconds() : -1.0, GetTagSpeedMultiplier(), EquipSpeedQuantized,
		CharacterOwner ? FVector::Dist(CharacterOwner->GetActorLocation(), NewLocation) : -1.0f);
	Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, NewBase, NewBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode, ServerGravityDirection);
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

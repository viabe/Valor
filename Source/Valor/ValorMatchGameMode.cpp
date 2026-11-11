#include "ValorMatchGameMode.h"

#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ValorCombatComponent.h"
#include "Engine/PlayerStartPIE.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Player/ValorPlayerController.h"
#include "Player/ValorPlayerState.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "ValorCharacter.h"
#include "ValorGameState.h"
#include "Weapons/ValorWeaponBase.h"
#include "World/ValorWeaponPickup.h"

DEFINE_LOG_CATEGORY_STATIC(LogValorMatch, Log, All);

namespace ValorMatchEconomy
{
	// 경제 초기화 라운드의 시작 크레딧: 상점에 권총이 없으면 가장 싼 총 값까지 올린다(권총이 들어오면 저절로 StartingCredits).
	int32 GetEconomyResetCredits(const AValorGameState* ValorGameState, int32 StartingCredits)
	{
		if (!ValorGameState)
		{
			return StartingCredits;
		}

		bool bHasSidearms = false;
		int32 CheapestCost = 0;
		for (const FValorShopWeaponEntry& Entry : ValorGameState->GetShopWeapons())
		{
			bHasSidearms |= Entry.Category == EValorWeaponCategory::Sidearm;
			CheapestCost = CheapestCost > 0 ? FMath::Min(CheapestCost, Entry.Cost) : Entry.Cost;
		}

		return ValorMatchRules::ComputeEconomyResetCredits(StartingCredits, bHasSidearms, CheapestCost);
	}
}

AValorMatchGameMode::AValorMatchGameMode()
{
	// 데스매치 BP(BP_ValorGameMode)와 같은 캐릭터 BP를 쓴다(메시·애님이 연결된 캐릭터). 없으면 C++ 캐릭터로 남는다.
	static ConstructorHelpers::FClassFinder<APawn> CharacterBlueprint(TEXT("/Game/Valor/Character/BP_ValorCharacterBase"));
	if (CharacterBlueprint.Succeeded())
	{
		DefaultPawnClass = CharacterBlueprint.Class;
	}

	// 경기 모드는 사서 쓴다: 주무기·실드 기본 지급 없음(칼, 그리고 권총 BP가 생기면 클래식만).
	StartingWeaponClass = nullptr;
	SpawnShield = 0.0f;
}

void AValorMatchGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	auto ReadSeconds = [&Options](const TCHAR* Key, float& InOutSeconds)
	{
		const FString Value = UGameplayStatics::ParseOption(Options, Key);
		if (!Value.IsEmpty())
		{
			InOutSeconds = FMath::Max(FCString::Atof(*Value), 0.1f);
		}
	};
	ReadSeconds(TEXT("PreMatchTime"), PreMatchDelaySeconds);
	ReadSeconds(TEXT("BuyTime"), BuyPhaseSeconds);
	ReadSeconds(TEXT("RoundTime"), RoundSeconds);
	ReadSeconds(TEXT("RoundEndTime"), RoundEndSeconds);
	ReadSeconds(TEXT("MatchEndTime"), MatchEndSeconds);
}

void AValorMatchGameMode::InitGameState()
{
	Super::InitGameState();

	if (AValorGameState* ValorGameState = GetValorGameState())
	{
		ValorGameState->SetUsesRounds(true);
		ValorGameState->SetFreezeDuringBuyPhase(bFreezePlayersDuringBuyPhase);
		ValorGameState->SetBlockFireDuringBuyPhase(bBlockFireDuringBuyPhase);
		ValorGameState->SetRoundPhase(EValorRoundPhase::WaitingToStart, 0.0f);
	}
}

AValorGameState* AValorMatchGameMode::GetValorGameState() const
{
	return GetGameState<AValorGameState>();
}

void AValorMatchGameMode::GetMatchPlayers(TArray<AController*>& OutControllers) const
{
	OutControllers.Reset();
	for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
	{
		AController* Controller = It->Get();
		const AValorPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AValorPlayerState>() : nullptr;
		if (PlayerState && PlayerState->GetTeam() != EValorTeam::None)
		{
			OutControllers.Add(Controller);
		}
	}
}

int32 AValorMatchGameMode::CountPlayers(EValorTeam Team) const
{
	TArray<AController*> Controllers;
	GetMatchPlayers(Controllers);
	return Controllers.FilterByPredicate([Team](const AController* Controller) { return ValorTeams::GetTeamOf(Controller) == Team; }).Num();
}

int32 AValorMatchGameMode::CountAlivePlayers(EValorTeam Team) const
{
	TArray<AController*> Controllers;
	GetMatchPlayers(Controllers);
	int32 Alive = 0;
	for (const AController* Controller : Controllers)
	{
		const AValorCharacter* Character = Cast<AValorCharacter>(Controller->GetPawn());
		if (ValorTeams::GetTeamOf(Controller) == Team && Character && Character->IsAlive())
		{
			++Alive;
		}
	}
	return Alive;
}

EValorTeam AValorMatchGameMode::PickTeamForNewPlayer(int32 CountA, int32 CountB)
{
	return CountB < CountA ? EValorTeam::TeamB : EValorTeam::TeamA;
}

// ======================================================================================
// 접속
// ======================================================================================

void AValorMatchGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	AValorPlayerState* PlayerState = NewPlayer ? NewPlayer->GetPlayerState<AValorPlayerState>() : nullptr;
	if (!PlayerState)
	{
		return;
	}

	const EValorTeam Team = PickTeamForNewPlayer(CountPlayers(EValorTeam::TeamA), CountPlayers(EValorTeam::TeamB));
	PlayerState->SetTeam(Team);
	PlayerState->SetCredits(RoundNumber > 0 && ValorMatchRules::IsOvertimeRound(RoundNumber, RoundsPerHalf) ? OvertimeCredits : ValorMatchEconomy::GetEconomyResetCredits(GetValorGameState(), StartingCredits));
	UE_LOG(LogValorMatch, Log, TEXT("%s 접속 → %s"), *PlayerState->GetPlayerName(), Team == EValorTeam::TeamA ? TEXT("TeamA") : TEXT("TeamB"));

	// 첫 플레이어가 들어오면 잠시(다른 접속을 기다리는 시간) 뒤 첫 라운드를 시작한다.
	if (!bMatchScheduled)
	{
		bMatchScheduled = true;
		SetPhase(EValorRoundPhase::WaitingToStart, FMath::Max(PreMatchDelaySeconds, 0.1f));
	}

	// 대기·구매 단계면 바로 팀 스폰에 세우고, 교전 중이면 다음 라운드까지 기다린다(관전).
	if (CurrentPhase == EValorRoundPhase::WaitingToStart || CurrentPhase == EValorRoundPhase::BuyPhase)
	{
		RebuildSpawnBases();
		const AValorGameState* ValorGameState = GetValorGameState();
		const EValorTeamSide Side = ValorGameState ? ValorGameState->GetSideOfTeam(Team) : EValorTeamSide::Attack;
		SpawnForRound(NewPlayer, CountPlayers(Team) - 1, false);
		UE_LOG(LogValorMatch, Log, TEXT("  %s 스폰에 세움"), Side == EValorTeamSide::Attack ? TEXT("공격") : TEXT("수비"));
	}
}

bool AValorMatchGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	// 부활은 라운드 시작에만 한다(관전 중 클릭으로 RestartPlayer가 불리는 엔진 기본 경로를 막는다).
	return false;
}

void AValorMatchGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);

	// 나간 사람의 팀이 그 사람만 남아 있었다면 전멸로 끝난다. 나간 컨트롤러는 다음 프레임에 목록에서 빠지므로 바로 세지 않는다.
	if (AValorPlayerState* PlayerState = Exiting ? Exiting->GetPlayerState<AValorPlayerState>() : nullptr)
	{
		PlayerState->SetTeam(EValorTeam::None);
	}

	if (CurrentPhase == EValorRoundPhase::Combat)
	{
		CheckElimination();
	}
}

// ======================================================================================
// 라운드 상태 머신
// ======================================================================================

void AValorMatchGameMode::SetPhase(EValorRoundPhase NewPhase, float DurationSeconds)
{
	CurrentPhase = NewPhase;
	if (AValorGameState* ValorGameState = GetValorGameState())
	{
		ValorGameState->SetRoundPhase(NewPhase, DurationSeconds);
	}

	FTimerManager& TimerManager = GetWorldTimerManager();
	TimerManager.ClearTimer(PhaseTimerHandle);
	if (DurationSeconds > 0.0f)
	{
		TimerManager.SetTimer(PhaseTimerHandle, this, &AValorMatchGameMode::HandlePhaseTimerExpired, DurationSeconds, false);
	}
}

void AValorMatchGameMode::DebugSkipPhase()
{
	GetWorldTimerManager().ClearTimer(PhaseTimerHandle);
	HandlePhaseTimerExpired();
}

void AValorMatchGameMode::HandlePhaseTimerExpired()
{
	switch (CurrentPhase)
	{
	case EValorRoundPhase::WaitingToStart:
	case EValorRoundPhase::RoundEnd:
		BeginRound();
		break;

	case EValorRoundPhase::BuyPhase:
		StartCombatPhase();
		break;

	case EValorRoundPhase::Combat:
	{
		// 시간 종료: 수비 승리(스파이크가 설치되지 않았다).
		const AValorGameState* ValorGameState = GetValorGameState();
		const EValorTeam AttackingTeam = ValorGameState ? ValorGameState->GetAttackingTeam() : EValorTeam::TeamA;
		EndRound(ValorTeams::GetOpposingTeam(AttackingTeam), EValorRoundEndReason::TimeExpired);
		break;
	}

	case EValorRoundPhase::MatchEnd:
		ResetMatch();
		break;

	default:
		break;
	}
}

void AValorMatchGameMode::BeginRound()
{
	AValorGameState* ValorGameState = GetValorGameState();
	if (!ValorGameState)
	{
		return;
	}

	++RoundNumber;
	const bool bEconomyReset = ValorMatchRules::IsEconomyResetRound(RoundNumber, RoundsPerHalf);
	const bool bOvertime = ValorMatchRules::IsOvertimeRound(RoundNumber, RoundsPerHalf);
	const EValorTeam AttackingTeam = ValorMatchRules::GetAttackingTeam(RoundNumber, RoundsPerHalf);
	ValorGameState->SetRoundNumber(RoundNumber, AttackingTeam);
	ValorGameState->SetRoundResult(EValorTeam::None, EValorRoundEndReason::None);

	if (bEconomyReset)
	{
		LossStreakA = 0;
		LossStreakB = 0;
	}

	// 바닥의 총 정리(떨어진 총 + 맵에 놓인 총).
	if (bRemoveWeaponPickupsEachRound)
	{
		for (TActorIterator<AValorWeaponPickup> It(GetWorld()); It; ++It)
		{
			It->Destroy();
		}
	}

	// 컨트롤러가 없는 시체(나간 사람 등)를 치운다. 컨트롤러가 있는 캐릭터는 아래 SpawnForRound가 새로 스폰하며 치운다.
	for (TActorIterator<AValorCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->GetController() && !It->IsAlive())
		{
			It->Destroy();
		}
	}

	RebuildSpawnBases();

	TArray<AController*> Controllers;
	GetMatchPlayers(Controllers);
	int32 AttackIndex = 0;
	int32 DefenseIndex = 0;
	for (AController* Controller : Controllers)
	{
		AValorPlayerState* PlayerState = Controller->GetPlayerState<AValorPlayerState>();
		PlayerState->ClearRoundPurchases();
		if (bEconomyReset)
		{
			PlayerState->SetCredits(ValorMatchEconomy::GetEconomyResetCredits(GetValorGameState(), StartingCredits));
		}
		else if (bOvertime)
		{
			PlayerState->SetCredits(OvertimeCredits);
		}

		const bool bAttacker = PlayerState->GetTeam() == AttackingTeam;
		SpawnForRound(Controller, bAttacker ? AttackIndex++ : DefenseIndex++, !bEconomyReset);
	}

	bTeamAHadPlayers = CountPlayers(EValorTeam::TeamA) > 0;
	bTeamBHadPlayers = CountPlayers(EValorTeam::TeamB) > 0;

	UE_LOG(LogValorMatch, Log, TEXT("라운드 %d 시작: 공격 %s, 점수 %d:%d%s%s"), RoundNumber, AttackingTeam == EValorTeam::TeamA ? TEXT("TeamA") : TEXT("TeamB"),
		ScoreA, ScoreB, bEconomyReset ? TEXT(", 경제 초기화") : TEXT(""), bOvertime ? TEXT(", 연장전") : TEXT(""));
	SetPhase(EValorRoundPhase::BuyPhase, BuyPhaseSeconds);
}

void AValorMatchGameMode::StartCombatPhase()
{
	SetPhase(EValorRoundPhase::Combat, RoundSeconds);
	UE_LOG(LogValorMatch, Log, TEXT("라운드 %d 교전 시작"), RoundNumber);
	CheckElimination();
}

void AValorMatchGameMode::CheckElimination()
{
	if (CurrentPhase != EValorRoundPhase::Combat)
	{
		return;
	}

	// 라운드 시작에 사람이 있던 팀만 전멸로 본다(혼자 연습하면 시간 종료로 끝난다).
	const bool bAEliminated = bTeamAHadPlayers && CountAlivePlayers(EValorTeam::TeamA) == 0;
	const bool bBEliminated = bTeamBHadPlayers && CountAlivePlayers(EValorTeam::TeamB) == 0;
	if (bAEliminated)
	{
		EndRound(EValorTeam::TeamB, EValorRoundEndReason::Elimination);
	}
	else if (bBEliminated)
	{
		EndRound(EValorTeam::TeamA, EValorRoundEndReason::Elimination);
	}
}

void AValorMatchGameMode::EndRound(EValorTeam Winner, EValorRoundEndReason Reason)
{
	AValorGameState* ValorGameState = GetValorGameState();
	if (!ValorGameState || CurrentPhase != EValorRoundPhase::Combat || Winner == EValorTeam::None)
	{
		return;
	}

	(Winner == EValorTeam::TeamA ? ScoreA : ScoreB) += 1;
	const EValorTeam Loser = ValorTeams::GetOpposingTeam(Winner);
	int32& LoserStreak = Loser == EValorTeam::TeamA ? LossStreakA : LossStreakB;
	int32& WinnerStreak = Winner == EValorTeam::TeamA ? LossStreakA : LossStreakB;
	++LoserStreak;
	WinnerStreak = 0;

	// 라운드 보상: 이긴 팀 3000, 진 팀은 연패 보너스.
	TArray<AController*> Controllers;
	GetMatchPlayers(Controllers);
	for (AController* Controller : Controllers)
	{
		AValorPlayerState* PlayerState = Controller->GetPlayerState<AValorPlayerState>();
		const bool bWon = PlayerState->GetTeam() == Winner;
		PlayerState->AddCredits(ValorMatchRules::ComputeRoundReward(bWon, bWon ? 0 : LoserStreak));
	}

	ValorGameState->SetTeamScores(ScoreA, ScoreB);
	ValorGameState->SetRoundResult(Winner, Reason);
	UE_LOG(LogValorMatch, Log, TEXT("라운드 %d 종료: %s 승리(%s), 점수 %d:%d"), RoundNumber, Winner == EValorTeam::TeamA ? TEXT("TeamA") : TEXT("TeamB"),
		Reason == EValorRoundEndReason::Elimination ? TEXT("전멸") : TEXT("시간 종료"), ScoreA, ScoreB);

	const EValorTeam MatchWinner = ValorMatchRules::GetMatchWinner(ScoreA, ScoreB, RoundsToWin);
	if (MatchWinner != EValorTeam::None)
	{
		ValorGameState->SetMatchWinner(MatchWinner);
		UE_LOG(LogValorMatch, Log, TEXT("경기 종료: %s 승리 %d:%d"), MatchWinner == EValorTeam::TeamA ? TEXT("TeamA") : TEXT("TeamB"), ScoreA, ScoreB);
		SetPhase(EValorRoundPhase::MatchEnd, MatchEndSeconds);
		return;
	}

	SetPhase(EValorRoundPhase::RoundEnd, RoundEndSeconds);
}

void AValorMatchGameMode::ResetMatch()
{
	RoundNumber = 0;
	ScoreA = 0;
	ScoreB = 0;
	LossStreakA = 0;
	LossStreakB = 0;

	if (AValorGameState* ValorGameState = GetValorGameState())
	{
		ValorGameState->SetTeamScores(0, 0);
		ValorGameState->SetMatchWinner(EValorTeam::None);
	}

	TArray<AController*> Controllers;
	GetMatchPlayers(Controllers);
	for (AController* Controller : Controllers)
	{
		Controller->GetPlayerState<AValorPlayerState>()->ResetScore();
	}

	BeginRound();
}

void AValorMatchGameMode::OnCharacterKilled(AValorCharacter* Victim, AController* VictimController, AValorPlayerState* VictimState, AValorPlayerState* KillerState, bool bSelfKill)
{
	// 적 처치 보상 200. 아군 사격은 막혀 있지만 규칙상으로도 아군·자기 처치는 보상하지 않는다.
	if (KillerState && VictimState && !bSelfKill && KillerState->GetTeam() != EValorTeam::None && KillerState->GetTeam() != VictimState->GetTeam())
	{
		KillerState->AddCredits(KillReward);
	}

	CheckElimination();
}

// ======================================================================================
// 스폰
// ======================================================================================

void AValorMatchGameMode::SplitSpawnGroups(const TArray<FVector>& Locations, TArray<int32>& OutGroups)
{
	OutGroups.Init(0, Locations.Num());
	if (Locations.Num() < 2)
	{
		return;
	}

	int32 SeedA = 0;
	int32 SeedB = 1;
	double FarthestDistSq = -1.0;
	for (int32 I = 0; I < Locations.Num(); ++I)
	{
		for (int32 J = I + 1; J < Locations.Num(); ++J)
		{
			const double DistSq = FVector::DistSquared(Locations[I], Locations[J]);
			if (DistSq > FarthestDistSq)
			{
				FarthestDistSq = DistSq;
				SeedA = I;
				SeedB = J;
			}
		}
	}

	for (int32 Index = 0; Index < Locations.Num(); ++Index)
	{
		OutGroups[Index] = FVector::DistSquared(Locations[Index], Locations[SeedB]) < FVector::DistSquared(Locations[Index], Locations[SeedA]) ? 1 : 0;
	}
}

void AValorMatchGameMode::RebuildSpawnBases()
{
	AttackSpawnBases.Reset();
	DefenseSpawnBases.Reset();

	UWorld* World = GetWorld();
	TArray<APlayerStart*> UntaggedStarts;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		// 에디터 "여기서 플레이" 시작점은 팀 스폰이 아니다.
		if (It->IsA<APlayerStartPIE>())
		{
			continue;
		}

		if (It->PlayerStartTag == AttackStartTag)
		{
			AttackSpawnBases.Add(It->GetActorTransform());
		}
		else if (It->PlayerStartTag == DefenseStartTag)
		{
			DefenseSpawnBases.Add(It->GetActorTransform());
		}
		else
		{
			UntaggedStarts.Add(*It);
		}
	}

	if (AttackSpawnBases.Num() > 0 && DefenseSpawnBases.Num() > 0)
	{
		return;
	}

	// 대체 1: 태그 없는 시작점이 둘 이상이면 서로 먼 두 무리로 나누고, 각 무리는 상대 무리 쪽을 바라보게 한다.
	AttackSpawnBases.Reset();
	DefenseSpawnBases.Reset();
	if (UntaggedStarts.Num() >= 2)
	{
		TArray<FVector> Locations;
		for (const APlayerStart* Start : UntaggedStarts)
		{
			Locations.Add(Start->GetActorLocation());
		}

		TArray<int32> Groups;
		SplitSpawnGroups(Locations, Groups);
		FVector Centers[2] = {FVector::ZeroVector, FVector::ZeroVector};
		int32 Counts[2] = {0, 0};
		for (int32 Index = 0; Index < Locations.Num(); ++Index)
		{
			Centers[Groups[Index]] += Locations[Index];
			++Counts[Groups[Index]];
		}
		Centers[0] /= FMath::Max(Counts[0], 1);
		Centers[1] /= FMath::Max(Counts[1], 1);

		for (int32 Index = 0; Index < Locations.Num(); ++Index)
		{
			const FVector Facing = (Centers[1 - Groups[Index]] - Locations[Index]).GetSafeNormal2D();
			const FTransform SpawnBase(FRotator(0.0f, Facing.Rotation().Yaw, 0.0f), Locations[Index]);
			(Groups[Index] == 0 ? AttackSpawnBases : DefenseSpawnBases).Add(SpawnBase);
		}
		return;
	}

	// 대체 2: 시작점이 하나면 공격은 그 자리, 수비는 그 앞쪽 바닥(벽에 막히면 거리를 줄여 가며 찾는다)에 서로 마주 보게 만든다.
	const FTransform Origin = UntaggedStarts.Num() > 0 ? UntaggedStarts[0]->GetActorTransform() : FTransform::Identity;
	const FVector Forward = Origin.GetRotation().GetForwardVector().GetSafeNormal2D();
	const APawn* PawnDefaults = DefaultPawnClass ? DefaultPawnClass->GetDefaultObject<APawn>() : nullptr;
	const ACharacter* CharacterDefaults = Cast<ACharacter>(PawnDefaults);
	const float HalfHeight = CharacterDefaults && CharacterDefaults->GetCapsuleComponent() ? CharacterDefaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 96.0f;

	FVector DefenseLocation = Origin.GetLocation() + (Forward * 300.0f);
	for (float Distance = FallbackSideSeparationCm; Distance >= 300.0f; Distance -= 250.0f)
	{
		const FVector Candidate = Origin.GetLocation() + (Forward * Distance);
		FHitResult FloorHit;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ValorSpawnFloor), false);
		if (!World->LineTraceSingleByChannel(FloorHit, Candidate + FVector(0.0f, 0.0f, 300.0f), Candidate - FVector(0.0f, 0.0f, 2000.0f), ECC_WorldStatic, QueryParams))
		{
			continue;
		}

		const FVector Standing = FloorHit.ImpactPoint + FVector(0.0f, 0.0f, HalfHeight + 2.0f);
		if (!PawnDefaults || !World->EncroachingBlockingGeometry(PawnDefaults, Standing, FRotator::ZeroRotator))
		{
			DefenseLocation = Standing;
			break;
		}
	}

	const float AttackYaw = Forward.Rotation().Yaw;
	AttackSpawnBases.Add(FTransform(FRotator(0.0f, AttackYaw, 0.0f), Origin.GetLocation()));
	DefenseSpawnBases.Add(FTransform(FRotator(0.0f, AttackYaw + 180.0f, 0.0f), DefenseLocation));
}

FTransform AValorMatchGameMode::FindSpawnTransform(EValorTeamSide Side, int32 IndexInSide) const
{
	const TArray<FTransform>& Bases = Side == EValorTeamSide::Defense ? DefenseSpawnBases : AttackSpawnBases;
	if (Bases.Num() == 0)
	{
		return FTransform::Identity;
	}

	const FTransform& Base = Bases[IndexInSide % Bases.Num()];
	const int32 SlotInBase = IndexInSide / Bases.Num();
	const FVector Right = Base.GetRotation().GetRightVector().GetSafeNormal2D();
	const APawn* PawnDefaults = DefaultPawnClass ? DefaultPawnClass->GetDefaultObject<APawn>() : nullptr;

	// 0, +1, -1, +2, -2 ... 칸 옆으로 비켜 서며, 벽·다른 캐릭터에 겹치지 않는 첫 자리를 쓴다.
	for (int32 Attempt = SlotInBase; Attempt < SlotInBase + 10; ++Attempt)
	{
		const int32 Step = (Attempt % 2 == 1) ? (Attempt + 1) / 2 : -(Attempt / 2);
		const FVector Location = Base.GetLocation() + (Right * (Step * TeammateSpawnSpacingCm));
		if (!PawnDefaults || !GetWorld()->EncroachingBlockingGeometry(PawnDefaults, Location, Base.Rotator()))
		{
			return FTransform(Base.GetRotation(), Location);
		}
	}

	return Base;
}

void AValorMatchGameMode::SpawnForRound(AController* Controller, int32 IndexInSide, bool bKeepLoadout)
{
	AValorPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AValorPlayerState>() : nullptr;
	const AValorGameState* ValorGameState = GetValorGameState();
	if (!PlayerState || !ValorGameState)
	{
		return;
	}

	// 살아남은 캐릭터의 장비를 기록해 두고(새 캐릭터에 SetPlayerDefaults가 다시 지급) 옛 캐릭터는 치운다.
	PlayerState->CarriedLoadout = FValorCarriedLoadout();
	if (AValorCharacter* OldCharacter = Cast<AValorCharacter>(Controller->GetPawn()))
	{
		if (bKeepLoadout && OldCharacter->IsAlive())
		{
			FValorCarriedLoadout& Carried = PlayerState->CarriedLoadout;
			if (const UValorCombatComponent* CombatComponent = OldCharacter->GetCombatComponent())
			{
				const AValorWeaponBase* Primary = CombatComponent->GetWeaponInSlot(EValorWeaponSlot::Primary);
				const AValorWeaponBase* Secondary = CombatComponent->GetWeaponInSlot(EValorWeaponSlot::Secondary);
				Carried.PrimaryClass = Primary ? Primary->GetClass() : nullptr;
				Carried.SecondaryClass = Secondary ? Secondary->GetClass() : nullptr;
			}
			if (const UAbilitySystemComponent* AbilitySystemComponent = OldCharacter->GetAbilitySystemComponent())
			{
				Carried.Shield = AbilitySystemComponent->GetNumericAttribute(UValorCombatAttributeSet::GetShieldAttribute());
				Carried.MaxShield = AbilitySystemComponent->GetNumericAttribute(UValorCombatAttributeSet::GetMaxShieldAttribute());
			}
			Carried.bValid = true;
		}

		Controller->UnPossess();
		OldCharacter->Destroy();
	}

	const EValorTeamSide Side = ValorGameState->GetSideOfTeam(PlayerState->GetTeam());
	RestartPlayerAtTransform(Controller, FindSpawnTransform(Side, IndexInSide));
}

void AValorMatchGameMode::SetPlayerDefaults(APawn* PlayerPawn)
{
	Super::SetPlayerDefaults(PlayerPawn);

	AValorCharacter* Character = Cast<AValorCharacter>(PlayerPawn);
	AValorPlayerState* PlayerState = Character ? Character->GetPlayerState<AValorPlayerState>() : nullptr;
	if (!PlayerState || !PlayerState->CarriedLoadout.bValid)
	{
		return;
	}

	// 살아남은 장비 다시 지급: 보조무기(기본 클래식을 대신함) → 주무기 순, 가장 좋은 무기를 들고 시작한다. 실드는 남은 양 그대로.
	const FValorCarriedLoadout Carried = PlayerState->CarriedLoadout;
	PlayerState->CarriedLoadout = FValorCarriedLoadout();
	if (UValorCombatComponent* CombatComponent = Character->GetCombatComponent())
	{
		if (Carried.SecondaryClass)
		{
			CombatComponent->GiveWeapon(Carried.SecondaryClass, !Carried.PrimaryClass, true);
		}
		if (Carried.PrimaryClass)
		{
			CombatComponent->GiveWeapon(Carried.PrimaryClass, true, true);
		}
	}

	if (Carried.MaxShield > 0.0f)
	{
		SetShield(*Character, Carried.Shield, Carried.MaxShield);
	}
}

// ======================================================================================
// 상점
// ======================================================================================

bool AValorMatchGameMode::GetBuyContext(AController* Buyer, AValorCharacter*& OutCharacter, AValorPlayerState*& OutPlayerState) const
{
	const AValorGameState* ValorGameState = GetValorGameState();
	OutCharacter = Buyer ? Cast<AValorCharacter>(Buyer->GetPawn()) : nullptr;
	OutPlayerState = Buyer ? Buyer->GetPlayerState<AValorPlayerState>() : nullptr;
	return ValorGameState && ValorGameState->IsBuyPhase() && OutCharacter && OutCharacter->IsAlive() && OutPlayerState && OutCharacter->GetCombatComponent();
}

void AValorMatchGameMode::SetShield(AValorCharacter& Character, float Shield, float MaxShield) const
{
	// 최대치를 먼저 바꿔야 현재치가 잘리지 않는다(PreAttributeChange 클램프).
	if (UAbilitySystemComponent* AbilitySystemComponent = Character.GetAbilitySystemComponent())
	{
		AbilitySystemComponent->SetNumericAttributeBase(UValorCombatAttributeSet::GetMaxShieldAttribute(), MaxShield);
		AbilitySystemComponent->SetNumericAttributeBase(UValorCombatAttributeSet::GetShieldAttribute(), Shield);
	}
}

void AValorMatchGameMode::RefundWeaponPurchaseInSlot(AValorCharacter& Character, AValorPlayerState& PlayerState, uint8 SlotValue)
{
	const EValorWeaponSlot Slot = static_cast<EValorWeaponSlot>(SlotValue);
	UValorCombatComponent* CombatComponent = Character.GetCombatComponent();
	const AValorWeaponBase* Current = CombatComponent ? CombatComponent->GetWeaponInSlot(Slot) : nullptr;
	FValorRoundPurchase Purchase;
	if (Current && PlayerState.TakeWeaponPurchase(Current->GetClass(), Purchase))
	{
		CombatComponent->RemoveWeaponInSlot(Slot);
		PlayerState.AddCredits(Purchase.Cost);
	}
}

bool AValorMatchGameMode::TryBuyWeapon(AController* Buyer, TSubclassOf<AValorWeaponBase> WeaponClass)
{
	AValorCharacter* Character = nullptr;
	AValorPlayerState* PlayerState = nullptr;
	const AValorGameState* ValorGameState = GetValorGameState();
	const FValorShopWeaponEntry* Entry = ValorGameState ? ValorGameState->FindShopWeapon(WeaponClass) : nullptr;
	if (!Entry || !GetBuyContext(Buyer, Character, PlayerState))
	{
		return false;
	}

	UValorCombatComponent* CombatComponent = Character->GetCombatComponent();
	const EValorWeaponSlot Slot = Entry->Category == EValorWeaponCategory::Sidearm ? EValorWeaponSlot::Secondary : EValorWeaponSlot::Primary;
	const AValorWeaponBase* Current = CombatComponent->GetWeaponInSlot(Slot);
	if (Current && Current->GetClass() == WeaponClass)
	{
		return false;
	}

	// 같은 슬롯에 이번 라운드에 산 총이 있으면 그 값은 돌려받는 셈으로 친다(발로란트: 바꿔 사면 앞의 것은 자동 환불).
	int32 RefundableCredits = 0;
	if (Current)
	{
		const TArray<FValorRoundPurchase>& Purchases = PlayerState->GetRoundPurchases();
		const int32 PurchaseIndex = Purchases.FindLastByPredicate([Current](const FValorRoundPurchase& Purchase) { return Purchase.WeaponClass == Current->GetClass(); });
		RefundableCredits = PurchaseIndex != INDEX_NONE ? Purchases[PurchaseIndex].Cost : 0;
	}

	if (PlayerState->GetCredits() + RefundableCredits < Entry->Cost)
	{
		return false;
	}

	if (RefundableCredits > 0)
	{
		RefundWeaponPurchaseInSlot(*Character, *PlayerState, static_cast<uint8>(Slot));
	}

	// 직접 산 총이 아닌(살아남아 가져온·주운) 총은 GiveWeapon의 교체 규칙대로 바닥에 떨어진다.
	PlayerState->AddCredits(-Entry->Cost);
	CombatComponent->GiveWeapon(WeaponClass, true, true);

	FValorRoundPurchase Purchase;
	Purchase.WeaponClass = WeaponClass;
	Purchase.Cost = Entry->Cost;
	PlayerState->AddRoundPurchase(Purchase);
	return true;
}

bool AValorMatchGameMode::TrySellWeapon(AController* Buyer, TSubclassOf<AValorWeaponBase> WeaponClass)
{
	AValorCharacter* Character = nullptr;
	AValorPlayerState* PlayerState = nullptr;
	const AValorGameState* ValorGameState = GetValorGameState();
	const FValorShopWeaponEntry* Entry = ValorGameState ? ValorGameState->FindShopWeapon(WeaponClass) : nullptr;
	if (!Entry || !GetBuyContext(Buyer, Character, PlayerState))
	{
		return false;
	}

	// 산 총을 아직 들고 있을 때만 되판다(버린 총은 되팔 수 없다).
	const EValorWeaponSlot Slot = Entry->Category == EValorWeaponCategory::Sidearm ? EValorWeaponSlot::Secondary : EValorWeaponSlot::Primary;
	const AValorWeaponBase* Current = Character->GetCombatComponent()->GetWeaponInSlot(Slot);
	if (!Current || Current->GetClass() != WeaponClass || !PlayerState->HasPurchasedWeaponThisRound(WeaponClass))
	{
		return false;
	}

	RefundWeaponPurchaseInSlot(*Character, *PlayerState, static_cast<uint8>(Slot));
	return true;
}

bool AValorMatchGameMode::TryBuyShield(AController* Buyer, int32 ShieldIndex)
{
	AValorCharacter* Character = nullptr;
	AValorPlayerState* PlayerState = nullptr;
	const AValorGameState* ValorGameState = GetValorGameState();
	if (!ValorGameState || !ValorGameState->GetShopShields().IsValidIndex(ShieldIndex) || !GetBuyContext(Buyer, Character, PlayerState))
	{
		return false;
	}

	const FValorShieldOffer& Offer = ValorGameState->GetShopShields()[ShieldIndex];
	const UAbilitySystemComponent* AbilitySystemComponent = Character->GetAbilitySystemComponent();
	if (!AbilitySystemComponent)
	{
		return false;
	}

	// 이번 라운드에 산 실드가 있으면 먼저 되돌린 셈으로 계산한다(경량 → 중형으로 바꿔 사기).
	FValorRoundPurchase PreviousShieldPurchase;
	const int32 PreviousIndex = PlayerState->GetRoundPurchases().FindLastByPredicate([](const FValorRoundPurchase& Purchase) { return Purchase.ShieldIndex != INDEX_NONE; });
	if (PreviousIndex != INDEX_NONE)
	{
		PreviousShieldPurchase = PlayerState->GetRoundPurchases()[PreviousIndex];
	}
	const bool bReplacesPurchase = PreviousIndex != INDEX_NONE;
	const float CurrentShield = bReplacesPurchase ? PreviousShieldPurchase.PreviousShield : AbilitySystemComponent->GetNumericAttribute(UValorCombatAttributeSet::GetShieldAttribute());
	const float CurrentMaxShield = bReplacesPurchase ? PreviousShieldPurchase.PreviousMaxShield : AbilitySystemComponent->GetNumericAttribute(UValorCombatAttributeSet::GetMaxShieldAttribute());
	const int32 RefundableCredits = bReplacesPurchase ? PreviousShieldPurchase.Cost : 0;

	if (bReplacesPurchase && PreviousShieldPurchase.ShieldIndex == ShieldIndex)
	{
		return false;
	}

	// 이미 같거나 더 좋은 실드(살아남아 가져온 것)가 있으면 살 필요가 없다.
	if (CurrentShield >= Offer.ShieldAmount || PlayerState->GetCredits() + RefundableCredits < Offer.Cost)
	{
		return false;
	}

	if (bReplacesPurchase)
	{
		FValorRoundPurchase Taken;
		PlayerState->TakeShieldPurchase(PreviousShieldPurchase.ShieldIndex, Taken);
		PlayerState->AddCredits(Taken.Cost);
	}

	PlayerState->AddCredits(-Offer.Cost);
	SetShield(*Character, Offer.ShieldAmount, Offer.ShieldAmount);

	FValorRoundPurchase Purchase;
	Purchase.ShieldIndex = ShieldIndex;
	Purchase.Cost = Offer.Cost;
	Purchase.PreviousShield = CurrentShield;
	Purchase.PreviousMaxShield = CurrentMaxShield;
	PlayerState->AddRoundPurchase(Purchase);
	return true;
}

bool AValorMatchGameMode::TrySellShield(AController* Buyer, int32 ShieldIndex)
{
	AValorCharacter* Character = nullptr;
	AValorPlayerState* PlayerState = nullptr;
	FValorRoundPurchase Purchase;
	if (!GetBuyContext(Buyer, Character, PlayerState) || !PlayerState->TakeShieldPurchase(ShieldIndex, Purchase))
	{
		return false;
	}

	SetShield(*Character, Purchase.PreviousShield, Purchase.PreviousMaxShield);
	PlayerState->AddCredits(Purchase.Cost);
	return true;
}

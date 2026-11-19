#include "ValorGameMode.h"

#include "Weapons/ValorMeleeWeapon.h"

#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystem/ValorDamageTypes.h"
#include "AbilitySystemComponent.h"
#include "Components/ValorCombatComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/PlayerStartPIE.h"
#include "Engine/NetDriver.h"
#include "Misc/PackageName.h"
#include "Player/ValorPlayerController.h"
#include "Player/ValorPlayerState.h"
#include "TimerManager.h"
#include "UI/ValorHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "ValorCharacter.h"
#include "ValorGameState.h"
#include "Weapons/ValorWeaponBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogValorServer, Log, All);

void AValorGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 데디 서버가 포트를 열지 못하면(같은 PC에 이미 서버가 떠 있음) 아무도 들어올 수 없는 서버로 남는다.
	// 엔진 기본값은 다음 포트(7778...)로 조용히 옮기는데, 그러면 클라는 127.0.0.1(7777)로 엉뚱한 서버에 붙는다.
	// DedicatedServerEngine.ini에서 옮기기를 끄고, 여기서 실패를 크게 남기고 끝낸다.
	const UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_DedicatedServer && !World->GetNetDriver())
	{
		UE_LOG(LogValorServer, Error, TEXT("포트 %d번을 열 수 없어 서버를 끝냅니다. 이 PC에 이미 다른 서버(메뉴의 로컬 서버 포함)가 떠 있는지 확인하세요. 다른 포트는 -port=7778."), World->URL.Port);
		RequestEngineExit(TEXT("Valor: server port in use"));
	}
}

AValorGameMode::AValorGameMode()
{
	// 총기/전투 시스템은 AValorCharacter에 직접 연결되어 있으므로 기본 Pawn도 C++ 캐릭터를 사용한다.
	DefaultPawnClass = AValorCharacter::StaticClass();
	PlayerControllerClass = AValorPlayerController::StaticClass();
	PlayerStateClass = AValorPlayerState::StaticClass();
	GameStateClass = AValorGameState::StaticClass();

	// 크로스헤어 HUD. HUD는 각 클라이언트의 PlayerController가 로컬에서만 생성하므로 서버/복제 비용이 없다.
	// BP_ValorGameMode는 HUD Class를 덮어쓰지 않으므로 이 기본값을 그대로 상속한다.
	HUDClass = AValorHUD::StaticClass();

	// 기본 지급 무기: 밴달 BP(메시·데이터 에셋이 연결된 유일한 기준 총). 경로가 바뀌면 BP_ValorGameMode에서 다시 지정하면 된다.
	static ConstructorHelpers::FClassFinder<AValorWeaponBase> DefaultStartingWeapon(TEXT("/Game/Valor/Gun/BP_Vandal"));
	if (DefaultStartingWeapon.Succeeded())
	{
		StartingWeaponClass = DefaultStartingWeapon.Class;
	}

	// 기본 보조무기: 클래식 BP(BP_Vandal을 복제해 Weapon Data Asset = DA_Classic으로 지정하면 된다). 없으면 지급하지 않는다.
	StartingSecondaryWeaponClass = TSoftClassPtr<AValorWeaponBase>(FSoftObjectPath(TEXT("/Game/Valor/Gun/BP_Classic.BP_Classic_C")));
}

TSubclassOf<AValorWeaponBase> AValorGameMode::ResolveStartingSecondaryWeaponClass()
{
	if (bStartingSecondaryResolved)
	{
		return ResolvedStartingSecondaryWeaponClass;
	}

	bStartingSecondaryResolved = true;
	if (StartingSecondaryWeaponClass.IsNull())
	{
		return nullptr;
	}

	// 에셋이 없으면 조용히 건너뛴다(로드 실패 경고 대신 안내 한 줄). 첫 스폰에서 한 번만 동기 로드한다.
	const FString PackageName = StartingSecondaryWeaponClass.ToSoftObjectPath().GetLongPackageName();
	if (!FPackageName::DoesPackageExist(PackageName))
	{
		UE_LOG(LogTemp, Log, TEXT("[Loadout] 기본 보조무기 %s 가 없어 보조무기 없이 스폰한다(BP_Vandal 복제 → DA_Classic 지정으로 만들 수 있다)."), *PackageName);
		return nullptr;
	}

	ResolvedStartingSecondaryWeaponClass = StartingSecondaryWeaponClass.LoadSynchronous();
	return ResolvedStartingSecondaryWeaponClass;
}

void AValorGameMode::HandleCharacterKilled(AValorCharacter* Victim, const FValorDamageEvent& KillingEvent)
{
	UWorld* World = GetWorld();
	if (!World || !Victim)
	{
		return;
	}

	AController* VictimController = Victim->GetController();
	const APawn* KillerPawn = Cast<APawn>(KillingEvent.Instigator.Get());
	const AController* KillerController = KillerPawn ? KillerPawn->GetController() : nullptr;

	AValorPlayerState* VictimState = Victim->GetPlayerState<AValorPlayerState>();
	AValorPlayerState* KillerState = KillerController ? KillerController->GetPlayerState<AValorPlayerState>() : nullptr;
	const bool bSelfKill = KillerPawn == Victim;
	const bool bHeadshot = KillingEvent.HitZone == EValorHitZone::Head;

	// 1) 전적: 피해자는 사망 +1, 처치자는 처치 +1(자기 자신은 제외).
	if (VictimState)
	{
		VictimState->AddDeath();
	}

	if (KillerState && !bSelfKill && KillerState != VictimState)
	{
		KillerState->AddKill(bHeadshot);
	}

	// 1-1) 처치 점수와 어시스트(점수판).
	AwardKillScoreAndAssists(Victim, VictimState, KillerState, bSelfKill);

	// 2) 킬피드(모두에게). 무기는 처치에 쓰인 사수의 총 또는 칼(피해 GE의 EffectCauser)이다. 스킬(화염 지대 등)은 무기 표시 없음.
	const AActor* DamageCauser = KillingEvent.DamageCauser.Get();
	const AActor* KillerWeapon = (Cast<AValorWeaponBase>(DamageCauser) || Cast<AValorMeleeWeapon>(DamageCauser)) ? DamageCauser : nullptr;

	FValorKillFeedEntry Entry;
	Entry.KillerName = (KillerState && !bSelfKill) ? KillerState->GetPlayerName() : FString();
	Entry.VictimName = VictimState ? VictimState->GetPlayerName() : Victim->GetName();
	Entry.WeaponClass = KillerWeapon ? KillerWeapon->GetClass() : nullptr;
	Entry.bHeadshot = bHeadshot;
	Entry.bWallbang = KillingEvent.bPenetrated;
	Entry.KillerTeam = KillerState ? KillerState->GetTeam() : EValorTeam::None;
	Entry.VictimTeam = VictimState ? VictimState->GetTeam() : EValorTeam::None;

	if (AValorGameState* ValorGameState = GetGameState<AValorGameState>())
	{
		ValorGameState->BroadcastKill(Entry);
	}

	// 3) 피해자 화면: 누가 무엇으로 죽였고 언제 부활하는지(부활하지 않는 모드는 부활 시각 -1).
	const float RespawnDelayAfterDeath = GetRespawnDelayAfterDeath();
	const bool bWillRespawn = RespawnDelayAfterDeath >= 0.0f;
	const float SafeRespawnDelay = FMath::Max(RespawnDelayAfterDeath, 0.01f);
	if (AValorPlayerController* VictimPlayerController = Cast<AValorPlayerController>(VictimController))
	{
		FValorDeathInfo DeathInfo;
		DeathInfo.KillerName = Entry.KillerName;
		DeathInfo.WeaponClass = Entry.WeaponClass;
		DeathInfo.bHeadshot = bHeadshot;
		DeathInfo.RespawnServerTime = bWillRespawn ? static_cast<float>(World->GetTimeSeconds() + SafeRespawnDelay) : -1.0f;
		VictimPlayerController->ClientNotifyKilled(DeathInfo);
	}

	// 4) 모드별 규칙(처치 보상, 전멸 판정 등).
	OnCharacterKilled(Victim, VictimController, VictimState, KillerState, bSelfKill);

	// 5) 부활 예약. 컨트롤러는 약참조로 넘겨, 그 사이 접속을 끊으면 아무 일도 하지 않는다.
	if (VictimController && bWillRespawn)
	{
		FTimerHandle RespawnTimerHandle;
		World->GetTimerManager().SetTimer(RespawnTimerHandle,
			FTimerDelegate::CreateUObject(this, &AValorGameMode::RespawnPlayer, TWeakObjectPtr<AController>(VictimController)),
			SafeRespawnDelay, false);
	}
}

void AValorGameMode::RespawnPlayer(TWeakObjectPtr<AController> ControllerToRespawn)
{
	AController* Controller = ControllerToRespawn.Get();
	if (!Controller)
	{
		return;
	}

	// 이미 살아 있는 캐릭터가 있으면(중복 예약 등) 건드리지 않는다.
	if (const AValorCharacter* CurrentCharacter = Cast<AValorCharacter>(Controller->GetPawn()))
	{
		if (CurrentCharacter->IsAlive())
		{
			return;
		}
	}

	// 시체는 잠시 남기고 빙의만 푼다. 빙의가 풀려야 RestartPlayer가 새 Pawn을 스폰한다.
	if (APawn* Corpse = Controller->GetPawn())
	{
		Controller->UnPossess();
		if (CorpseLifeSpanAfterRespawn > 0.0f)
		{
			Corpse->SetLifeSpan(CorpseLifeSpanAfterRespawn);
		}
		else
		{
			Corpse->Destroy();
		}
	}

	RestartPlayer(Controller);
}

bool AValorGameMode::ShouldSpawnAtStartSpot(AController* Player)
{
	// 기본 구현은 처음 고른 StartSpot을 계속 재사용한다 → 부활할 때마다 같은 자리. 매번 ChoosePlayerStart로 다시 고르게 한다.
	return false;
}

AActor* AValorGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	UClass* PawnClass = GetDefaultPawnClassForController(Player);
	const APawn* PawnToFit = PawnClass ? PawnClass->GetDefaultObject<APawn>() : nullptr;

	TArray<APlayerStart*> FreeStarts;
	TArray<FVector> FreeStartLocations;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		APlayerStart* PlayerStart = *It;

		// 에디터 "여기서 플레이" 시작점은 기본 규칙(항상 그 자리)을 따른다.
		if (PlayerStart->IsA<APlayerStartPIE>())
		{
			return Super::ChoosePlayerStart_Implementation(Player);
		}

		if (!World->EncroachingBlockingGeometry(PawnToFit, PlayerStart->GetActorLocation(), PlayerStart->GetActorRotation()))
		{
			FreeStarts.Add(PlayerStart);
			FreeStartLocations.Add(PlayerStart->GetActorLocation());
		}
	}

	// 살아 있는 다른 캐릭터에게서 가장 먼 시작점(데스매치에서 부활하자마자 등 뒤를 잡히지 않게).
	TArray<FVector> ThreatLocations;
	for (TActorIterator<AValorCharacter> It(World); It; ++It)
	{
		if (It->IsAlive() && It->GetController() != Player)
		{
			ThreatLocations.Add(It->GetActorLocation());
		}
	}

	const int32 SafestIndex = PickSafestSpawnIndex(FreeStartLocations, ThreatLocations);
	if (FreeStarts.IsValidIndex(SafestIndex))
	{
		return FreeStarts[SafestIndex];
	}

	return Super::ChoosePlayerStart_Implementation(Player);
}

int32 AValorGameMode::PickSafestSpawnIndex(const TArray<FVector>& CandidateLocations, const TArray<FVector>& ThreatLocations)
{
	if (CandidateLocations.Num() == 0 || ThreatLocations.Num() == 0)
	{
		return INDEX_NONE;
	}

	int32 BestIndex = INDEX_NONE;
	double BestNearestThreatDistSq = -1.0;
	for (int32 CandidateIndex = 0; CandidateIndex < CandidateLocations.Num(); ++CandidateIndex)
	{
		double NearestThreatDistSq = TNumericLimits<double>::Max();
		for (const FVector& ThreatLocation : ThreatLocations)
		{
			NearestThreatDistSq = FMath::Min(NearestThreatDistSq, FVector::DistSquared(CandidateLocations[CandidateIndex], ThreatLocation));
		}

		if (NearestThreatDistSq > BestNearestThreatDistSq)
		{
			BestNearestThreatDistSq = NearestThreatDistSq;
			BestIndex = CandidateIndex;
		}
	}

	return BestIndex;
}

void AValorGameMode::SetPlayerDefaults(APawn* PlayerPawn)
{
	Super::SetPlayerDefaults(PlayerPawn);

	// 연속 처치 추가 점수는 이번 목숨(경기 모드는 라운드마다 다시 스폰하므로 이번 라운드) 기준이다.
	if (AValorPlayerState* SpawnedState = PlayerPawn ? PlayerPawn->GetPlayerState<AValorPlayerState>() : nullptr)
	{
		SpawnedState->ResetRoundKills();
	}

	AValorCharacter* Character = Cast<AValorCharacter>(PlayerPawn);
	if (!Character)
	{
		return;
	}

	// 스폰 실드. 속성 기본값은 서버에서만 바꾸고 복제로 퍼진다. 최대치를 먼저 올려야 현재치가 잘리지 않는다(PreAttributeChange 클램프).
	if (SpawnShield > 0.0f)
	{
		if (UAbilitySystemComponent* AbilitySystemComponent = Character->GetAbilitySystemComponent())
		{
			AbilitySystemComponent->SetNumericAttributeBase(UValorCombatAttributeSet::GetMaxShieldAttribute(), SpawnShield);
			AbilitySystemComponent->SetNumericAttributeBase(UValorCombatAttributeSet::GetShieldAttribute(), SpawnShield);
		}
	}

	// 기본 지급: 보조무기(있으면) → 주무기 순으로 넣고, 가장 좋은 무기를 들고 시작한다. 스폰 지급은 장착 시간 없이 바로 쏠 수 있다.
	if (UValorCombatComponent* CombatComponent = Character->GetCombatComponent())
	{
		if (const TSubclassOf<AValorWeaponBase> SecondaryClass = ResolveStartingSecondaryWeaponClass())
		{
			CombatComponent->GiveWeapon(SecondaryClass, !StartingWeaponClass, true);
		}

		if (StartingWeaponClass)
		{
			CombatComponent->GiveWeapon(StartingWeaponClass, true, true);
		}
	}
}

int32 AValorGameMode::CountAliveEnemiesOf(EValorTeam Team, const AValorPlayerState* Exclude) const
{
	int32 Count = 0;
	for (TActorIterator<AValorCharacter> It(GetWorld()); It; ++It)
	{
		const AValorPlayerState* CharacterState = It->GetPlayerState<AValorPlayerState>();
		if (!It->IsAlive() || !CharacterState || CharacterState == Exclude)
		{
			continue;
		}
		if (Team == EValorTeam::None || CharacterState->GetTeam() != Team)
		{
			++Count;
		}
	}
	return Count;
}

void AValorGameMode::AwardKillScoreAndAssists(AValorCharacter* Victim, AValorPlayerState* VictimState, AValorPlayerState* KillerState, bool bSelfKill)
{
	const UWorld* World = GetWorld();
	if (!World || !Victim)
	{
		return;
	}

	// 적을 처치한 경우만(자기 처치·환경 처치·아군 처치는 점수와 어시스트가 없다).
	const bool bEnemyKill = KillerState && VictimState && !bSelfKill && KillerState != VictimState
		&& (KillerState->GetTeam() == EValorTeam::None || KillerState->GetTeam() != VictimState->GetTeam());
	if (!bEnemyKill)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();

	// 처치 점수: 처치 직전 남은 적 수(맞은 사람은 이미 죽었으므로 +1). 연속 처치는 두 번째부터 +50.
	const int32 EnemiesAliveBeforeKill = CountAliveEnemiesOf(KillerState->GetTeam(), KillerState) + 1;
	const int32 KillNumber = KillerState->AddRoundKill();
	KillerState->AddCombatScore(ValorScoreRules::ComputeKillScore(EnemiesAliveBeforeKill) + ValorScoreRules::ComputeMultiKillBonus(KillNumber));

	// 피해·섬광·정찰 어시스트: 처치자 말고 그 적에게 기여한 사람(기록은 적에게서만 쌓인다). 피해 없는 어시스트는 +25.
	TArray<const AValorPlayerState*, TInlineAllocator<4>> Assisted;
	for (const FValorAssistContribution& Contribution : Victim->GetAssistContributions())
	{
		AValorPlayerState* Assister = Contribution.Player.Get();
		if (!Assister || Assister == KillerState || Assister == VictimState)
		{
			continue;
		}
		const bool bDamageAssist = ValorScoreRules::QualifiesForDamageAssist(Contribution.TotalDamage, ValorScoreRules::SumRecentDamage(Contribution.RecentHits, Now));
		const bool bUtilityAssist = ValorScoreRules::QualifiesForUtilityAssist(Contribution.UtilityEndTime, Now);
		if (!bDamageAssist && !bUtilityAssist)
		{
			continue;
		}
		Assister->AddAssist();
		if (Contribution.TotalDamage <= 0.0f)
		{
			Assister->AddCombatScore(ValorScoreRules::NonDamageAssistScore);
		}
		Assisted.Add(Assister);
	}

	// 회복 어시스트: 처치자를 회복시켜 준 아군(회복이 끝난 뒤 10초 안의 처치). 이미 같은 처치로 어시스트를 받았으면 한 번만.
	const AValorCharacter* KillerCharacter = Cast<AValorCharacter>(KillerState->GetPawn());
	AValorPlayerState* Healer = KillerCharacter ? KillerCharacter->GetLastHealer() : nullptr;
	if (Healer && Healer != KillerState && Healer != VictimState && !Assisted.Contains(Healer) && ValorScoreRules::QualifiesForHealAssist(KillerCharacter->GetLastHealEndTime(), Now))
	{
		Healer->AddAssist();
		Healer->AddCombatScore(ValorScoreRules::NonDamageAssistScore);
	}
}

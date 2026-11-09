#include "ValorGameMode.h"

#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystem/ValorDamageTypes.h"
#include "AbilitySystemComponent.h"
#include "Components/ValorCombatComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/PlayerStartPIE.h"
#include "Misc/PackageName.h"
#include "Player/ValorPlayerController.h"
#include "Player/ValorPlayerState.h"
#include "TimerManager.h"
#include "UI/ValorHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "ValorCharacter.h"
#include "ValorGameState.h"
#include "Weapons/ValorWeaponBase.h"

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

	// 2) 킬피드(모두에게). 무기는 처치에 쓰인 사수의 무기(피해 GE의 EffectCauser)다.
	const AValorWeaponBase* KillerWeapon = Cast<AValorWeaponBase>(KillingEvent.DamageCauser.Get());

	FValorKillFeedEntry Entry;
	Entry.KillerName = (KillerState && !bSelfKill) ? KillerState->GetPlayerName() : FString();
	Entry.VictimName = VictimState ? VictimState->GetPlayerName() : Victim->GetName();
	Entry.WeaponClass = KillerWeapon ? KillerWeapon->GetClass() : nullptr;
	Entry.bHeadshot = bHeadshot;
	Entry.bWallbang = KillingEvent.bPenetrated;

	if (AValorGameState* ValorGameState = GetGameState<AValorGameState>())
	{
		ValorGameState->BroadcastKill(Entry);
	}

	// 3) 피해자 화면: 누가 무엇으로 죽였고 언제 부활하는지.
	const float SafeRespawnDelay = FMath::Max(RespawnDelay, 0.01f);
	if (AValorPlayerController* VictimPlayerController = Cast<AValorPlayerController>(VictimController))
	{
		FValorDeathInfo DeathInfo;
		DeathInfo.KillerName = Entry.KillerName;
		DeathInfo.WeaponClass = Entry.WeaponClass;
		DeathInfo.bHeadshot = bHeadshot;
		DeathInfo.RespawnServerTime = static_cast<float>(World->GetTimeSeconds() + SafeRespawnDelay);
		VictimPlayerController->ClientNotifyKilled(DeathInfo);
	}

	// 4) 부활 예약. 컨트롤러는 약참조로 넘겨, 그 사이 접속을 끊으면 아무 일도 하지 않는다.
	if (VictimController)
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

#include "ValorGameState.h"

#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/ValorWeaponBase.h"

AValorGameState::AValorGameState()
{
	// 상점 총 목록(발로란트 상점 순서: 권총 → 기관단총 → 산탄총 → 소총 → 저격총 → 중화기). 경로에 에셋이 없으면 건너뛴다.
	static const TCHAR* const WeaponBlueprintNames[] =
	{
		TEXT("BP_Classic"), TEXT("BP_Shorty"), TEXT("BP_Frenzy"), TEXT("BP_Ghost"), TEXT("BP_Sheriff"),
		TEXT("BP_STINGER"), TEXT("BP_Spectre"),
		TEXT("BP_BUCKY"), TEXT("BP_JUDGE"),
		TEXT("BP_BULLDOG"), TEXT("BP_GUARDIAN"), TEXT("BP_Phantom"), TEXT("BP_Vandal"),
		TEXT("BP_MARSHALL"), TEXT("BP_OUTLAW"), TEXT("BP_Operator"),
		TEXT("BP_ARES"), TEXT("BP_ODIN"), TEXT("BP_WARDEN")
	};
	for (const TCHAR* BlueprintName : WeaponBlueprintNames)
	{
		ShopWeaponClasses.Add(TSoftClassPtr<AValorWeaponBase>(FSoftObjectPath(FString::Printf(TEXT("/Game/Valor/Gun/%s.%s_C"), BlueprintName, BlueprintName))));
	}

	// 발로란트 실드: 경량 400 = 25, 중형 1000 = 50.
	auto AddShield = [this](const TCHAR* DisplayName, int32 Cost, float Amount)
	{
		FValorShieldOffer& Offer = ShopShields.AddDefaulted_GetRef();
		Offer.DisplayName = DisplayName;
		Offer.Cost = Cost;
		Offer.ShieldAmount = Amount;
	};
	AddShield(TEXT("Light Shield"), 400, 25.0f);
	AddShield(TEXT("Heavy Shield"), 1000, 50.0f);
}

void AValorGameState::BeginPlay()
{
	Super::BeginPlay();

	// 전용 서버도 구매 검증에 목록이 필요하므로 모든 머신에서 만든다.
	BuildShopCatalog();
}

void AValorGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValorGameState, bUsesRounds);
	DOREPLIFETIME(AValorGameState, bFreezeDuringBuyPhase);
	DOREPLIFETIME(AValorGameState, bBlockFireDuringBuyPhase);
	DOREPLIFETIME(AValorGameState, RoundPhase);
	DOREPLIFETIME(AValorGameState, PhaseEndServerTime);
	DOREPLIFETIME(AValorGameState, RoundNumber);
	DOREPLIFETIME(AValorGameState, AttackingTeam);
	DOREPLIFETIME(AValorGameState, ScoreA);
	DOREPLIFETIME(AValorGameState, ScoreB);
	DOREPLIFETIME(AValorGameState, LastRoundWinner);
	DOREPLIFETIME(AValorGameState, LastRoundEndReason);
	DOREPLIFETIME(AValorGameState, MatchWinner);
}

void AValorGameState::BuildShopCatalog()
{
	ShopWeapons.Reset();
	ShopWeaponClassRefs.Reset();

	for (const TSoftClassPtr<AValorWeaponBase>& SoftClass : ShopWeaponClasses)
	{
		if (SoftClass.IsNull() || !FPackageName::DoesPackageExist(SoftClass.ToSoftObjectPath().GetLongPackageName()))
		{
			continue;
		}

		UClass* WeaponClass = SoftClass.LoadSynchronous();
		const AValorWeaponBase* WeaponDefaults = WeaponClass ? WeaponClass->GetDefaultObject<AValorWeaponBase>() : nullptr;
		if (!WeaponDefaults)
		{
			continue;
		}

		const FValorWeaponConfig& Config = WeaponDefaults->GetWeaponConfig();
		if (Config.Category == EValorWeaponCategory::Sidearm && !bAllowSidearmPurchases)
		{
			continue;
		}

		FValorShopWeaponEntry& Entry = ShopWeapons.AddDefaulted_GetRef();
		Entry.WeaponClass = WeaponClass;
		Entry.DisplayName = Config.DisplayName.IsEmpty() ? WeaponClass->GetName() : Config.DisplayName.ToString();
		Entry.Category = Config.Category;
		Entry.Cost = Config.Cost;
		ShopWeaponClassRefs.Add(WeaponClass);
	}
}

const FValorShopWeaponEntry* AValorGameState::FindShopWeapon(const UClass* WeaponClass) const
{
	return WeaponClass ? ShopWeapons.FindByPredicate([WeaponClass](const FValorShopWeaponEntry& Entry) { return Entry.WeaponClass == WeaponClass; }) : nullptr;
}

void AValorGameState::SetRoundPhase(EValorRoundPhase NewPhase, float DurationSeconds)
{
	if (!HasAuthority())
	{
		return;
	}

	RoundPhase = NewPhase;
	PhaseEndServerTime = DurationSeconds > 0.0f ? static_cast<float>(GetServerWorldTimeSeconds()) + DurationSeconds : 0.0f;
}

void AValorGameState::SetRoundNumber(int32 NewRoundNumber, EValorTeam NewAttackingTeam)
{
	if (HasAuthority())
	{
		RoundNumber = NewRoundNumber;
		AttackingTeam = NewAttackingTeam;
	}
}

void AValorGameState::SetTeamScores(int32 NewScoreA, int32 NewScoreB)
{
	if (HasAuthority())
	{
		ScoreA = NewScoreA;
		ScoreB = NewScoreB;
	}
}

void AValorGameState::SetRoundResult(EValorTeam Winner, EValorRoundEndReason Reason)
{
	if (HasAuthority())
	{
		LastRoundWinner = Winner;
		LastRoundEndReason = Reason;
	}
}

void AValorGameState::SetMatchWinner(EValorTeam Winner)
{
	if (HasAuthority())
	{
		MatchWinner = Winner;
	}
}

EValorTeamSide AValorGameState::GetSideOfTeam(EValorTeam Team) const
{
	if (Team == EValorTeam::None)
	{
		return EValorTeamSide::None;
	}

	return Team == AttackingTeam ? EValorTeamSide::Attack : EValorTeamSide::Defense;
}

int32 AValorGameState::GetTeamScore(EValorTeam Team) const
{
	switch (Team)
	{
	case EValorTeam::TeamA: return ScoreA;
	case EValorTeam::TeamB: return ScoreB;
	default: return 0;
	}
}

float AValorGameState::GetPhaseTimeRemaining() const
{
	if (PhaseEndServerTime <= 0.0f)
	{
		return -1.0f;
	}

	return FMath::Max(0.0f, PhaseEndServerTime - static_cast<float>(GetServerWorldTimeSeconds()));
}

bool AValorGameState::IsWeaponFireAllowed() const
{
	if (!bUsesRounds)
	{
		return true;
	}

	return RoundPhase != EValorRoundPhase::MatchEnd && !(RoundPhase == EValorRoundPhase::BuyPhase && bBlockFireDuringBuyPhase);
}

bool AValorGameState::IsMovementLocked() const
{
	return bUsesRounds && bFreezeDuringBuyPhase && RoundPhase == EValorRoundPhase::BuyPhase;
}

void AValorGameState::BroadcastKill(const FValorKillFeedEntry& Entry)
{
	if (!HasAuthority())
	{
		return;
	}

	MulticastAddKillFeedEntry(Entry);
}

void AValorGameState::MulticastAddKillFeedEntry_Implementation(const FValorKillFeedEntry& Entry)
{
	// 전용 서버는 화면이 없으므로 쌓지 않는다(리슨 호스트는 서버이면서 화면이 있으므로 쌓는다).
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	FKillFeedRecord& Record = KillFeed.AddDefaulted_GetRef();
	Record.Entry = Entry;
	if (const AValorWeaponBase* WeaponDefaults = Entry.WeaponClass ? Entry.WeaponClass->GetDefaultObject<AValorWeaponBase>() : nullptr)
	{
		Record.WeaponDisplayName = WeaponDefaults->GetWeaponConfig().DisplayName.ToString();
	}

	const UWorld* World = GetWorld();
	Record.LocalTime = World ? World->GetTimeSeconds() : 0.0;

	if (KillFeed.Num() > MaxKillFeedEntries)
	{
		KillFeed.RemoveAt(0, KillFeed.Num() - MaxKillFeedEntries);
	}
}

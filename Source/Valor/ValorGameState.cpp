#include "ValorGameState.h"

#include "Weapons/ValorWeaponBase.h"

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

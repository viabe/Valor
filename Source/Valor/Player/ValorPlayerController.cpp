#include "ValorPlayerController.h"

#include "Kismet/GameplayStatics.h"
#include "ValorCharacter.h"
#include "ValorGameState.h"
#include "Player/ValorPlayerState.h"
#include "ValorMatchGameMode.h"
#include "Weapons/ValorWeaponBase.h"
#include "Sound/SoundBase.h"

void AValorPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	// FPS 기본 입력 모드는 게임 전용으로 유지한다.
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;
}

void AValorPlayerController::AcknowledgePossession(APawn* InPawn)
{
	Super::AcknowledgePossession(InPawn);

	// 새 캐릭터로 부활: 이전 생의 사망 화면·피격 표시를 지운다.
	// 원격 클라(ClientRestart)와 리슨 호스트(OnPossess → 로컬 ClientRestart) 모두 이 함수를 거친다.
	ClearDeathFeedback();
}

void AValorPlayerController::ClientNotifyHitConfirmed_Implementation(const FValorHitConfirm& HitConfirm)
{
	LastHitConfirm = HitConfirm;
	LastHitConfirmTime = GetLocalTime();

	USoundBase* Sound = HitConfirm.bKilled && KillConfirmSound ? KillConfirmSound.Get()
		: (HitConfirm.HitZone == EValorHitZone::Head && HeadshotConfirmSound ? HeadshotConfirmSound.Get() : HitConfirmSound.Get());
	if (Sound)
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}
}

void AValorPlayerController::ClientNotifyDamageTaken_Implementation(FVector_NetQuantize SourceLocation, float Damage)
{
	FDamageIndicator& Indicator = DamageIndicators.AddDefaulted_GetRef();
	Indicator.SourceLocation = SourceLocation;
	Indicator.LocalTime = GetLocalTime();
	Indicator.Damage = Damage;

	if (DamageIndicators.Num() > MaxDamageIndicators)
	{
		DamageIndicators.RemoveAt(0, DamageIndicators.Num() - MaxDamageIndicators);
	}
}

void AValorPlayerController::ClientNotifyKilled_Implementation(const FValorDeathInfo& DeathInfo)
{
	LastDeathInfo = DeathInfo;
	bHasDeathInfo = true;
}

double AValorPlayerController::GetLocalTime() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void AValorPlayerController::ClearDeathFeedback()
{
	bHasDeathInfo = false;
	DamageIndicators.Reset();
}

void AValorPlayerController::ToggleBuyMenu()
{
	SetBuyMenuOpen(!bBuyMenuOpen);
}

void AValorPlayerController::SetBuyMenuOpen(bool bOpen)
{
	if (!IsLocalController())
	{
		return;
	}

	// 구매 단계에 살아 있을 때만 연다(닫기는 언제나 된다).
	if (bOpen)
	{
		const AValorGameState* ValorGameState = GetWorld() ? GetWorld()->GetGameState<AValorGameState>() : nullptr;
		const AValorCharacter* PossessedCharacter = Cast<AValorCharacter>(GetPawn());
		if (!ValorGameState || !ValorGameState->IsBuyPhase() || !PossessedCharacter || !PossessedCharacter->IsAlive())
		{
			return;
		}
	}

	if (bBuyMenuOpen == bOpen)
	{
		return;
	}

	bBuyMenuOpen = bOpen;

	// 메뉴는 캔버스 HUD의 클릭 영역(HitBox)으로 고른다: 커서 + 클릭 이벤트를 켜고, 마우스가 시점을 돌리지 않게 한다.
	bShowMouseCursor = bOpen;
	bEnableClickEvents = bOpen;
	if (bOpen)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
		SetInputMode(InputMode);
		SetIgnoreLookInput(true);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		ResetIgnoreLookInput();
	}
}

void AValorPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// 구매 단계가 끝나거나 죽으면 메뉴를 닫는다.
	if (bBuyMenuOpen)
	{
		const AValorGameState* ValorGameState = GetWorld() ? GetWorld()->GetGameState<AValorGameState>() : nullptr;
		const AValorCharacter* PossessedCharacter = Cast<AValorCharacter>(GetPawn());
		if (!ValorGameState || !ValorGameState->IsBuyPhase() || !PossessedCharacter || !PossessedCharacter->IsAlive())
		{
			SetBuyMenuOpen(false);
		}
	}
}

void AValorPlayerController::ServerBuyWeapon_Implementation(TSubclassOf<AValorWeaponBase> WeaponClass)
{
	if (AValorMatchGameMode* MatchGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AValorMatchGameMode>() : nullptr)
	{
		MatchGameMode->TryBuyWeapon(this, WeaponClass);
	}
}

void AValorPlayerController::ServerSellWeapon_Implementation(TSubclassOf<AValorWeaponBase> WeaponClass)
{
	if (AValorMatchGameMode* MatchGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AValorMatchGameMode>() : nullptr)
	{
		MatchGameMode->TrySellWeapon(this, WeaponClass);
	}
}

void AValorPlayerController::ServerBuyShield_Implementation(int32 ShieldIndex)
{
	if (AValorMatchGameMode* MatchGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AValorMatchGameMode>() : nullptr)
	{
		MatchGameMode->TryBuyShield(this, ShieldIndex);
	}
}

void AValorPlayerController::ServerSellShield_Implementation(int32 ShieldIndex)
{
	if (AValorMatchGameMode* MatchGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AValorMatchGameMode>() : nullptr)
	{
		MatchGameMode->TrySellShield(this, ShieldIndex);
	}
}

void AValorPlayerController::ValorCredits(int32 Amount)
{
	ServerDebugSetCredits(Amount);
}

void AValorPlayerController::ValorSkipPhase()
{
	ServerDebugSkipPhase();
}

void AValorPlayerController::ServerDebugSetCredits_Implementation(int32 Amount)
{
#if !UE_BUILD_SHIPPING
	if (AValorPlayerState* ValorPlayerState = GetPlayerState<AValorPlayerState>())
	{
		ValorPlayerState->SetCredits(Amount);
	}
#endif
}

void AValorPlayerController::ServerDebugSkipPhase_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (AValorMatchGameMode* MatchGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AValorMatchGameMode>() : nullptr)
	{
		MatchGameMode->DebugSkipPhase();
	}
#endif
}

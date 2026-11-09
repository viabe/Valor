#include "ValorPlayerController.h"

#include "Kismet/GameplayStatics.h"
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

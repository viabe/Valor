#include "ValorPlayerController.h"

#include "Kismet/GameplayStatics.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
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

	// 새 캐릭터로 부활: 이전 생의 사망 화면·피격 표시를 지우고 관전을 끝낸다.
	// 원격 클라(ClientRestart)와 리슨 호스트(OnPossess → 로컬 ClientRestart) 모두 이 함수를 거친다.
	ClearDeathFeedback();
	SpectateStartTime = -1.0;
	StopSpectating();
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

	// 라운드제(부활 없음)에서는 잠시 시체 시점을 보여 준 뒤 아군 관전으로 넘어간다.
	const AValorGameState* ValorGameState = GetWorld() ? GetWorld()->GetGameState<AValorGameState>() : nullptr;
	if (DeathInfo.RespawnServerTime < 0.0f && ValorGameState && ValorGameState->UsesRounds())
	{
		SpectateStartTime = GetLocalTime() + DeathCamSeconds;
	}
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
	ApplyCursorMode(bOpen || bAgentSelectOpen);
}

void AValorPlayerController::ApplyCursorMode(bool bCursor)
{
	// 메뉴는 캔버스 HUD의 클릭 영역(HitBox)으로 고른다: 커서 + 클릭 이벤트를 켜고, 마우스가 시점을 돌리지 않게 한다.
	if (bShowMouseCursor == bCursor)
	{
		return;
	}

	bShowMouseCursor = bCursor;
	bEnableClickEvents = bCursor;
	if (bCursor)
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

	// 관전: 예약 시각이 되면 시작, 보던 아군이 죽으면 다음 아군, 내가 다시 살아 있으면(새 라운드) 끝낸다.
	const AValorCharacter* OwnCharacter = Cast<AValorCharacter>(GetPawn());
	if (IsLocalController())
	{
		if (OwnCharacter && OwnCharacter->IsAlive())
		{
			SpectateStartTime = -1.0;
			if (IsSpectating())
			{
				StopSpectating();
			}
		}
		else if (SpectateStartTime >= 0.0 && GetLocalTime() >= SpectateStartTime)
		{
			SpectateStartTime = -1.0;
			CycleSpectate(1);
		}
		else if (IsSpectating())
		{
			const AValorCharacter* Target = SpectateTarget.Get();
			if (!Target || !Target->IsAlive())
			{
				CycleSpectate(1);
			}
			else
			{
				UpdateSpectateCamera(DeltaTime, false);
			}
		}
	}

	// 요원 선택 창: 선택 단계 동안 열어 둔다(확정한 뒤에도 팀 선택을 보도록 단계가 끝날 때까지).
	if (IsLocalController())
	{
		const AValorGameState* SelectGameState = GetWorld() ? GetWorld()->GetGameState<AValorGameState>() : nullptr;
		const bool bShouldOpen = SelectGameState && SelectGameState->UsesRounds() && SelectGameState->GetRoundPhase() == EValorRoundPhase::AgentSelect;
		if (bShouldOpen != bAgentSelectOpen)
		{
			bAgentSelectOpen = bShouldOpen;
			ApplyCursorMode(bAgentSelectOpen || bBuyMenuOpen);
		}
	}

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

// ======================================================================================
// 개발용: 봇 추가, 즉사
// ======================================================================================

void AValorPlayerController::ValorAddBot(const FString& Team)
{
	const FString Upper = Team.ToUpper();
	const EValorTeam RequestedTeam = Upper == TEXT("A") ? EValorTeam::TeamA : (Upper == TEXT("B") ? EValorTeam::TeamB : EValorTeam::None);
	ServerDebugAddBot(static_cast<uint8>(RequestedTeam));
}

void AValorPlayerController::ValorKillMe()
{
	ServerDebugKillMe();
}

void AValorPlayerController::ServerDebugAddBot_Implementation(uint8 Team)
{
#if !UE_BUILD_SHIPPING
	if (AValorMatchGameMode* MatchGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AValorMatchGameMode>() : nullptr)
	{
		MatchGameMode->AddBot(static_cast<EValorTeam>(Team));
	}
#endif
}

void AValorPlayerController::ValorTagMe(const FString& IntervalSeconds, const FString& StrengthPercent)
{
	const bool bOff = IntervalSeconds.Equals(TEXT("off"), ESearchCase::IgnoreCase) || (!IntervalSeconds.IsEmpty() && FCString::Atof(*IntervalSeconds) <= 0.0f);
	const float Interval = bOff ? 0.0f : (IntervalSeconds.IsEmpty() ? 1.5f : FCString::Atof(*IntervalSeconds));
	const float Strength = StrengthPercent.IsEmpty() ? 0.725f : FCString::Atof(*StrengthPercent) / 100.0f;
	bDebugTagLoopRequested = !bOff;
	ServerDebugTagLoop(Interval, Strength);
}

void AValorPlayerController::ServerDebugTagLoop_Implementation(float IntervalSeconds, float Strength)
{
#if !UE_BUILD_SHIPPING
	GetWorldTimerManager().ClearTimer(DebugTagTimerHandle);
	if (IntervalSeconds <= 0.0f)
	{
		return;
	}

	// 실제 총에 맞을 때와 같은 서버 경로(AValorCharacter::ApplyBulletTag)로 건다. 피해는 주지 않는다.
	DebugTagStrength = FMath::Clamp(Strength, 0.0f, 1.0f);
	DebugApplyTagToSelf();
	GetWorldTimerManager().SetTimer(DebugTagTimerHandle, this, &AValorPlayerController::DebugApplyTagToSelf, FMath::Max(0.3f, IntervalSeconds), true);
#endif
}

void AValorPlayerController::DebugApplyTagToSelf()
{
	if (AValorCharacter* PossessedCharacter = Cast<AValorCharacter>(GetPawn()))
	{
		PossessedCharacter->ApplyBulletTag(DebugTagStrength);
	}
}

void AValorPlayerController::ServerDebugKillMe_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (AValorCharacter* PossessedCharacter = Cast<AValorCharacter>(GetPawn()))
	{
		PossessedCharacter->KillByEnvironment(nullptr);
	}
#endif
}

// ======================================================================================
// 관전
// ======================================================================================

void AValorPlayerController::GetSpectateCandidates(TArray<AValorCharacter*>& OutCandidates) const
{
	OutCandidates.Reset();
	const AValorPlayerState* OwnState = GetPlayerState<AValorPlayerState>();
	if (!OwnState || OwnState->GetTeam() == EValorTeam::None)
	{
		return;
	}

	for (TActorIterator<AValorCharacter> It(GetWorld()); It; ++It)
	{
		const AValorPlayerState* CandidateState = It->GetPlayerState<AValorPlayerState>();
		if (*It != GetPawn() && It->IsAlive() && CandidateState && CandidateState->GetTeam() == OwnState->GetTeam())
		{
			OutCandidates.Add(*It);
		}
	}

	// 순서를 고정한다(PlayerId 순). 서버·클라에서 같은 순서일 필요는 없지만 같은 화면에서 왼쪽·오른쪽 클릭 순서가 흔들리지 않아야 한다.
	OutCandidates.Sort([](const AValorCharacter& A, const AValorCharacter& B)
	{
		return A.GetPlayerState()->GetPlayerId() < B.GetPlayerState()->GetPlayerId();
	});
}

void AValorPlayerController::SpectateNext()
{
	CycleSpectate(1);
}

void AValorPlayerController::SpectatePrevious()
{
	CycleSpectate(-1);
}

void AValorPlayerController::CycleSpectate(int32 Direction)
{
	TArray<AValorCharacter*> Candidates;
	GetSpectateCandidates(Candidates);
	if (Candidates.Num() == 0)
	{
		// 볼 아군이 없다: 시체 시점으로 돌아간다(팀 전멸이면 곧 라운드가 끝난다).
		StopSpectating();
		return;
	}

	const int32 CurrentIndex = Candidates.IndexOfByKey(SpectateTarget.Get());
	const int32 NextIndex = CurrentIndex == INDEX_NONE ? 0 : (CurrentIndex + Direction + Candidates.Num()) % Candidates.Num();
	SetSpectateTarget(Candidates[NextIndex]);
}

void AValorPlayerController::SetSpectateTarget(AValorCharacter* NewTarget)
{
	if (!NewTarget || !IsLocalController())
	{
		return;
	}

	if (!SpectateCamera)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpectateCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, SpawnParameters);
		if (SpectateCamera)
		{
			SpectateCamera->GetCameraComponent()->SetFieldOfView(103.0f);
			SpectateCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		}
	}

	const bool bTargetChanged = SpectateTarget.Get() != NewTarget;
	SpectateTarget = NewTarget;
	if (SpectateCamera)
	{
		UpdateSpectateCamera(0.0f, bTargetChanged);
		SetViewTargetWithBlend(SpectateCamera, 0.0f);
	}
}

void AValorPlayerController::StopSpectating()
{
	SpectateTarget = nullptr;
	if (SpectateCamera)
	{
		SpectateCamera->Destroy();
		SpectateCamera = nullptr;
	}

	if (IsLocalController() && GetPawn())
	{
		SetViewTargetWithBlend(GetPawn(), 0.0f);
	}
}

void AValorPlayerController::UpdateSpectateCamera(float DeltaTime, bool bSnap)
{
	const AValorCharacter* Target = SpectateTarget.Get();
	if (!Target || !SpectateCamera)
	{
		return;
	}

	// 아군의 눈 위치와 시선(다른 머신에서는 복제된 조준 피치 + 몸 방향)으로 어깨 너머 카메라를 둔다. 벽에 막히면 당긴다.
	const FVector Pivot = Target->GetPawnViewLocation();
	const FRotator AimRotation = Target->GetBaseAimRotation();
	const FRotationMatrix AimAxes(AimRotation);
	const FVector Desired = Pivot - (AimAxes.GetUnitAxis(EAxis::X) * SpectateArmLength) + (AimAxes.GetUnitAxis(EAxis::Y) * SpectateShoulderOffset) + FVector(0.0f, 0.0f, 15.0f);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ValorSpectateCamera), false);
	for (TActorIterator<AValorCharacter> It(GetWorld()); It; ++It)
	{
		QueryParams.AddIgnoredActor(*It);
	}
	FHitResult Hit;
	FVector CameraLocation = Desired;
	if (GetWorld()->SweepSingleByChannel(Hit, Pivot, Desired, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(12.0f), QueryParams))
	{
		CameraLocation = Hit.Location;
	}

	// 원격 캐릭터 위치는 복제 주기마다 조금씩 튀므로 위치만 살짝 부드럽게 따라간다(시선은 그대로).
	const FVector Smoothed = (bSnap || DeltaTime <= 0.0f) ? CameraLocation : FMath::VInterpTo(SpectateCamera->GetActorLocation(), CameraLocation, DeltaTime, 25.0f);
	SpectateCamera->SetActorLocationAndRotation(Smoothed, AimRotation);
}

// ======================================================================================
// 스킬 상점, 궁극기 포인트(개발용)
// ======================================================================================

void AValorPlayerController::ServerBuyAbility_Implementation(uint8 Slot)
{
	if (AValorMatchGameMode* MatchGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AValorMatchGameMode>() : nullptr)
	{
		MatchGameMode->TryBuyAbility(this, static_cast<EValorAbilitySlot>(FMath::Min<uint8>(Slot, ValorAbilityRules::SlotCount - 1)));
	}
}

void AValorPlayerController::ServerSellAbility_Implementation(uint8 Slot)
{
	if (AValorMatchGameMode* MatchGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AValorMatchGameMode>() : nullptr)
	{
		MatchGameMode->TrySellAbility(this, static_cast<EValorAbilitySlot>(FMath::Min<uint8>(Slot, ValorAbilityRules::SlotCount - 1)));
	}
}

void AValorPlayerController::ValorUltPoints(int32 Points)
{
	ServerDebugSetUltimatePoints(Points);
}

void AValorPlayerController::ServerDebugSetUltimatePoints_Implementation(int32 Points)
{
#if !UE_BUILD_SHIPPING
	if (AValorMatchGameMode* MatchGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AValorMatchGameMode>() : nullptr)
	{
		MatchGameMode->DebugSetUltimatePoints(this, Points);
	}
#endif
}

void AValorPlayerController::ValorAgent(const FString& AgentName)
{
	ServerDebugSetAgent(AgentName);
}

void AValorPlayerController::ServerDebugSetAgent_Implementation(const FString& AgentName)
{
#if !UE_BUILD_SHIPPING
	if (AValorMatchGameMode* MatchGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AValorMatchGameMode>() : nullptr)
	{
		MatchGameMode->SetAgentByName(this, AgentName);
	}
#endif
}

void AValorPlayerController::ClientFlashed_Implementation(float Seconds)
{
	// 이미 더 센 눈부심이 남아 있으면 그대로 둔다.
	const double Now = GetLocalTime();
	const double CurrentEnd = FlashStartTime + FlashSeconds;
	if (Now + Seconds > CurrentEnd)
	{
		FlashStartTime = Now;
		FlashSeconds = Seconds;
	}
}

float AValorPlayerController::GetFlashAlpha() const
{
	if (FlashSeconds <= 0.0f || FlashStartTime < 0.0)
	{
		return 0.0f;
	}

	const float Elapsed = static_cast<float>(GetLocalTime() - FlashStartTime);
	if (Elapsed >= FlashSeconds)
	{
		return 0.0f;
	}
	const float HoldSeconds = FlashSeconds * 0.7f;
	return Elapsed <= HoldSeconds ? 1.0f : 1.0f - ((Elapsed - HoldSeconds) / (FlashSeconds - HoldSeconds));
}

void AValorPlayerController::ServerSelectAgent_Implementation(TSubclassOf<UValorAgentDefinition> AgentClass)
{
	if (AValorMatchGameMode* MatchGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AValorMatchGameMode>() : nullptr)
	{
		MatchGameMode->TrySelectAgent(this, AgentClass);
	}
}

void AValorPlayerController::ServerLockAgent_Implementation()
{
	if (AValorMatchGameMode* MatchGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AValorMatchGameMode>() : nullptr)
	{
		MatchGameMode->TryLockAgent(this);
	}
}

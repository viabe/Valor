#include "ValorWeaponBase.h"

#include "Components/SceneComponent.h"
#include "Interfaces/ValorWeaponOwnerInterface.h"
#include "Net/UnrealNetwork.h"
#include "Components/SkeletalMeshComponent.h"
#include "ValorCharacter.h"

AValorWeaponBase::AValorWeaponBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bNetUseOwnerRelevancy = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(Root);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	InitializeFallbackConfig();
	CurrentMagazineAmmo = FallbackWeaponConfig.MagazineSize;
	CurrentReserveAmmo = FallbackWeaponConfig.MaxReserveAmmo;
}

void AValorWeaponBase::BeginPlay()
{
	Super::BeginPlay();

	const FValorWeaponConfig& Config = GetWeaponConfig();
	CurrentMagazineAmmo = FMath::Clamp(CurrentMagazineAmmo, 0, Config.MagazineSize);
	CurrentReserveAmmo = FMath::Clamp(CurrentReserveAmmo, 0, Config.MaxReserveAmmo);
	WeaponRandomSeed = GetUniqueID() * 31u + 17u;

	ResolveRecoilProfile();
}

void AValorWeaponBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValorWeaponBase, OwningCharacter);
	DOREPLIFETIME(AValorWeaponBase, CurrentMagazineAmmo);
	DOREPLIFETIME(AValorWeaponBase, CurrentReserveAmmo);
}

void AValorWeaponBase::OnEquippedBy(AValorCharacter* NewOwnerCharacter)
{
	OwningCharacter = NewOwnerCharacter;
	SetOwner(NewOwnerCharacter);
	SetInstigator(NewOwnerCharacter);

	if (!NewOwnerCharacter)
	{
		return;
	}

	if (IValorWeaponOwnerInterface* WeaponOwner = Cast<IValorWeaponOwnerInterface>(NewOwnerCharacter))
	{
		if (USceneComponent* AttachComponent = WeaponOwner->GetWeaponAttachComponent())
		{
			// 캐릭터 손 소켓은 액터 루트 기준점으로만 사용하고, 실제 총 위치는 무기 메시의 그립 소켓을 역정렬해 맞춘다.
			AttachToComponent(AttachComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, WeaponOwner->GetWeaponAttachSocketName());
			RefreshWeaponMeshAlignment();
		}
	}
}

void AValorWeaponBase::OnUnequipped()
{
	if (WeaponMesh)
	{
		WeaponMesh->SetRelativeTransform(FTransform::Identity);
	}

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	OwningCharacter = nullptr;
	SetOwner(nullptr);
	SetInstigator(nullptr);
}

const FValorWeaponConfig& AValorWeaponBase::GetWeaponConfig() const
{
	return WeaponDataAsset ? WeaponDataAsset->WeaponConfig : FallbackWeaponConfig;
}

bool AValorWeaponBase::CanFire(float ServerWorldTimeSeconds) const
{
	const FValorWeaponConfig& Config = GetWeaponConfig();
	if (CurrentMagazineAmmo <= 0)
	{
		return false;
	}

	return (ServerWorldTimeSeconds - LastServerFireWorldTime) + KINDA_SMALL_NUMBER >= Config.TimeBetweenShots;
}

bool AValorWeaponBase::PrepareAndConsumeShot(float ServerWorldTimeSeconds, bool bIsADS, float MovementAlpha, bool bIsWalking, bool bIsCrouched, FValorComputedShotData& OutShotData)
{
	if (!CanFire(ServerWorldTimeSeconds))
	{
		return false;
	}

	RefreshSprayState(ServerWorldTimeSeconds);

	const FValorWeaponConfig& Config = GetWeaponConfig();
	const int32 ShotIndex = CurrentSprayShotCount;

	float SpreadAngle = Config.BaseFirstShotSpreadDegrees + (Config.AdditionalSpreadPerShotDegrees * ShotIndex);
	SpreadAngle += Config.MovementSpreadDegrees * FMath::Clamp(MovementAlpha, 0.0f, 1.0f);

	if (bIsWalking)
	{
		SpreadAngle += Config.WalkingSpreadDegrees;
	}

	if (bIsCrouched)
	{
		SpreadAngle *= Config.CrouchSpreadMultiplier;
	}

	if (bIsADS)
	{
		SpreadAngle *= Config.ADSSpreadMultiplier;
	}

	SpreadAngle = FMath::Clamp(SpreadAngle, 0.0f, Config.MaxSpreadDegrees);

	// 반동은 "결정적 패턴 구간 → 랜덤 지속 구간" 순으로 이번 발의 '증분'을 구한다(발로란트 동일).
	// 패턴 인덱스 안에서는 데이터로 정의된 정확한 증분을 쓰고, 패턴을 다 쓰면 좌우 랜덤으로 넘어간다.
	float StepPitch = 0.0f;
	float StepYaw = 0.0f;
	const FValorRecoilProfile& RecoilProfile = ResolvedRecoilProfile;
	if (RecoilProfile.Pattern.IsValidIndex(ShotIndex))
	{
		const FValorRecoilStep& RecoilStep = RecoilProfile.Pattern[ShotIndex];
		StepPitch = RecoilStep.PitchKick;
		StepYaw = RecoilStep.YawKick;
		SpreadAngle += RecoilStep.AdditionalSpread;
	}
	else if (RecoilProfile.Pattern.Num() > 0)
	{
		// ShotIndex로 시드를 고정해 같은 발사 순서면 항상 같은 결과가 나오게 한다(서버/클라 결정성 유지).
		FRandomStream Stream(WeaponRandomSeed + ShotIndex * 13u);
		StepPitch = Stream.FRandRange(RecoilProfile.SustainedPitchMin, RecoilProfile.SustainedPitchMax);
		StepYaw = Stream.FRandRange(RecoilProfile.SustainedYawMin, RecoilProfile.SustainedYawMax);
	}

	// 앉기/조준 시 반동 증분을 줄인다(발로란트처럼 더 안정적인 사격).
	if (bIsCrouched)
	{
		StepPitch *= Config.CrouchRecoilMultiplier;
		StepYaw *= Config.CrouchRecoilMultiplier;
	}

	if (bIsADS)
	{
		StepPitch *= Config.ADSRecoilMultiplier;
		StepYaw *= Config.ADSRecoilMultiplier;
	}

	OutShotData.ShotIndex = ShotIndex;
	OutShotData.SpreadAngleDegrees = SpreadAngle;
	// 핵심(발로란트 방식): 반동은 카메라를 밀지 않는다. 대신 이번 발은 '지금까지 누적된' 오프셋 위치에
	// 떨어지고(조준점 대비 위/옆으로 벌어짐), 그 뒤 이번 증분을 누적해 다음 발이 더 벌어지게 한다.
	// 첫 발은 누적 0이라 조준점에 정확히 맞는다. 플레이어는 마우스로 끌어내려 이 오프셋을 보정한다.
	OutShotData.RecoilPitchDegrees = AccumulatedRecoilPitch;
	OutShotData.RecoilYawDegrees = AccumulatedRecoilYaw;
	// 이번 발 증분은 별도로 남긴다 → 로컬 화면의 시각적 뷰 펀치 크기로 쓰인다(탄도와 무관).
	OutShotData.RecoilStepPitchDegrees = StepPitch;
	OutShotData.RecoilStepYawDegrees = StepYaw;
	OutShotData.RandomSeed = WeaponRandomSeed + ShotIndex * 23u;

	AccumulatedRecoilPitch += StepPitch;
	AccumulatedRecoilYaw += StepYaw;

	LastServerFireWorldTime = ServerWorldTimeSeconds;
	CurrentSprayShotCount++;
	CurrentMagazineAmmo = FMath::Max(0, CurrentMagazineAmmo - 1);
	return true;
}

bool AValorWeaponBase::CanReload() const
{
	const FValorWeaponConfig& Config = GetWeaponConfig();
	return CurrentMagazineAmmo < Config.MagazineSize && CurrentReserveAmmo > 0;
}

void AValorWeaponBase::ReloadFromReserve()
{
	if (!CanReload())
	{
		return;
	}

	const FValorWeaponConfig& Config = GetWeaponConfig();
	const int32 AmmoNeeded = Config.MagazineSize - CurrentMagazineAmmo;
	const int32 AmmoToLoad = FMath::Min(AmmoNeeded, CurrentReserveAmmo);

	CurrentMagazineAmmo += AmmoToLoad;
	CurrentReserveAmmo -= AmmoToLoad;
}

float AValorWeaponBase::GetReloadDuration() const
{
	return GetWeaponConfig().ReloadDuration;
}

float AValorWeaponBase::GetShotInterval() const
{
	return GetWeaponConfig().TimeBetweenShots;
}

bool AValorWeaponBase::IsAutomatic() const
{
	return GetWeaponConfig().bAutomatic;
}

float AValorWeaponBase::GetADSFieldOfView() const
{
	return GetWeaponConfig().ADSFieldOfView;
}

float AValorWeaponBase::GetADSInterpSpeed() const
{
	return GetWeaponConfig().ADSInterpSpeed;
}

UAnimMontage* AValorWeaponBase::GetFireMontage() const
{
	return GetWeaponConfig().FireMontage;
}

float AValorWeaponBase::GetFireMontagePlayRate() const
{
	return GetWeaponConfig().FireMontagePlayRate;
}

float AValorWeaponBase::GetTraceDistance() const
{
	return GetWeaponConfig().TraceDistanceCm;
}

EValorWeaponAnimationType AValorWeaponBase::GetWeaponAnimationType() const
{
	return GetWeaponConfig().AnimationType;
}

EValorWallPenetrationTier AValorWeaponBase::GetPenetrationTier() const
{
	return GetWeaponConfig().PenetrationTier;
}

float AValorWeaponBase::GetPenetrationDepth() const
{
	return GetWeaponConfig().PenetrationDepthCm;
}

float AValorWeaponBase::GetPenetrationDamageMultiplier() const
{
	return GetWeaponConfig().PenetrationDamageMultiplier;
}

FVector AValorWeaponBase::ApplySpreadToDirection(const FVector& AimDirection, const FValorComputedShotData& ShotData) const
{
	// 1) 결정적 반동 패턴을 '탄착 오프셋'으로 적용한다. 카메라(조준점)는 그대로 두고 탄만 조준점 대비
	//    위(+Pitch)/오른쪽(+Yaw)으로 벌어진다 → 발로란트처럼 "조준점 고정 + 탄이 스프레이 패턴을 그린다".
	FRotator ShotRotation = AimDirection.Rotation();
	ShotRotation.Pitch = FMath::Clamp(ShotRotation.Pitch + ShotData.RecoilPitchDegrees, -89.0f, 89.0f);
	ShotRotation.Yaw += ShotData.RecoilYawDegrees;
	const FVector RecoiledDirection = ShotRotation.Vector();

	// 2) 그 위에 소량의 랜덤 스프레드를 얹는다(이동/점프 시 커진다). 서 있을 땐 거의 0이라 패턴이 또렷하게 유지된다.
	FRandomStream Stream(ShotData.RandomSeed);
	const float PitchOffset = Stream.FRandRange(-ShotData.SpreadAngleDegrees, ShotData.SpreadAngleDegrees);
	const float YawOffset = Stream.FRandRange(-ShotData.SpreadAngleDegrees, ShotData.SpreadAngleDegrees);
	return FRotator(PitchOffset, YawOffset, 0.0f).RotateVector(RecoiledDirection).GetSafeNormal();
}

float AValorWeaponBase::ComputeDamage(float DistanceCm, EValorHitZone HitZone) const
{
	const TArray<FValorDamageRangeStep>& DamageRanges = GetWeaponConfig().DamageRanges;
	if (DamageRanges.Num() == 0)
	{
		return 0.0f;
	}

	const FValorDamageRangeStep* SelectedRange = &DamageRanges.Last();
	for (const FValorDamageRangeStep& RangeStep : DamageRanges)
	{
		if (DistanceCm <= RangeStep.MaxDistanceCm)
		{
			SelectedRange = &RangeStep;
			break;
		}
	}

	switch (HitZone)
	{
	case EValorHitZone::Head:
		return SelectedRange->HeadDamage;
	case EValorHitZone::Leg:
		return SelectedRange->LegDamage;
	case EValorHitZone::Body:
	default:
		return SelectedRange->BodyDamage;
	}
}

void AValorWeaponBase::InitializeFallbackConfig()
{
	FallbackWeaponConfig.DisplayName = FText::FromString(TEXT("Vandal"));

	// 폴백(데이터 자산이 없을 때)에도 실제 밴달 스프레이가 나오도록 기본 패턴을 채워 둔다.
	BuildDefaultVandalPattern(FallbackWeaponConfig.RecoilProfile.Pattern);

	FallbackWeaponConfig.DamageRanges =
	{
		{1500.0f, 160.0f, 40.0f, 34.0f},
		{3000.0f, 150.0f, 37.0f, 31.0f},
		{50000.0f, 140.0f, 34.0f, 28.0f}
	};
}

void AValorWeaponBase::BuildDefaultVandalPattern(TArray<FValorRecoilStep>& OutPattern)
{
	// 발로란트 밴달(탄창 25발) 스프레이를 재현한 결정적 패턴이다. 배열 길이(25) == 탄창(25).
	// 값의 의미: {PitchKick(위로 +), YawKick(오른쪽 +), AdditionalSpread}. 단위는 "그 발을 쏜 뒤" 카메라에 더해지는 도(degree)다.
	// (첫 발은 킥이 적용되기 전에 발사되므로 항상 정확 → 탭/버스트가 정확한 발로란트 특성과 일치.)
	//
	// 실제 밴달 스프레이의 정설(웹 자료 교차 확인)은 "매우 긴 수직 줄기(⊥) → 상단에서 번개(지그재그) 모양"이다:
	//  - 앞 ~10발: 거의 수직으로 상승(이 구간을 마우스로 끌어내려 잡는다). 좌우 편차는 미미.
	//  - 10발 이후: 수직 상승은 멈추고(plateau) 좌우로 크게 흔들린다. 실제로는 "왼쪽으로 살짝 → 오른쪽으로 크게"
	//    스윙하는 균형 잡힌 지그재그이며, 실 게임에선 이 후반부가 semi-random(스프레이마다 좌우가 조금씩 달라짐)이다.
	// 이전 패턴이 "모양이 다르다"고 느껴진 이유: 상단이 좌측(-4도)으로만 크게 치우치고 우측 복귀(+1.5도)가 약해
	//   한쪽 갈고리처럼 보였다. 이번엔 좌(-3.2도)/우(+3.25도)를 균형 있게 잡아 번개 모양 지그재그로 맞춘다.
	// (후반부를 매 스프레이 랜덤하게 만들고 싶으면, Pattern 길이를 ~13개로 줄이고 나머지를 RecoilProfile의
	//  Sustained* 랜덤 구간에 맡기면 된다 → PrepareAndConsumeShot의 else 분기. 지금은 결정적 패턴으로 모양을 고정한다.)
	OutPattern =
	{
		// (1) 수직 줄기: 초탄은 촘촘, 3~5발째 상승 최고조. 좌우 편차는 거의 없음(아주 미세한 우측 드리프트)
		{0.90f,  0.00f, 0.00f},   // 0
		{1.20f,  0.00f, 0.00f},   // 1
		{1.30f,  0.05f, 0.00f},   // 2
		{1.30f,  0.10f, 0.00f},   // 3
		{1.20f,  0.10f, 0.00f},   // 4
		{1.10f,  0.05f, 0.00f},   // 5
		{0.90f,  0.00f, 0.00f},   // 6
		// (2) 상승 감쇠 + 좌측으로 완만히 기울며 상단 훅 진입(수직은 거의 마무리)
		{0.70f, -0.15f, 0.00f},   // 7
		{0.55f, -0.25f, 0.00f},   // 8
		{0.40f, -0.40f, 0.00f},   // 9
		// (3) plateau(피치 ~0) + 좌측 스윙 (번개의 왼쪽 꺾임)
		{0.30f, -0.70f, 0.00f},   // 10
		{0.20f, -0.90f, 0.00f},   // 11
		{0.12f, -0.80f, 0.00f},   // 12
		{0.08f, -0.30f, 0.00f},   // 13
		// (4) 중앙을 가로질러 오른쪽으로 크게 되돌아오는 스윙 (번개의 오른쪽 꺾임, 좌우 균형)
		{0.05f,  0.60f, 0.00f},   // 14  (좌측 최대 -3.2도 부근)
		{0.03f,  1.30f, 0.00f},   // 15
		{0.03f,  1.60f, 0.00f},   // 16
		{0.02f,  1.55f, 0.00f},   // 17
		{0.02f,  1.05f, 0.00f},   // 18
		{0.02f,  0.35f, 0.00f},   // 19
		// (5) 다시 왼쪽으로 흔들며 마무리
		{0.02f, -0.65f, 0.00f},   // 20  (우측 최대 +3.25도 부근)
		{0.02f, -1.25f, 0.00f},   // 21
		{0.02f, -1.20f, 0.00f},   // 22
		{0.02f, -0.95f, 0.00f},   // 23
		{0.02f, -0.50f, 0.00f}    // 24
	};
}

void AValorWeaponBase::ResolveRecoilProfile()
{
	// 데이터 자산(또는 폴백)의 프로파일을 그대로 가져온다(회복/클램프 같은 스칼라 값은 그대로 유지).
	ResolvedRecoilProfile = GetWeaponConfig().RecoilProfile;

	// 데이터 자산이 패턴을 채워뒀다면 그 값이 최우선이다(데이터 주도 설계).
	// 비어 있으면 WeaponId에 맞는 코드 기본 패턴을 주입해, 데이터 자산을 아직 안 채워도 동작하게 한다.
	if (ResolvedRecoilProfile.Pattern.Num() == 0)
	{
		BuildDefaultPatternForWeapon(GetWeaponConfig().WeaponId, ResolvedRecoilProfile.Pattern);
	}
}

void AValorWeaponBase::BuildDefaultPatternForWeapon(FName WeaponId, TArray<FValorRecoilStep>& OutPattern)
{
	// WeaponId로 총별 기본 패턴을 고른다. 새 총기를 추가하면 여기에 분기를 추가하면 된다.
	// (예) else if (WeaponId == TEXT("Phantom")) { BuildDefaultPhantomPattern(OutPattern); }
	// 현재 구현된 총기는 밴달뿐이므로, 알 수 없는 WeaponId도 안전하게 밴달(라이플 기본)로 동작시킨다.
	// 다른 총기를 추가할 때 데이터 자산 패턴을 채우지 않으면 이 기본값이 쓰이므로, 반드시 분기를 추가하거나
	// 데이터 자산을 채워야 의도한 반동이 나온다.
	BuildDefaultVandalPattern(OutPattern);
	(void)WeaponId;
}

void AValorWeaponBase::RefreshWeaponMeshAlignment()
{
	if (!WeaponMesh)
	{
		return;
	}

	WeaponMesh->SetRelativeTransform(FTransform::Identity);

	const FName GripSocketName = GetWeaponConfig().GripSocketName;
	if (GripSocketName.IsNone() || !WeaponMesh->DoesSocketExist(GripSocketName))
	{
		return;
	}

	const FTransform GripSocketTransform = WeaponMesh->GetSocketTransform(GripSocketName, RTS_Component);
	const FQuat MeshRelativeRotation = GripSocketTransform.GetRotation().Inverse();
	const FVector MeshRelativeLocation = MeshRelativeRotation.RotateVector(-GripSocketTransform.GetLocation());

	// 소켓 정렬은 위치/회전만 보정하고, 무기 원본 스케일은 유지해야 메시가 사라지지 않는다.
	WeaponMesh->SetRelativeLocationAndRotation(MeshRelativeLocation, MeshRelativeRotation);
	WeaponMesh->SetRelativeScale3D(FVector::OneVector);
}

void AValorWeaponBase::RefreshSprayState(float CurrentWorldTimeSeconds)
{
	const FValorWeaponConfig& Config = GetWeaponConfig();
	if ((CurrentWorldTimeSeconds - LastServerFireWorldTime) > Config.SpreadRecoveryDelay)
	{
		// 사격을 멈추면 스프레이가 처음으로 리셋된다: 패턴 인덱스와 누적 반동 오프셋을 모두 0으로 되돌린다.
		CurrentSprayShotCount = 0;
		AccumulatedRecoilPitch = 0.0f;
		AccumulatedRecoilYaw = 0.0f;
	}
}

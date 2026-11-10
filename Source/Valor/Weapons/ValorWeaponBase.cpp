#include "ValorWeaponBase.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/ValorWeaponOwnerInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Guid.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "ValorCharacter.h"
#include "Weapons/ValorSpraySimulation.h"

namespace
{
	// 트레이서/탄흔 에셋이 아직 없을 때 개발 빌드에서 디버그 선/점으로 대신 보여 줄지 여부.
	// 힙파이어 반동은 "탄이 크로스헤어 위로 올라가는" 것으로만 보이므로, 탄착 피드백이 없으면 반동이 없는 것처럼 느껴진다.
	TAutoConsoleVariable<int32> CVarValorDrawShotDebug(
		TEXT("Valor.Debug.DrawShots"),
		1,
		TEXT("1이면 트레이서/탄흔 에셋이 비어 있을 때 디버그 선(트레이서)과 점(탄흔)으로 대신 표시한다. 0이면 끈다."),
		ECVF_Default);

	// 힙파이어 카메라가 스프레이 패턴을 따라가는 비율을 데이터 자산 대신 임시로 덮어쓴다(비교 테스트용, 음수면 사용 안 함).
	// 예) 0.5 = 발로란트 힙파이어(영상 측정 기본값), 1 = 화면이 패턴을 끝까지 따라 올라감(탄이 크로스헤어에 맺힘), 0 = 화면 고정.
	TAutoConsoleVariable<float> CVarValorHipCameraFollowOverride(
		TEXT("Valor.Debug.HipCameraFollow"),
		-1.0f,
		TEXT("0~1이면 힙파이어 CameraRecoilFollowRatio를 이 값으로 덮어쓴다(비교용). 음수면 데이터 자산 값을 쓴다."),
		ECVF_Cheat);

#if !UE_BUILD_SHIPPING
	// 개발 중 설정 실수 확인: 무기 BP 이름에 총 이름이 들어 있는데 데이터 에셋은 다른 총인 경우(예: BP_STINGER가 DA_Vandal을 가리킴).
	// 이 경우 총이 엉뚱한 수치(점사 없는 연사 등)로 동작하는데 화면만 봐서는 원인을 알기 어려워, 로그와 화면에 바로 알린다.
	void WarnIfWeaponDataLooksMismatched(const AValorWeaponBase& Weapon)
	{
		static const TCHAR* const KnownWeaponNames[] =
		{
			TEXT("Classic"), TEXT("Shorty"), TEXT("Frenzy"), TEXT("Ghost"), TEXT("Bandit"), TEXT("Sheriff"),
			TEXT("Stinger"), TEXT("Spectre"), TEXT("Bucky"), TEXT("Judge"), TEXT("Bulldog"), TEXT("Guardian"),
			TEXT("Phantom"), TEXT("Vandal"), TEXT("Warden"), TEXT("Marshal"), TEXT("Outlaw"), TEXT("Operator"),
			TEXT("Ares"), TEXT("Odin")
		};

		const FString ClassName = Weapon.GetClass()->GetName();
		const FName ConfiguredWeaponId = Weapon.GetWeaponConfig().WeaponId;
		for (const TCHAR* KnownWeaponName : KnownWeaponNames)
		{
			if (!ClassName.Contains(KnownWeaponName, ESearchCase::IgnoreCase) || ConfiguredWeaponId == FName(KnownWeaponName))
			{
				continue;
			}

			const FString Message = FString::Printf(
				TEXT("[무기 설정 확인] %s 가 '%s' 데이터로 동작 중 — Weapon Data Asset을 DA_%s로 지정했는지 확인"),
				*ClassName, *ConfiguredWeaponId.ToString(), KnownWeaponName);
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
			if (GEngine && Weapon.GetNetMode() != NM_DedicatedServer)
			{
				GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Orange, Message);
			}
			return;
		}
	}
#endif
}

AValorWeaponBase::AValorWeaponBase()
{
	// Tick은 3인칭 조준 정렬에만 쓴다(원격 캐릭터를 그리는 머신에서 손에 든 총만 켬). 애니메이션이 손 위치를 확정한 뒤에
	// 돌려야 하므로 프레임 마지막 단계(PostUpdateWork)에서 실행한다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	bReplicates = true;
	bNetUseOwnerRelevancy = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(Root);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	CurrentMagazineAmmo = FallbackWeaponConfig.MagazineSize;
	CurrentReserveAmmo = FallbackWeaponConfig.MaxReserveAmmo;

	// 기록이 없는 발은 1발을 쓴 것으로 본다(대부분의 총).
	FMemory::Memset(PredictedRoundsByShot, 1, sizeof(PredictedRoundsByShot));
}

void AValorWeaponBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// 생성자의 WeaponMesh 설정만으로는 부족하다: BP에서 추가한 부품 메시(오퍼레이터 등)는 기본 충돌이 켜진 채 생성된다.
	// 서버·클라 모두 액터 생성 시 한 번 실행되고, 이후 충돌을 다시 켜는 코드는 없다.
	ValorInventory::DisableHeldWeaponCollision(*this);

	// BP에서 액터 루트에 직접 붙인 부품(오퍼레이터 조준경 등)을 총 메시의 자식으로 옮긴다.
	// 왜: 장착 때 총 메시만 손잡이 기준으로 다시 정렬하므로(RefreshWeaponMeshAlignment), 루트에 붙은 부품은 제자리에 남아 총과 떨어진다.
	// 지금(정렬 전)은 BP 뷰포트에서 배치한 그대로이므로, 월드 변환을 유지한 채 옮기면 "총 메시 기준 상대 배치"가 BP와 같아진다.
	if (Root && WeaponMesh)
	{
		const TArray<TObjectPtr<USceneComponent>> RootChildren = Root->GetAttachChildren();
		for (USceneComponent* Child : RootChildren)
		{
			if (Child && Child != WeaponMesh && Child->GetOwner() == this)
			{
				Child->AttachToComponent(WeaponMesh, FAttachmentTransformRules::KeepWorldTransform);
			}
		}
	}
}

void AValorWeaponBase::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		// 탄약은 서버가 실제 데이터 자산 값으로 가득 채운다(생성자 시점엔 데이터 자산을 모르므로 여기서 확정).
		const FValorWeaponConfig& Config = GetWeaponConfig();
		CurrentMagazineAmmo = Config.MagazineSize;
		CurrentReserveAmmo = Config.MaxReserveAmmo;

		// 반동 시드는 서버가 한 번만 만든다. 예측 가능한 값(액터 ID 등)을 쓰면 다른 플레이어도 패턴을 알 수 있으므로 무작위로 만든다.
		const FGuid SeedSource = FGuid::NewGuid();
		RecoilSeed = static_cast<int32>(SeedSource.A ^ SeedSource.B ^ SeedSource.C ^ SeedSource.D);
	}

#if !UE_BUILD_SHIPPING
	WarnIfWeaponDataLooksMismatched(*this);
#endif
}

void AValorWeaponBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValorWeaponBase, OwningCharacter);
	DOREPLIFETIME(AValorWeaponBase, CurrentMagazineAmmo);
	DOREPLIFETIME(AValorWeaponBase, CurrentReserveAmmo);

	// 예측에만 필요한 값이므로 소유자에게만 보낸다(대역폭 절약 + 다른 플레이어에게 반동 난수 비노출).
	DOREPLIFETIME_CONDITION(AValorWeaponBase, RecoilSeed, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AValorWeaponBase, ServerShotCount, COND_OwnerOnly);
}

void AValorWeaponBase::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// 손에 든 총은 머신마다 다른 곳에 붙는다(내 화면 = 1인칭 피벗, 남의 화면 = 3인칭 손). 각 머신이 장착 OnRep에서 직접 붙인다.
	// 엔진 기본값대로 서버의 부착을 복제하면, 도착하는 순간 클라의 OnRep_AttachmentReplication이 로컬 부착을 덮어쓴다.
	//  - 소유 클라: 내 1인칭 총이 숨겨진 3인칭 손으로 끌려가 화면에서 사라진다(도착 순서에 따라 될 때도 안 될 때도 있다).
	//  - 리슨 호스트의 총: 호스트 화면의 1인칭 피벗 부착이 다른 클라로 가서, 그 화면에서는 총이 손이 아니라 머리 앞에 붙는다.
	// 버린 총은 이 액터가 아니라 별도 픽업 액터가 되므로(이 액터는 파괴) 분리 상태를 복제할 필요도 없다.
	DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(AActor, AttachmentReplication, false);
}

void AValorWeaponBase::OnEquippedBy(AValorCharacter* NewOwnerCharacter)
{
	OwningCharacter = NewOwnerCharacter;
	SetOwner(NewOwnerCharacter);
	SetInstigator(NewOwnerCharacter);

	RefreshViewAttachment(NewOwnerCharacter);
}

void AValorWeaponBase::RefreshViewAttachment(AValorCharacter* InViewOwnerCharacter)
{
	if (!InViewOwnerCharacter)
	{
		return;
	}

	// 부착점(1인칭 피벗 또는 3인칭 손 소켓)은 액터 루트 기준점으로만 쓰고, 실제 총 위치는 무기 메시의 손잡이 기준점을 역정렬해 맞춘다.
	// 두 부착점 모두 "손잡이 = 부착점"이라는 같은 규칙이라 정렬 코드는 하나다.
	ViewOwnerCharacter = InViewOwnerCharacter;
	bFirstPersonView = ValorViewmodel::AttachHeldItemToOwnerView(*this, InViewOwnerCharacter);
	RefreshWeaponMeshAlignment();
	RefreshThirdPersonAimTick();
}

void AValorWeaponBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateThirdPersonAim(DeltaSeconds);
}

void AValorWeaponBase::RefreshThirdPersonAimTick()
{
	// 화면이 없는 데디케이티드 서버, 1인칭(내 화면), 집어넣은 총은 정렬할 필요가 없다.
	const bool bShouldAlign = ViewOwnerCharacter.IsValid() && !bFirstPersonView && !bHolstered && GetNetMode() != NM_DedicatedServer;
	if (IsActorTickEnabled() != bShouldAlign)
	{
		SetActorTickEnabled(bShouldAlign);
	}
}

void AValorWeaponBase::UpdateThirdPersonAim(float DeltaSeconds)
{
	AValorCharacter* AimingCharacter = ViewOwnerCharacter.Get();
	USceneComponent* ItemRoot = GetRootComponent();
	USceneComponent* HandComponent = ItemRoot ? ItemRoot->GetAttachParent() : nullptr;
	UWorld* World = GetWorld();
	if (!AimingCharacter || !HandComponent || !World)
	{
		return;
	}

	// 보정 전 총 방향 = 손 소켓(이번 프레임 애니메이션 결과) x 손 -> 총 리그 기본 회전.
	const FQuat HandRotation = HandComponent->GetSocketQuaternion(ItemRoot->GetAttachSocketName());
	const FQuat BaseRotation = HandRotation * AimingCharacter->GetThirdPersonWeaponRotation();
	const FVector MuzzleDirection = BaseRotation.RotateVector(FVector::UpVector);

	// 조준점: 그 캐릭터의 눈에서 조준 방향(원격 캐릭터는 복제된 피치)으로 처음 닿는 지점.
	// 캐릭터 메시는 Visibility를 무시하므로 월드(Visibility)와 캐릭터(Pawn 오브젝트) 두 번 쏘고 가까운 쪽을 쓴다.
	const FVector EyeLocation = AimingCharacter->GetPawnViewLocation();
	const FRotator AimRotation = AimingCharacter->GetBaseAimRotation();
	const FVector AimDirection = AimRotation.Vector();
	constexpr float MaxAimDistance = 5000.0f;
	constexpr float MinAimDistance = 150.0f;
	const FVector TraceEnd = EyeLocation + (AimDirection * MaxAimDistance);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ValorThirdPersonAim), false, AimingCharacter);
	QueryParams.AddIgnoredActor(this);

	float TargetDistance = MaxAimDistance;
	FVector TargetEyeOffset = FVector::ZeroVector;
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, EyeLocation, TraceEnd, ECC_Visibility, QueryParams))
	{
		TargetDistance = Hit.Distance;
	}
	if (World->LineTraceSingleByObjectType(Hit, EyeLocation, TraceEnd, FCollisionObjectQueryParams(ECC_Pawn), QueryParams) && Hit.Distance < TargetDistance)
	{
		// 캐릭터에 맞으면 캡슐 앞면이 아니라 그 캐릭터 눈(카메라 위치)의 깊이에서 총구 선이 시선과 만나게 한다.
		// 앞면(약 35cm 앞)에서 만나면 총구 선이 그 뒤로 계속 벌어져, 겨눠진 사람의 눈 위치에서는 위·옆으로 몇 cm 비켜 간다.
		const APawn* HitPawn = Cast<APawn>(Hit.GetActor());
		if (HitPawn)
		{
			const FVector TargetEye = HitPawn->GetPawnViewLocation();
			const float EyeDepth = (TargetEye - EyeLocation) | AimDirection;
			TargetDistance = FMath::Max(Hit.Distance, EyeDepth);

			// 머리를 겨누면 총구를 그 사람의 눈으로 모은다. 3인칭 머리 메시(보이는 머리 중심)는 카메라 위치보다 약 22cm 앞·위에 있어,
			// 보이는 머리를 정확히 겨눈 시선도 겨눠진 사람의 눈을 약 10cm 비켜 간다(3m에서 약 2도, 네트워크 도구로 잼).
			// 시선이 눈에서 20cm 안(머리 전체)을 지나면 완전히, 35cm에서 0이 되도록 눈 쪽으로 옮긴다. 가슴(40cm 이상)은 건드리지 않는다.
			// 제3자 화면에서는 머리 크기 안의 차이라 티가 나지 않는다.
			const FVector OffsetToEye = TargetEye - (EyeLocation + (AimDirection * EyeDepth));
			TargetEyeOffset = OffsetToEye * (1.0f - FMath::SmoothStep(20.0f, 35.0f, static_cast<float>(OffsetToEye.Size())));
		}
		else
		{
			TargetDistance = Hit.Distance;
		}
	}

	ThirdPersonAimDistance = FMath::FInterpTo(ThirdPersonAimDistance, FMath::Max(TargetDistance, MinAimDistance), DeltaSeconds, 10.0f);
	ThirdPersonAimEyeOffset = FMath::VInterpTo(ThirdPersonAimEyeOffset, TargetEyeOffset, DeltaSeconds, 10.0f);
	const FVector AimPoint = EyeLocation + (AimDirection * ThirdPersonAimDistance) + ThirdPersonAimEyeOffset;
	const FVector DesiredDirection = (AimPoint - ItemRoot->GetComponentLocation()).GetSafeNormal();
	if (DesiredDirection.IsNearlyZero())
	{
		return;
	}

	// 목표 회전: 총구 = 조준점 방향, 총의 위 = 시선의 위. 손잡이를 축으로 돌린다.
	// 손 자세의 좌우 기울임(cant)까지 없앤다. 총구만 맞추고 기울임을 남기면, 정면에서 본 총 단면이 비스듬해 다른 곳을 겨누는 것처럼 보인다.
	const FQuat AlignedRotation = ValorViewmodel::MakeLevelRigRotation(DesiredDirection, FRotationMatrix(AimRotation).GetUnitAxis(EAxis::Z));

	// 손 자세의 총구가 조준점에서 20도 안이면 완전히 맞추고, 40도에서 손 자세 그대로가 되도록 줄인다(재장전·장착 동작 중).
	const float CorrectionDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(MuzzleDirection | DesiredDirection, -1.0f, 1.0f)));
	const float Weight = 1.0f - FMath::SmoothStep(20.0f, 40.0f, CorrectionDegrees);
	const FQuat WorldRotation = FQuat::Slerp(BaseRotation, AlignedRotation, Weight);

	ItemRoot->SetRelativeRotation(HandRotation.Inverse() * WorldRotation);
}

float AValorWeaponBase::GetViewmodelSightHeightCm() const
{
	if (CachedSightHeightCm < 0.0f)
	{
		float TopAboveGripCm = 0.0f;
		const USkeletalMesh* SkeletalMesh = WeaponMesh ? WeaponMesh->GetSkeletalMeshAsset() : nullptr;
		if (SkeletalMesh)
		{
			const FName GripReferenceName = ValorViewmodel::ResolveGripReferenceName(*WeaponMesh, GetWeaponConfig().GripSocketName);
			// 참조 자세 기준(애니메이션 없음). 메시 상대 변환에 영향받지 않도록 컴포넌트 공간 값끼리 계산한다.
			const FTransform GripTransform = GripReferenceName.IsNone() ? FTransform::Identity : WeaponMesh->GetSocketTransform(GripReferenceName, RTS_Component);
			TopAboveGripCm = ValorViewmodel::ComputeTopAboveGripCm(SkeletalMesh->GetBounds().GetBox(), GripTransform);
		}

		CachedSightHeightCm = ValorViewmodel::ResolveSightHeightCm(GetViewmodelConfig(), TopAboveGripCm);
	}

	return CachedSightHeightCm;
}

void AValorWeaponBase::OnUnequipped()
{
	bHolstered = false;
	bFirstPersonView = false;
	ViewOwnerCharacter.Reset();
	RefreshThirdPersonAimTick();
	SetScopedViewHidden(false);

	if (WeaponMesh)
	{
		WeaponMesh->SetRelativeTransform(FTransform::Identity);
	}

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	// 1인칭 피벗의 축소 배율을 물려받았을 수 있으므로 원래 크기로 되돌린다.
	SetActorScale3D(FVector::OneVector);
	OwningCharacter = nullptr;
	SetOwner(nullptr);
	SetInstigator(nullptr);
}

const FValorWeaponConfig& AValorWeaponBase::GetWeaponConfig() const
{
	return WeaponDataAsset ? WeaponDataAsset->WeaponConfig : FallbackWeaponConfig;
}

FValorComputedShotData AValorWeaponBase::SimulateShot(double ShotTime, const FValorShooterStance& Stance)
{
	const FValorWeaponConfig& Config = GetWeaponConfig();

	// 소유 클라: 새 스프레이의 첫 발(= 직전 스프레이가 Gun Recovery Time 이상 지나 완전히 회복됨)에서 난수 번호를 서버 값에 맞춘다.
	// 그 사이 서버가 거부한 예측 발이 있었다면 여기서 어긋남이 사라진다(서버 조정, reconciliation).
	// 회복 시간(0.375s) 동안 이전 발들의 서버 승인 결과가 도착하므로 RTT가 이보다 짧으면 항상 최신 값으로 맞춰진다.
	if (!HasAuthority() && ValorSpray::IsNewSpray(Config, Stance.bIsADS, SprayState, ShotTime))
	{
		ShotCounter = ServerShotCount;
	}

	const int32 ShotSeed = ValorSpray::MakeShotSeed(RecoilSeed, ShotCounter);
	const FValorComputedShotData ShotData = ValorSpray::AdvanceShot(Config, Stance, SprayState, ShotTime, ShotSeed);

	++ShotCounter;
	if (HasAuthority())
	{
		ServerShotCount = ShotCounter;
	}

	return ShotData;
}

FValorSprayEvaluation AValorWeaponBase::EvaluateSpray(double Now, const FValorShooterStance& Stance) const
{
	return ValorSpray::Evaluate(GetWeaponConfig(), Stance, SprayState, Now);
}

FVector AValorWeaponBase::ComputeShotDirection(const FRotator& AimRotation, const FValorComputedShotData& ShotData) const
{
	return ValorSpray::ComputeShotDirection(AimRotation, ShotData);
}

FVector AValorWeaponBase::ComputePelletDirection(const FRotator& AimRotation, const FValorComputedShotData& ShotData, int32 PelletIndex) const
{
	return ValorSpray::ComputePelletDirection(AimRotation, ShotData, PelletIndex);
}

FVector AValorWeaponBase::ComputeRecoilDirection(const FRotator& AimRotation, const FValorComputedShotData& ShotData) const
{
	return ValorSpray::ComputeRecoilDirection(AimRotation, ShotData);
}

float AValorWeaponBase::GetMaxFireRate() const
{
	return ValorSpray::GetMaxFireRate(GetWeaponConfig());
}

EValorAltFireType AValorWeaponBase::GetAltFireType() const
{
	return GetWeaponConfig().AltFireType;
}

bool AValorWeaponBase::IsAltFireAttack() const
{
	const EValorAltFireType AltFireType = GetAltFireType();
	return AltFireType == EValorAltFireType::Shotgun || AltFireType == EValorAltFireType::AirBurst;
}

float AValorWeaponBase::GetAirBurstDistance() const
{
	return GetWeaponConfig().AirBurstDistanceCm;
}

float AValorWeaponBase::GetFireInterval(bool bIsADS) const
{
	return ValorSpray::GetFireInterval(GetWeaponConfig(), bIsADS);
}

float AValorWeaponBase::GetCameraRecoilFollowRatio(bool bIsADS) const
{
	const FValorWeaponConfig& Config = GetWeaponConfig();
	if (bIsADS)
	{
		return Config.AltFire.CameraRecoilFollowRatio;
	}

	const float HipOverride = CVarValorHipCameraFollowOverride.GetValueOnGameThread();
	return HipOverride >= 0.0f ? FMath::Clamp(HipOverride, 0.0f, 1.0f) : Config.HipFire.CameraRecoilFollowRatio;
}

const FValorCameraKickConfig& AValorWeaponBase::GetCameraKickConfig(bool bIsADS) const
{
	const FValorWeaponConfig& Config = GetWeaponConfig();
	return bIsADS ? Config.AltFire.CameraKick : Config.HipFire.CameraKick;
}

int32 AValorWeaponBase::GetRoundsForShot(bool bAltMode, int32 AvailableRounds) const
{
	if (AvailableRounds <= 0)
	{
		return 0;
	}

	const FValorWeaponConfig& Config = GetWeaponConfig();
	const FValorFireModeStats& FireMode = bAltMode ? Config.AltFire : Config.HipFire;
	return FMath::Clamp(FireMode.AmmoPerShot, 1, AvailableRounds);
}

int32 AValorWeaponBase::GetPelletCountForShot(bool bAltMode, int32 RoundsUsed) const
{
	const FValorWeaponConfig& Config = GetWeaponConfig();
	const FValorFireModeStats& FireMode = bAltMode ? Config.AltFire : Config.HipFire;
	const int32 PelletCount = FMath::Max(FireMode.PelletCount, 1);
	if (FireMode.AmmoPerShot <= 1)
	{
		return PelletCount;
	}

	// 클래식 우클릭(3탄 3펠릿)을 탄 2발 남기고 쏘면 2펠릿만 나간다.
	return FMath::Clamp((PelletCount * RoundsUsed) / FireMode.AmmoPerShot, 1, PelletCount);
}

int32 AValorWeaponBase::ConsumeAmmoForShot(bool bAltMode)
{
	if (!HasAuthority())
	{
		return 0;
	}

	const int32 RoundsUsed = GetRoundsForShot(bAltMode, CurrentMagazineAmmo);
	CurrentMagazineAmmo = FMath::Max(0, CurrentMagazineAmmo - RoundsUsed);
	return RoundsUsed;
}

void AValorWeaponBase::NotePredictedShotRounds(int32 Rounds)
{
	// SimulateShot이 ShotCounter를 올린 뒤에 호출되므로, 방금 쏜 발의 번호는 ShotCounter - 1이다.
	const int32 ShotIndex = FMath::Max(ShotCounter - 1, 0);
	PredictedRoundsByShot[ShotIndex % PredictedRoundsHistorySize] = static_cast<uint8>(FMath::Clamp(Rounds, 0, 255));
}

int32 AValorWeaponBase::GetPredictedMagazineAmmo() const
{
	if (HasAuthority())
	{
		return CurrentMagazineAmmo;
	}

	// 서버가 아직 처리하지 않은 예측 발 = 로컬 누적 발사 번호 [ServerShotCount, ShotCounter).
	// 탄약과 ServerShotCount는 같은 서버 프레임에 바뀌어 보통 함께 복제되므로, 이 발들이 쓴 탄만큼 빼면 된다.
	int32 RoundsInFlight = 0;
	for (int32 ShotIndex = FMath::Max(ServerShotCount, ShotCounter - PredictedRoundsHistorySize); ShotIndex < ShotCounter; ++ShotIndex)
	{
		RoundsInFlight += PredictedRoundsByShot[ShotIndex % PredictedRoundsHistorySize];
	}

	return FMath::Max(0, CurrentMagazineAmmo - RoundsInFlight);
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

void AValorWeaponBase::RestoreAmmoState(int32 MagazineAmmo, int32 ReserveAmmo)
{
	if (!HasAuthority())
	{
		return;
	}

	// 데이터 에셋 한도로 자른다: 떨어뜨린 뒤 에셋 수치가 바뀌었거나 값이 잘못돼도 탄창이 넘치지 않게 한다.
	const FValorWeaponConfig& Config = GetWeaponConfig();
	CurrentMagazineAmmo = FMath::Clamp(MagazineAmmo, 0, Config.MagazineSize);
	CurrentReserveAmmo = FMath::Clamp(ReserveAmmo, 0, Config.MaxReserveAmmo);
}

void AValorWeaponBase::PlayFireEffects(const FValorShotEffects& ShotEffects) const
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const FValorWeaponFXConfig& Effects = GetWeaponConfig().Effects;
	const FVector MuzzleLocation = GetMuzzleLocation();

	// 총구 화염·발사음은 발사 1회에 한 번(산탄총 펠릿 수만큼 겹쳐 재생하지 않는다).
	if (Effects.MuzzleFlashFX && WeaponMesh)
	{
		UNiagaraFunctionLibrary::SpawnSystemAttached(Effects.MuzzleFlashFX, WeaponMesh, Effects.MuzzleSocketName, FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true);
	}

	if (Effects.FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Effects.FireSound, MuzzleLocation);
	}

#if ENABLE_DRAW_DEBUG
	const bool bDrawDebug = CVarValorDrawShotDebug.GetValueOnGameThread() > 0;
#endif
	auto SpawnTracer = [&](const FVector& TracerStart, const FVector& TracerEnd)
	{
		if (Effects.TracerFX)
		{
			if (UNiagaraComponent* TracerComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Effects.TracerFX, TracerStart, (TracerEnd - TracerStart).Rotation()))
			{
				TracerComponent->SetVariableVec3(Effects.TracerEndParameterName, TracerEnd);
			}
		}
#if ENABLE_DRAW_DEBUG
		else if (bDrawDebug)
		{
			// 에셋이 비어 있는 동안에도 스프레이 모양이 보이도록 개발 빌드에서만 임시로 그린다.
			// 트레이서는 총구에서 시작해야 카메라 정면 방향과 겹치지 않고 "줄기"로 보인다.
			DrawDebugLine(World, TracerStart, TracerEnd, FColor(255, 214, 120), false, 0.05f, 0, 0.6f);
		}
#endif
	};

	// 버키 캐니스터: 총구 → 폭발 지점까지 한 줄, 펠릿은 폭발 지점에서 퍼진다.
	const FVector PelletTracerStart = ShotEffects.bHasTracerOrigin ? FVector(ShotEffects.TracerOrigin) : MuzzleLocation;
	if (ShotEffects.bHasTracerOrigin)
	{
		SpawnTracer(MuzzleLocation, PelletTracerStart);
	}

	for (const FValorShotImpact& Impact : ShotEffects.Impacts)
	{
		SpawnTracer(PelletTracerStart, Impact.ImpactPoint);

		if (!Impact.bBlockingHit)
		{
			continue;
		}

		if (Effects.ImpactFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Effects.ImpactFX, Impact.ImpactPoint, Impact.ImpactNormal.Rotation());
		}

		// 캐릭터에는 데칼을 남기지 않는다(움직이는 대상에 데칼이 떠 보이는 문제 방지).
		if (Effects.ImpactDecalMaterial && !Impact.bHitCharacter)
		{
			UGameplayStatics::SpawnDecalAtLocation(this, Effects.ImpactDecalMaterial, Effects.ImpactDecalSize, Impact.ImpactPoint, Impact.ImpactNormal.Rotation(), Effects.ImpactDecalLifeSpan);
		}
#if ENABLE_DRAW_DEBUG
		else if (bDrawDebug && !Effects.ImpactDecalMaterial)
		{
			DrawDebugPoint(World, Impact.ImpactPoint, 7.0f, Impact.bHitCharacter ? FColor::Red : FColor::Yellow, false, 4.0f);
		}
#endif
	}
}

FVector AValorWeaponBase::GetMuzzleLocation() const
{
	if (!WeaponMesh)
	{
		return GetActorLocation();
	}

	const FName MuzzleSocketName = GetWeaponConfig().Effects.MuzzleSocketName;
	if (!MuzzleSocketName.IsNone() && WeaponMesh->DoesSocketExist(MuzzleSocketName))
	{
		return WeaponMesh->GetSocketLocation(MuzzleSocketName);
	}

	return WeaponMesh->GetComponentLocation();
}

float AValorWeaponBase::GetReloadDuration() const
{
	return GetWeaponConfig().ReloadDuration;
}

bool AValorWeaponBase::IsAutomatic() const
{
	return GetWeaponConfig().bAutomatic;
}

float AValorWeaponBase::GetADSFieldOfView(float HipFieldOfView) const
{
	return GetZoomFieldOfView(HipFieldOfView, 1);
}

int32 AValorWeaponBase::GetMaxZoomLevel() const
{
	const FValorWeaponConfig& Config = GetWeaponConfig();
	if (Config.AltFireType != EValorAltFireType::ADS)
	{
		return 0;
	}

	return Config.SecondaryADSZoomMultiplier > Config.ADSZoomMultiplier ? 2 : 1;
}

float AValorWeaponBase::GetZoomFieldOfView(float HipFieldOfView, int32 ZoomLevel) const
{
	// 줌 배율 Z는 화면 폭의 tan(FOV/2)를 1/Z로 줄이는 것과 같다: ADS_FOV = 2·atan(tan(Hip/2) / Z).
	// 밴달(1.25배)·힙 103도 → 약 90.3도, 오퍼레이터 2.5배 → 약 53.8도, 5배 → 약 28.1도.
	const FValorWeaponConfig& Config = GetWeaponConfig();
	const float ZoomMultiplier = (ZoomLevel >= 2 && Config.SecondaryADSZoomMultiplier > 0.0f) ? Config.SecondaryADSZoomMultiplier : Config.ADSZoomMultiplier;
	const float Zoom = FMath::Max(ZoomMultiplier, 1.0f);
	const float HalfHipRadians = FMath::DegreesToRadians(FMath::Clamp(HipFieldOfView, 5.0f, 170.0f) * 0.5f);
	return FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(HalfHipRadians) / Zoom));
}

bool AValorWeaponBase::UsesScopeOverlay() const
{
	return GetWeaponConfig().bUseScopeOverlay;
}

UMaterialInterface* AValorWeaponBase::GetScopeOverlayMaterial() const
{
	return GetWeaponConfig().ScopeOverlayMaterial;
}

bool AValorWeaponBase::ShouldUnscopeAfterShot() const
{
	return GetWeaponConfig().bUnscopeAfterShot;
}

bool AValorWeaponBase::IsSniper() const
{
	return GetWeaponConfig().Category == EValorWeaponCategory::Sniper;
}

float AValorWeaponBase::GetADSFireDelay() const
{
	return FMath::Max(GetWeaponConfig().ADSFireDelaySeconds, 0.0f);
}

void AValorWeaponBase::SetScopedViewHidden(bool bHideForScope)
{
	bScopeViewHidden = bHideForScope;
	RefreshMeshVisibility();
}

void AValorWeaponBase::SetHolstered(bool bNewHolstered)
{
	bHolstered = bNewHolstered;
	RefreshMeshVisibility();
	RefreshThirdPersonAimTick();
}

void AValorWeaponBase::RefreshMeshVisibility()
{
	// 루트부터 자식 전체에 적용한다. WeaponMesh만 바꾸면 BP에서 루트 아래에 따로 붙인 부품 메시(오퍼레이터 등)가
	// 칼로 바꾼 뒤에도 손에 남거나, 조준경 화면을 가린다. 가시성은 복제되지 않는 로컬 연출이다.
	const bool bShouldBeVisible = !bHolstered && !bScopeViewHidden;
	if (Root && Root->IsVisible() != bShouldBeVisible)
	{
		Root->SetVisibility(bShouldBeVisible, true);
	}
}

void AValorWeaponBase::CancelBurst()
{
	SprayState.ShotsInBurst = 0;
}

EValorWeaponSlot AValorWeaponBase::GetInventorySlot() const
{
	return ValorInventory::SlotForCategory(GetWeaponConfig().Category);
}

float AValorWeaponBase::GetEquipTime() const
{
	return FMath::Max(GetWeaponConfig().EquipTimeSeconds, 0.0f);
}

float AValorWeaponBase::GetMoveSpeedMultiplier(bool bIsADS) const
{
	const FValorWeaponConfig& Config = GetWeaponConfig();
	float Multiplier = Config.HipFire.MoveSpeedMultiplier;

	// 우클릭이 조준인 총만 ADS 배율을 쓴다. 클래식·버키의 AltFire 값은 "우클릭 공격" 모드라 이동 속도와 무관하다.
	if (bIsADS && GetMaxZoomLevel() > 0)
	{
		Multiplier *= Config.AltFire.MoveSpeedMultiplier;
	}

	return FMath::Clamp(Multiplier, 0.1f, 1.0f);
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

void AValorWeaponBase::RefreshWeaponMeshAlignment()
{
	if (!WeaponMesh)
	{
		return;
	}

	WeaponMesh->SetRelativeTransform(FTransform::Identity);

	// GripSocket이 없는 총(밴달 외 13종)은 리그 공통 Root 뼈로 맞춘다. 축 규칙이 같아 밴달과 같은 방향으로 잡힌다.
	const FName GripSocketName = ValorViewmodel::ResolveGripReferenceName(*WeaponMesh, GetWeaponConfig().GripSocketName);
	if (GripSocketName.IsNone())
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

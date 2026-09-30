#include "ValorWeaponBase.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
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
}

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

	CurrentMagazineAmmo = FallbackWeaponConfig.MagazineSize;
	CurrentReserveAmmo = FallbackWeaponConfig.MaxReserveAmmo;
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

void AValorWeaponBase::ConsumeAmmo()
{
	if (!HasAuthority())
	{
		return;
	}

	CurrentMagazineAmmo = FMath::Max(0, CurrentMagazineAmmo - 1);
}

int32 AValorWeaponBase::GetPredictedMagazineAmmo() const
{
	if (HasAuthority())
	{
		return CurrentMagazineAmmo;
	}

	// 서버가 아직 처리하지 않은 예측 발 수 = 로컬 누적 발사 수 - 서버 누적 발사 수.
	// 탄약과 ServerShotCount는 같은 서버 프레임에 바뀌어 보통 함께 복제되므로 이 차이가 "비행 중인 발"이다.
	const int32 ShotsInFlight = FMath::Max(0, ShotCounter - ServerShotCount);
	return FMath::Max(0, CurrentMagazineAmmo - ShotsInFlight);
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

void AValorWeaponBase::PlayFireEffects(const FVector& ImpactPoint, const FVector& ImpactNormal, bool bBlockingHit, bool bHitCharacter) const
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const FValorWeaponFXConfig& Effects = GetWeaponConfig().Effects;
	const FVector MuzzleLocation = GetMuzzleLocation();

	if (Effects.MuzzleFlashFX && WeaponMesh)
	{
		UNiagaraFunctionLibrary::SpawnSystemAttached(Effects.MuzzleFlashFX, WeaponMesh, Effects.MuzzleSocketName, FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true);
	}

	if (Effects.TracerFX)
	{
		if (UNiagaraComponent* TracerComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Effects.TracerFX, MuzzleLocation, (ImpactPoint - MuzzleLocation).Rotation()))
		{
			TracerComponent->SetVariableVec3(Effects.TracerEndParameterName, ImpactPoint);
		}
	}

	if (bBlockingHit)
	{
		if (Effects.ImpactFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Effects.ImpactFX, ImpactPoint, ImpactNormal.Rotation());
		}

		// 캐릭터에는 데칼을 남기지 않는다(움직이는 대상에 데칼이 떠 보이는 문제 방지).
		if (Effects.ImpactDecalMaterial && !bHitCharacter)
		{
			UGameplayStatics::SpawnDecalAtLocation(this, Effects.ImpactDecalMaterial, Effects.ImpactDecalSize, ImpactPoint, ImpactNormal.Rotation(), Effects.ImpactDecalLifeSpan);
		}
	}

	if (Effects.FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Effects.FireSound, MuzzleLocation);
	}

#if ENABLE_DRAW_DEBUG
	if (CVarValorDrawShotDebug.GetValueOnGameThread() > 0)
	{
		// 에셋이 비어 있는 동안에도 스프레이 모양이 벽에 보이도록 개발 빌드에서만 임시로 그린다.
		// 트레이서는 총구에서 시작해야 카메라 정면 방향과 겹치지 않고 "줄기"로 보인다.
		if (!Effects.TracerFX)
		{
			DrawDebugLine(World, MuzzleLocation, ImpactPoint, FColor(255, 214, 120), false, 0.05f, 0, 0.6f);
		}

		if (bBlockingHit && !Effects.ImpactDecalMaterial)
		{
			DrawDebugPoint(World, ImpactPoint, 7.0f, bHitCharacter ? FColor::Red : FColor::Yellow, false, 4.0f);
		}
	}
#endif
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
	// 줌 배율 Z는 화면 폭의 tan(FOV/2)를 1/Z로 줄이는 것과 같다: ADS_FOV = 2·atan(tan(Hip/2) / Z).
	// 밴달(1.25배)·힙 103도 → 약 90.3도.
	const float Zoom = FMath::Max(GetWeaponConfig().ADSZoomMultiplier, 1.0f);
	const float HalfHipRadians = FMath::DegreesToRadians(FMath::Clamp(HipFieldOfView, 5.0f, 170.0f) * 0.5f);
	return FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(HalfHipRadians) / Zoom));
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

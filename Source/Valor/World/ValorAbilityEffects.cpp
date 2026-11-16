#include "World/ValorAbilityEffects.h"

#include "Player/ValorPlayerState.h"

#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystem/Effects/ValorDamageGameplayEffect.h"
#include "AbilitySystem/ValorAgentTypes.h"
#include "AbilitySystem/ValorGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Player/ValorPlayerController.h"
#include "UObject/ConstructorHelpers.h"
#include "ValorCharacter.h"
#include "World/ValorSmokeCloud.h"

namespace ValorAbilityEffects
{
	// 효과 지점에서 캐릭터 눈까지 벽에 가리지 않았나(캐릭터·스킬 물체는 무시, 연막도 시야를 가린다).
	bool HasLineOfSight(const UWorld* World, const FVector& From, const AValorCharacter& Character, const AActor* IgnoreActor)
	{
		const FVector To = Character.GetPawnViewLocation();
		for (TActorIterator<AValorSmokeCloud> It(World); It; ++It)
		{
			// 연막 안을 지나가는 선은 가린다(선분과 구의 최근접 거리).
			const FVector Closest = FMath::ClosestPointOnSegment(It->GetActorLocation(), From, To);
			if (It->ContainsLocation(Closest) && !It->ContainsLocation(From) && !It->ContainsLocation(To))
			{
				return false;
			}
		}

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ValorAbilityLineOfSight), false, IgnoreActor);
		for (TActorIterator<AValorCharacter> It(World); It; ++It)
		{
			QueryParams.AddIgnoredActor(*It);
		}
		FHitResult Hit;
		return !World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, QueryParams);
	}
}

// ======================================================================================
// 섬광
// ======================================================================================

AValorFlashBurst::AValorFlashBurst()
{
	LifetimeSeconds = 0.5f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AValorFlashBurst::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	AValorPlayerState* SourceState = GetInstigator() ? GetInstigator()->GetPlayerState<AValorPlayerState>() : nullptr;
	for (TActorIterator<AValorCharacter> It(GetWorld()); It; ++It)
	{
		AValorPlayerController* ValorController = Cast<AValorPlayerController>(It->GetController());
		if (!It->IsAlive())
		{
			continue;
		}

		// 시선(서버가 아는 조준 방향)과 섬광 방향 사이 각도. 정면에 가까울수록 오래 눈부시다.
		const FVector Eye = It->GetPawnViewLocation();
		const FVector ToFlash = (GetActorLocation() - Eye).GetSafeNormal();
		const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(It->GetBaseAimRotation().Vector() | ToFlash, -1.0f, 1.0f)));
		const bool bVisible = ValorAbilityEffects::HasLineOfSight(GetWorld(), GetActorLocation(), **It, this);
		const float Seconds = ValorAbilityRules::ComputeFlashSeconds(AngleDegrees, bVisible, FVector::Dist(Eye, GetActorLocation()), RadiusCm, FullBlindSeconds, PartialBlindSeconds);
		if (Seconds <= 0.0f)
		{
			continue;
		}
		// 화면은 사람에게만 하얗게(봇은 화면이 없다). 어시스트 기록은 눈부신 적 모두(봇 포함).
		if (ValorController)
		{
			ValorController->ClientFlashed(Seconds);
		}
		if (SourceState && OwnerTeam != EValorTeam::None && ValorTeams::GetTeamOf(*It) != OwnerTeam)
		{
			It->RecordUtilityContribution(SourceState, GetWorld()->GetTimeSeconds() + Seconds);
		}
	}
}

// ======================================================================================
// 정찰
// ======================================================================================

AValorReconPulse::AValorReconPulse()
{
	LifetimeSeconds = 0.5f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AValorReconPulse::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority() || OwnerTeam == EValorTeam::None)
	{
		return;
	}

	for (TActorIterator<AValorCharacter> It(GetWorld()); It; ++It)
	{
		const EValorTeam CharacterTeam = ValorTeams::GetTeamOf(*It);
		if (It->IsAlive() && CharacterTeam != EValorTeam::None && CharacterTeam != OwnerTeam
			&& FVector::Dist(It->GetActorLocation(), GetActorLocation()) <= RadiusCm
			&& ValorAbilityEffects::HasLineOfSight(GetWorld(), GetActorLocation(), **It, this))
		{
			It->RevealToTeam(OwnerTeam, RevealSeconds);
			It->RecordUtilityContribution(GetInstigator() ? GetInstigator()->GetPlayerState<AValorPlayerState>() : nullptr, GetWorld()->GetTimeSeconds() + RevealSeconds);
		}
	}
}

// ======================================================================================
// 벽
// ======================================================================================

AValorAbilityWall::AValorAbilityWall()
{
	LifetimeSeconds = 20.0f;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}

	// 캐릭터·탄(Visibility)·카메라를 막는다.
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionObjectType(ECC_WorldDynamic);
	Mesh->SetCollisionResponseToAllChannels(ECR_Block);
	Mesh->SetCanEverAffectNavigation(false);
}

void AValorAbilityWall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValorAbilityWall, Health);
}

void AValorAbilityWall::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// 기본 정육면체는 100cm, 중심이 액터 위치다(세우는 스킬이 바닥에서 높이 절반 위에 둔다).
	Mesh->SetWorldScale3D(Size / 100.0f);
	Health = MaxHealth;
}

void AValorAbilityWall::ApplyWallDamage(float Damage)
{
	if (!HasAuthority() || Damage <= 0.0f || Health <= 0.0f)
	{
		return;
	}

	Health = FMath::Max(0.0f, Health - Damage);
	if (Health <= 0.0f)
	{
		Destroy();
	}
}

// ======================================================================================
// 지속 회복
// ======================================================================================

AValorHealOverTime::AValorHealOverTime()
{
	bReplicates = false;
	PrimaryActorTick.bCanEverTick = true;
	LifetimeSeconds = 10.0f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AValorHealOverTime::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AValorCharacter* Character = Target.Get();
	UAbilitySystemComponent* AbilitySystem = Character ? Character->GetAbilitySystemComponent() : nullptr;
	if (!HasAuthority() || !AbilitySystem || !Character->IsAlive() || Elapsed >= DurationSeconds)
	{
		Destroy();
		return;
	}

	// 회복 어시스트 기록(처음 한 번): 다른 사람이 걸어 준 회복만.
	if (Elapsed <= 0.0f && GetInstigator() && GetInstigator() != Character)
	{
		Character->RecordHealFrom(GetInstigator()->GetPlayerState<AValorPlayerState>(), GetWorld()->GetTimeSeconds() + (DurationSeconds - Elapsed));
	}

	const float Step = FMath::Min(DeltaSeconds, DurationSeconds - Elapsed);
	Elapsed += Step;
	const float Health = AbilitySystem->GetNumericAttribute(UValorCombatAttributeSet::GetHealthAttribute());
	const float MaxHealth = AbilitySystem->GetNumericAttribute(UValorCombatAttributeSet::GetMaxHealthAttribute());
	AbilitySystem->SetNumericAttributeBase(UValorCombatAttributeSet::GetHealthAttribute(), FMath::Min(MaxHealth, Health + (TotalHeal * Step / DurationSeconds)));
}

// ======================================================================================
// 투척물 프리셋
// ======================================================================================

AValorProjectile_Smoke::AValorProjectile_Smoke()
{
	PayloadClass = AValorSmokeCloud_Canister::StaticClass();
	InitialSpeed = 1600.0f;
	bBounce = true;
	bDetonateOnStop = true;
	FuseSeconds = 3.0f;
	PayloadHeightOffset = 100.0f;
}

AValorProjectile_Flash::AValorProjectile_Flash()
{
	PayloadClass = AValorFlashBurst::StaticClass();
	InitialSpeed = 1500.0f;
	bBounce = true;
	bDetonateOnStop = false;
	FuseSeconds = 0.6f;
}

AValorProjectile_Recon::AValorProjectile_Recon()
{
	PayloadClass = AValorReconPulse::StaticClass();
	InitialSpeed = 3000.0f;
	GravityScale = 0.3f;
	bStickOnHit = true;
	bDetonateOnStop = true;
}

// ======================================================================================
// 화염 지대
// ======================================================================================

AValorDamageZone::AValorDamageZone()
{
	PrimaryActorTick.bCanEverTick = true;
	LifetimeSeconds = 4.0f;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->CastShadow = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CylinderMesh.Object);
	}
}

void AValorDamageZone::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// 기본 원기둥은 지름 100cm·높이 100cm. 바닥에 얇게 깐 원판으로 보이게 한다.
	Mesh->SetWorldScale3D(FVector(RadiusCm / 50.0f, RadiusCm / 50.0f, 0.1f));
}

void AValorDamageZone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

	Accumulated += DeltaSeconds;
	while (Accumulated >= TickSeconds)
	{
		Accumulated -= TickSeconds;
		TArray<AValorCharacter*> Victims;
		for (TActorIterator<AValorCharacter> It(GetWorld()); It; ++It)
		{
			const FVector Offset = It->GetActorLocation() - GetActorLocation();
			if (It->IsAlive() && Offset.Size2D() <= RadiusCm && FMath::Abs(Offset.Z) <= 200.0f)
			{
				Victims.Add(*It);
			}
		}
		for (AValorCharacter* Victim : Victims)
		{
			ApplyZoneDamage(*Victim, DamagePerSecond * TickSeconds);
		}
	}
}

void AValorDamageZone::ApplyZoneDamage(AValorCharacter& Victim, float Damage)
{
	// 만든 사람의 ASC로 보낸다(처치 기록·킬피드가 만든 사람에게 간다). 만든 사람이 없으면 맞는 쪽 ASC로.
	AValorCharacter* InstigatorCharacter = Cast<AValorCharacter>(GetInstigator());
	UAbilitySystemComponent* TargetAbilitySystem = Victim.GetAbilitySystemComponent();
	UAbilitySystemComponent* SourceAbilitySystem = InstigatorCharacter && InstigatorCharacter->GetAbilitySystemComponent() ? InstigatorCharacter->GetAbilitySystemComponent() : TargetAbilitySystem;
	if (!SourceAbilitySystem || !TargetAbilitySystem)
	{
		return;
	}

	FGameplayEffectContextHandle Context = SourceAbilitySystem->MakeEffectContext();
	Context.AddInstigator(InstigatorCharacter, this);
	Context.AddOrigin(GetActorLocation());
	const FGameplayEffectSpecHandle Spec = SourceAbilitySystem->MakeOutgoingSpec(UValorDamageGameplayEffect::StaticClass(), 1.0f, Context);
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(ValorGameplayTags::SetByCaller_Damage, Damage);
		SourceAbilitySystem->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetAbilitySystem);
	}
}

AValorDamageZone_Inferno::AValorDamageZone_Inferno()
{
	LifetimeSeconds = 7.0f;
	RadiusCm = 600.0f;
	DamagePerSecond = 40.0f;
}

AValorReconPulse_Wide::AValorReconPulse_Wide()
{
	RadiusCm = 5000.0f;
	RevealSeconds = 4.0f;
}

AValorAbilityWall_Small::AValorAbilityWall_Small()
{
	MaxHealth = 400.0f;
	Size = FVector(30.0f, 250.0f, 200.0f);
	LifetimeSeconds = 15.0f;
}

AValorProjectile_Incendiary::AValorProjectile_Incendiary()
{
	PayloadClass = AValorDamageZone::StaticClass();
	InitialSpeed = 1400.0f;
	bBounce = true;
	bDetonateOnStop = true;
	FuseSeconds = 2.5f;
	PayloadHeightOffset = 5.0f;
}

AValorProjectile_Inferno::AValorProjectile_Inferno()
{
	PayloadClass = AValorDamageZone_Inferno::StaticClass();
	InitialSpeed = 2000.0f;
	bBounce = true;
	bDetonateOnStop = true;
	FuseSeconds = 3.0f;
	PayloadHeightOffset = 5.0f;
}

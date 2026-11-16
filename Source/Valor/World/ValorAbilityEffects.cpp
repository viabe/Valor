#include "World/ValorAbilityEffects.h"

#include "AbilitySystem/Attributes/ValorCombatAttributeSet.h"
#include "AbilitySystem/ValorAgentTypes.h"
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

	for (TActorIterator<AValorCharacter> It(GetWorld()); It; ++It)
	{
		AValorPlayerController* ValorController = Cast<AValorPlayerController>(It->GetController());
		if (!ValorController || !It->IsAlive())
		{
			continue;
		}

		// 시선(서버가 아는 조준 방향)과 섬광 방향 사이 각도. 정면에 가까울수록 오래 눈부시다.
		const FVector Eye = It->GetPawnViewLocation();
		const FVector ToFlash = (GetActorLocation() - Eye).GetSafeNormal();
		const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(It->GetBaseAimRotation().Vector() | ToFlash, -1.0f, 1.0f)));
		const bool bVisible = ValorAbilityEffects::HasLineOfSight(GetWorld(), GetActorLocation(), **It, this);
		const float Seconds = ValorAbilityRules::ComputeFlashSeconds(AngleDegrees, bVisible, FVector::Dist(Eye, GetActorLocation()), RadiusCm, FullBlindSeconds, PartialBlindSeconds);
		if (Seconds > 0.0f)
		{
			ValorController->ClientFlashed(Seconds);
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

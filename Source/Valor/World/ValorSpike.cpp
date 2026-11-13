#include "World/ValorSpike.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ValorCharacterMovementComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "ValorCharacter.h"
#include "ValorGameState.h"
#include "ValorMatchGameMode.h"
#include "ValorMatchTypes.h"
#include "World/ValorSpikeSite.h"

AValorSpike::AValorSpike()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(true);
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// 임시 모양: 엔진 기본 원기둥(지름 30cm, 높이 35cm). 모델이 생기면 BP에서 바꾼다.
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, 17.5f));
	Mesh->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.35f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CylinderMesh.Object);
	}
}

void AValorSpike::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValorSpike, State);
	DOREPLIFETIME(AValorSpike, Carrier);
	DOREPLIFETIME(AValorSpike, Defuser);
	DOREPLIFETIME(AValorSpike, PlantProgress);
	DOREPLIFETIME(AValorSpike, DefuseProgress);
	DOREPLIFETIME(AValorSpike, DefuseCheckpoint);
	DOREPLIFETIME(AValorSpike, PlantedSiteName);
}

AValorSpike* AValorSpike::FindSpike(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AValorSpike> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool AValorSpike::IsCharacterUsingSpike(const AValorCharacter& Character)
{
	const UValorCharacterMovementComponent* Movement = Cast<UValorCharacterMovementComponent>(Character.GetCharacterMovement());
	if (!Movement || !Movement->WantsToUseSpike())
	{
		return false;
	}

	const AValorSpike* Spike = FindSpike(Character.GetWorld());
	return Spike && Spike->CanBeUsedBy(Character);
}

bool AValorSpike::CanBeUsedBy(const AValorCharacter& Character) const
{
	const UWorld* World = GetWorld();
	const AValorGameState* ValorGameState = World ? World->GetGameState<AValorGameState>() : nullptr;
	if (!ValorGameState || !Character.IsAlive())
	{
		return false;
	}

	if (State == EValorSpikeState::Carried)
	{
		// 설치: 소지자가 교전 중에, 땅 위에서, 발이 지점 상자 안에 있을 때.
		const UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
		const float HalfHeight = Character.GetCapsuleComponent() ? Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
		return Carrier == &Character
			&& ValorGameState->GetRoundPhase() == EValorRoundPhase::Combat
			&& Movement && Movement->IsMovingOnGround()
			&& AValorSpikeSite::FindSiteContaining(World, Character.GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight)) != nullptr;
	}

	if (State == EValorSpikeState::Planted)
	{
		// 해체: 설치 뒤, 수비 팀이, 가까이에서. 이미 다른 사람이 해체 중이면 안 된다(한 명씩).
		const FVector Offset = Character.GetActorLocation() - GetActorLocation();
		return ValorGameState->GetRoundPhase() == EValorRoundPhase::SpikePlanted
			&& ValorTeams::GetTeamOf(&Character) != ValorGameState->GetAttackingTeam()
			&& ValorTeams::GetTeamOf(&Character) != EValorTeam::None
			&& Offset.Size2D() <= DefuseRadiusCm && FMath::Abs(Offset.Z) <= 200.0f
			&& (!Defuser || Defuser == &Character);
	}

	return false;
}

void AValorSpike::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		TickServer(DeltaSeconds);
	}
}

void AValorSpike::TickServer(float DeltaSeconds)
{
	const UWorld* World = GetWorld();
	const AValorGameState* ValorGameState = World ? World->GetGameState<AValorGameState>() : nullptr;
	if (!ValorGameState)
	{
		return;
	}

	switch (State)
	{
	case EValorSpikeState::Carried:
	{
		if (!Carrier || !Carrier->IsAlive())
		{
			DropAt(Carrier ? Carrier->GetActorLocation() : GetActorLocation());
			return;
		}

		// 소지 중에는 소지자를 따라간다(떨어뜨릴 때 위치, HUD 표시용).
		SetActorLocation(Carrier->GetActorLocation());
		PlantProgress = ValorSpikeRules::AdvancePlant(PlantProgress, DeltaSeconds, IsCharacterUsingSpike(*Carrier));
		if (PlantProgress >= PlantSeconds)
		{
			PlantAt(*Carrier);
		}
		break;
	}

	case EValorSpikeState::Dropped:
		// 공격 팀 누구든 밟으면 줍는다.
		for (TActorIterator<AValorCharacter> It(World); It; ++It)
		{
			if (It->IsAlive() && ValorTeams::GetTeamOf(*It) == ValorGameState->GetAttackingTeam()
				&& FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) <= PickupRadiusCm)
			{
				GiveTo(*It);
				break;
			}
		}
		break;

	case EValorSpikeState::Planted:
	{
		// 해체하는 사람: 지금 해체 중인 사람이 계속하면 그대로, 아니면 4 키를 누른 수비 중 첫 사람.
		if (Defuser && !IsCharacterUsingSpike(*Defuser))
		{
			Defuser = nullptr;
		}
		if (!Defuser)
		{
			for (TActorIterator<AValorCharacter> It(World); It; ++It)
			{
				if (IsCharacterUsingSpike(**It))
				{
					Defuser = *It;
					break;
				}
			}
		}

		DefuseProgress = ValorSpikeRules::AdvanceDefuse(DefuseProgress, DefuseCheckpoint, DeltaSeconds, Defuser != nullptr, DefuseSeconds);
		if (Defuser && DefuseProgress >= DefuseSeconds)
		{
			AValorCharacter* SuccessfulDefuser = Defuser;
			State = EValorSpikeState::Defused;
			Defuser = nullptr;
			RefreshVisibility();
			if (AValorMatchGameMode* MatchGameMode = GetWorld()->GetAuthGameMode<AValorMatchGameMode>())
			{
				MatchGameMode->HandleSpikeDefused(SuccessfulDefuser);
			}
		}
		break;
	}

	default:
		break;
	}
}

FVector AValorSpike::FindGroundBelow(const FVector& Location) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ValorSpikeGround), false, this);
	for (TActorIterator<AValorCharacter> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}

	if (GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0.0f, 0.0f, 50.0f), Location - FVector(0.0f, 0.0f, 500.0f), ECC_WorldStatic, Params))
	{
		return Hit.ImpactPoint;
	}
	return Location;
}

void AValorSpike::PlantAt(AValorCharacter& Planter)
{
	const float HalfHeight = Planter.GetCapsuleComponent() ? Planter.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
	const FVector Feet = Planter.GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight);
	const AValorSpikeSite* Site = AValorSpikeSite::FindSiteContaining(GetWorld(), Feet);

	State = EValorSpikeState::Planted;
	Carrier = nullptr;
	PlantProgress = 0.0f;
	DefuseProgress = 0.0f;
	DefuseCheckpoint = 0.0f;
	PlantedSiteName = Site ? Site->GetSiteName() : NAME_None;
	SetActorLocation(FindGroundBelow(Feet));
	RefreshVisibility();

	if (AValorMatchGameMode* MatchGameMode = GetWorld()->GetAuthGameMode<AValorMatchGameMode>())
	{
		MatchGameMode->HandleSpikePlanted(&Planter);
	}
}

void AValorSpike::GiveTo(AValorCharacter* NewCarrier)
{
	if (!HasAuthority() || !NewCarrier)
	{
		return;
	}

	State = EValorSpikeState::Carried;
	Carrier = NewCarrier;
	PlantProgress = 0.0f;
	SetActorLocation(NewCarrier->GetActorLocation());
	RefreshVisibility();
}

void AValorSpike::DropAt(const FVector& Location)
{
	if (!HasAuthority())
	{
		return;
	}

	State = EValorSpikeState::Dropped;
	Carrier = nullptr;
	PlantProgress = 0.0f;
	SetActorLocation(FindGroundBelow(Location));
	RefreshVisibility();
}

void AValorSpike::Detonate(float RadiusCm)
{
	if (!HasAuthority() || State != EValorSpikeState::Planted)
	{
		return;
	}

	State = EValorSpikeState::Detonated;
	Defuser = nullptr;
	RefreshVisibility();

	TArray<AValorCharacter*> Victims;
	for (TActorIterator<AValorCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && FVector::Dist(It->GetActorLocation(), GetActorLocation()) <= RadiusCm)
		{
			Victims.Add(*It);
		}
	}
	for (AValorCharacter* Victim : Victims)
	{
		Victim->KillByEnvironment(this);
	}
}

void AValorSpike::OnRep_State()
{
	RefreshVisibility();
}

void AValorSpike::RefreshVisibility()
{
	// 소지 중·폭발 뒤에는 숨긴다(땅에 떨어졌거나 설치됐을 때만 보인다).
	Mesh->SetVisibility(State == EValorSpikeState::Dropped || State == EValorSpikeState::Planted || State == EValorSpikeState::Defused);
}

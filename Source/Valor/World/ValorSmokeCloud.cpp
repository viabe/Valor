#include "World/ValorSmokeCloud.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AValorSmokeCloud::AValorSmokeCloud()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	auto SetupSphere = [&](UStaticMeshComponent* Component, float SignX)
	{
		Component->SetupAttachment(RootComponent);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->CastShadow = false;
		if (SphereMesh.Succeeded())
		{
			Component->SetStaticMesh(SphereMesh.Object);
		}
		// 기본 구 메시는 지름 100cm. X를 음수로 뒤집으면 면이 안쪽을 향해 안에서 보인다.
		Component->SetRelativeScale3D(FVector(SignX * RadiusCm / 50.0f, RadiusCm / 50.0f, RadiusCm / 50.0f));
	};

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetupSphere(Mesh, 1.0f);
	InnerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("InnerMesh"));
	SetupSphere(InnerMesh, -1.0f);
}

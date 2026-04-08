#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValorWeaponPickup.generated.h"

class AValorCharacter;
class AValorWeaponBase;
class USphereComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

UCLASS()
class VALOR_API AValorWeaponPickup : public AActor
{
	GENERATED_BODY()

public:
	AValorWeaponPickup();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	AValorWeaponBase* SpawnWeaponForPickup(AValorCharacter* PickingCharacter);

	bool IsPickupAvailable() const { return bIsAvailable; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Valor|Pickup")
	USphereComponent* InteractionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Valor|Pickup")
	UStaticMeshComponent* PickupStaticMesh;

	// 스켈레탈 총기 에셋을 그대로 바닥 픽업에 표시할 수 있도록 별도 메시를 둔다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Valor|Pickup")
	USkeletalMeshComponent* PickupSkeletalMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Pickup")
	TSubclassOf<AValorWeaponBase> WeaponClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Pickup")
	bool bDestroyOnPickup = true;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Pickup")
	bool bIsAvailable = true;
};

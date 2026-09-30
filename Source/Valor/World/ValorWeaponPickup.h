#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValorWeaponPickup.generated.h"

class AValorCharacter;
class AValorWeaponBase;
class USphereComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * 바닥에 놓인 총(월드 아이템). 손에 든 총(AValorWeaponBase)과 분리된 "줍기 전용" 표현이다.
 *
 * - 레벨에 배치한 픽업: 블루프린트에서 WeaponClass와 표시 메시를 지정한다.
 * - 떨어뜨린 총: 총을 교체할 때 전투 컴포넌트가 서버에서 스폰한다. 총 종류와 탄약을 그대로 넘겨받고,
 *   표시 메시는 무기 클래스의 메시를 그대로 쓴다(총마다 픽업 블루프린트를 만들 필요가 없다).
 *
 * 네트워크: 스폰과 줍기는 서버만 처리한다. 클라에는 줍기 가능 여부와 무기 클래스(표시 메시용)만 복제하고,
 *   탄약은 서버에만 둔다(주운 뒤에는 무기 액터의 탄약으로 복제된다).
 * 트레이드오프: 떨어뜨릴 때 무기 액터를 파괴하고 픽업을 새로 만든다. 무기 액터를 그대로 바닥에 두는 방식보다
 *   탄약을 한 번 옮겨야 하지만, "손에 든 총"과 "바닥의 총"의 복제·충돌·관련성 규칙을 따로 가져갈 수 있다.
 */
UCLASS()
class VALOR_API AValorWeaponPickup : public AActor
{
	GENERATED_BODY()

public:
	AValorWeaponPickup();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 서버 전용: 픽업에 해당하는 총을 스폰한다. 떨어뜨린 총이면 그때의 탄약을 되돌린다. 성공하면 픽업은 사라진다.
	AValorWeaponBase* SpawnWeaponForPickup(AValorCharacter* PickingCharacter);

	// 서버 전용: 손에 들고 있던 총을 이 픽업으로 옮긴다(총 종류 + 탄약). 스폰 직후, 첫 복제 전에 호출한다.
	void InitializeFromDroppedWeapon(const AValorWeaponBase& DroppedWeapon);

	bool IsPickupAvailable() const { return bIsAvailable; }

	// 조준으로 고를 수 있는 반경(cm) = 줍기 구의 반지름.
	float GetInteractionRadius() const;

	// 줍기 후보 판정(순수 함수). 대상이 시점 앞쪽 MaxDistance 이내이고 조준선과의 거리가 InteractionRadius 이하이면 true.
	// OutDistanceFromAimLine = 조준선과 대상 중심의 거리(작을수록 크로스헤어 정중앙). ViewDirection은 단위 벡터여야 한다.
	static bool ComputeAimOffset(const FVector& ViewLocation, const FVector& ViewDirection, const FVector& TargetLocation,
		float InteractionRadius, float MaxDistance, float& OutDistanceFromAimLine);

protected:
	// 줍기 범위(조준 판정용). 충돌은 없다: 사격 트레이스(Visibility)가 바닥의 총 주변에서 막히지 않게 하기 위해서다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Valor|Pickup")
	USphereComponent* InteractionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Valor|Pickup")
	UStaticMeshComponent* PickupStaticMesh;

	// 스켈레탈 총기 에셋을 그대로 바닥 픽업에 표시할 수 있도록 별도 메시를 둔다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Valor|Pickup")
	USkeletalMeshComponent* PickupSkeletalMesh;

	// 주우면 생기는 총. 떨어뜨린 총은 서버가 런타임에 채우므로 복제한다(클라가 표시 메시를 고를 때 쓴다).
	UPROPERTY(EditDefaultsOnly, ReplicatedUsing=OnRep_WeaponClass, BlueprintReadOnly, Category="Valor|Pickup")
	TSubclassOf<AValorWeaponBase> WeaponClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Pickup")
	bool bDestroyOnPickup = true;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category="Valor|Pickup")
	bool bIsAvailable = true;

	// 무기 메시로 표시할 때 바닥에 눕히는 회전과 높이. 무기 메시의 축 방향에 맞춰 조정한다(기본: 옆으로 눕힘).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Pickup|Display")
	FRotator WeaponMeshDisplayRotation = FRotator(0.0f, 0.0f, 90.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Pickup|Display")
	FVector WeaponMeshDisplayOffset = FVector(0.0f, 0.0f, 4.0f);

	UFUNCTION()
	void OnRep_WeaponClass();

private:
	// 표시 메시가 비어 있으면 무기 클래스의 메시로 채운다. 블루프린트에서 직접 지정한 표시 메시는 건드리지 않는다.
	void RefreshDisplayMesh();

	// 서버 전용: 떨어뜨린 총의 탄약. INDEX_NONE이면 새 총(데이터 에셋 기본 탄약)으로 스폰한다.
	int32 StoredMagazineAmmo = INDEX_NONE;
	int32 StoredReserveAmmo = INDEX_NONE;

	// 표시 메시를 무기 클래스에서 가져와 채웠는지(무기 클래스가 바뀌면 다시 채우기 위함).
	bool bDisplayMeshFromWeapon = false;
};

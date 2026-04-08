#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "ValorCombatComponent.generated.h"

class AValorCharacter;
class AValorWeaponBase;
class AValorWeaponPickup;
class UGameplayAbility;
class UValorAbilitySystemComponent;
class UValorCombatAttributeSet;

UCLASS(ClassGroup=(Valor), meta=(BlueprintSpawnableComponent))
class VALOR_API UValorCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UValorCombatComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitializeAbilityBindings(UValorAbilitySystemComponent* InAbilitySystemComponent);

	void HandleFireInputPressed();
	void HandleFireInputReleased();
	void HandleReloadInputPressed();
	void HandleADSInputPressed();
	void HandleADSInputReleased();
	void HandleInteractInputPressed();

	void ExecuteServerFireAbility();
	void ExecuteServerReloadAbility();
	void SetADSStateFromAbility(bool bNewADS);

	AValorWeaponBase* GetEquippedWeapon() const { return EquippedWeapon; }
	bool IsADSActive() const { return bIsADS; }
	bool IsReloading() const { return bIsReloading; }
	bool IsFireInputHeld() const { return bLocalFireHeld; }
	float GetLastFireSimulationWorldTime() const { return LastFireSimulationWorldTime; }

protected:
	UPROPERTY(ReplicatedUsing=OnRep_EquippedWeapon, VisibleInstanceOnly, Category="Valor|Combat")
	TObjectPtr<AValorWeaponBase> EquippedWeapon;

	UPROPERTY(ReplicatedUsing=OnRep_IsADS, VisibleInstanceOnly, Category="Valor|Combat")
	bool bIsADS = false;

	UPROPERTY(ReplicatedUsing=OnRep_IsReloading, VisibleInstanceOnly, Category="Valor|Combat")
	bool bIsReloading = false;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat")
	TSubclassOf<UGameplayAbility> FireAbilityClass;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat")
	TSubclassOf<UGameplayAbility> ReloadAbilityClass;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Combat")
	TSubclassOf<UGameplayAbility> ADSAbilityClass;

	UFUNCTION(Server, Unreliable)
	void ServerRequestFire(float ClientShotTimestampSeconds);

	UFUNCTION(Server, Reliable)
	void ServerRequestReload();

	UFUNCTION(Server, Reliable)
	void ServerSetADSInput(bool bNewADS);

	UFUNCTION(Server, Reliable)
	void ServerInteractWithPickup();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSimulateFire(FVector_NetQuantize TraceEnd, FRotator RecoilKick, bool bDidHitCharacter);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayReloadCue();

	UFUNCTION()
	void OnRep_EquippedWeapon(AValorWeaponBase* PreviousWeapon);

	UFUNCTION()
	void OnRep_IsADS();

	UFUNCTION()
	void OnRep_IsReloading();

private:
	void RequestSingleFireShot();
	void HandleLocalAutomaticFire();
	void EquipWeapon(AValorWeaponBase* NewWeapon);
	void ApplyEquippedWeaponAttachment() const;
	void RefreshADSOnLocalClient() const;
	void FinishReload();
	AValorWeaponPickup* FindPickupInView() const;

	bool PerformServerHitScan(const FVector& TraceStart, const FVector& ShotDirection, float MaxDistance, float ClientShotTimestampSeconds, AValorCharacter*& OutHitCharacter, FVector& OutImpactPoint, float& OutTravelDistance, bool& bOutPenetratedSurface) const;

	UPROPERTY()
	TObjectPtr<AValorCharacter> OwnerCharacter;

	UPROPERTY()
	TObjectPtr<UValorAbilitySystemComponent> AbilitySystemComponent;

	FGameplayAbilitySpecHandle FireAbilityHandle;
	FGameplayAbilitySpecHandle ReloadAbilityHandle;
	FGameplayAbilitySpecHandle ADSAbilityHandle;

	FTimerHandle LocalAutomaticFireTimerHandle;
	FTimerHandle ReloadTimerHandle;

	float PendingClientShotTimestampSeconds = 0.0f;
	bool bLocalFireHeld = false;
	float LastFireSimulationWorldTime = -1000.0f;
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weapons/ValorViewmodelTypes.h"
#include "ValorMeleeWeapon.generated.h"

class AValorCharacter;
class USceneComponent;
class USkeletalMeshComponent;

/**
 * 근접 무기(발로란트 Tactical Knife, 3번 슬롯). 모든 캐릭터가 항상 하나씩 가지며 버리거나 줍지 않는다.
 *
 * 왜 총(AValorWeaponBase)과 다른 클래스인가: 총의 핵심(탄약·반동·스프레이·발사 검증)이 칼에는 전혀 없다.
 *   총 클래스에 "칼이면 건너뛰기" 분기를 넣는 대신, 슬롯 시스템이 필요로 하는 공통점(장착 시간, 이동 속도, 손 소켓 부착, 숨김)만
 *   각자 갖게 하고 전투 컴포넌트가 칼 슬롯을 따로 다룬다.
 * 수치 근거(Valorant 위키 Melee): 달리기 6.75m/s(총 배율 없음 = 가장 빠름), 장착 0.6초(Normal),
 *   피해 앞 50/75(좌/우클릭), 뒤 100/150, 사거리 2m.
 * 네트워크: 서버가 스폰하고 소유자 관련성(bNetUseOwnerRelevancy)으로 캐릭터와 같이 복제된다. 손에 들었는지(보임/숨김)는
 *   복제된 장착 상태로 각 머신이 직접 계산하므로 액터 숨김 플래그를 복제하지 않는다(소유 클라의 교체 예측과 충돌하지 않게).
 * 트레이드오프: 공격(좌/우클릭 베기)은 아직 없다. 공격 수치는 데이터로만 담아 두고, 다음 단계에서 랙 보상 근접 판정으로 붙인다.
 */
UCLASS()
class VALOR_API AValorMeleeWeapon : public AActor
{
	GENERATED_BODY()

public:
	AValorMeleeWeapon();

	// 블루프린트 컴포넌트까지 만들어진 뒤 충돌을 모두 끈다(총과 같은 이유, ValorInventory::DisableHeldWeaponCollision).
	virtual void PostInitializeComponents() override;
	// 총과 같이 서버의 부착 상태를 복제하지 않는다(AValorWeaponBase::PreReplication 참고).
	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;

	// 소유 캐릭터에 붙인다(모든 머신): 로컬 플레이어 화면이면 1인칭 뷰모델, 아니면 3인칭 손 소켓. 서버는 소유자/인스티게이터도 정한다.
	void OnEquippedBy(AValorCharacter* NewOwnerCharacter);

	// 총과 같은 규칙으로 이 머신의 시점에 맞게 다시 붙인다(빙의·컨트롤러 복제 뒤 호출).
	void RefreshViewAttachment(AValorCharacter* ViewOwnerCharacter);

	// 손에 들지 않은 동안 숨긴다(로컬 연출 전용).
	void SetHolstered(bool bNewHolstered);

	float GetEquipTime() const { return EquipTimeSeconds; }
	float GetMoveSpeedMultiplier() const { return MoveSpeedMultiplier; }
	const FText& GetDisplayName() const { return DisplayName; }
	const FValorViewmodelConfig& GetViewmodelConfig() const { return Viewmodel; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Valor|Melee")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Valor|Melee")
	TObjectPtr<USkeletalMeshComponent> MeleeMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Melee")
	FText DisplayName;

	// 꺼내는 데 걸리는 시간(초). 위키 Melee "Equip Speeds 0.6 sec (Normal)".
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Melee", meta=(ClampMin="0.0"))
	float EquipTimeSeconds = 0.6f;

	// 이동 속도 배율(칼 기준 달리기 6.75m/s 대비). 칼은 배율이 없다 = 1.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Melee", meta=(ClampMin="0.1", ClampMax="1.0"))
	float MoveSpeedMultiplier = 1.0f;

	// 1인칭 배치(총과 같은 구조). 칼은 조준이 없으므로 힙 값과 킥만 쓴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Melee|Viewmodel")
	FValorViewmodelConfig Viewmodel;

	// 손잡이 기준 뼈. 메시에 GripSocket이 있으면 그것을 먼저 쓴다. 기본 칼(Araxys) 리그의 손잡이 뼈는 이름이 "Root72"다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Melee|Attachment")
	FName GripBoneName = TEXT("Root72");

	// 칼 리그 축 → 총 리그 규칙(Z = 앞/날 끝, Y = 아래, X = 왼쪽) 보정 회전(손잡이 뼈 기준).
	// 왜: 1인칭 피벗과 3인칭 손 소켓은 총 리그 규칙에 맞춰져 있다. 칼 리그는 길이 방향이 Y축이라 그대로 붙이면 옆으로 눕는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Melee|Attachment")
	FRotator GripRotationOffset = FRotator::ZeroRotator;

	// === [구현 예정] 근접 공격 수치(위키 Melee). 다음 단계의 근접 판정이 읽는다. ===

	// 앞에서 맞혔을 때 피해(좌클릭 베기 / 우클릭 찌르기).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Melee|Attack", meta=(ClampMin="0.0"))
	float FrontDamage = 50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Melee|Attack", meta=(ClampMin="0.0"))
	float FrontAltDamage = 75.0f;

	// 등 뒤에서 맞혔을 때 피해(2배). 우클릭 뒤치기는 150이라 실드가 있어도 한 방에 쓰러진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Melee|Attack", meta=(ClampMin="0.0"))
	float BackDamage = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Melee|Attack", meta=(ClampMin="0.0"))
	float BackAltDamage = 150.0f;

	// 사거리(cm). 위키 "2m max".
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Melee|Attack", meta=(ClampMin="0.0"))
	float AttackRangeCm = 200.0f;

private:
	void RefreshMeshAlignment();
};

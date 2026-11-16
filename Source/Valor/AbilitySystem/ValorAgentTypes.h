#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ValorAgentTypes.generated.h"

class UValorAgentAbility;

// 요원 스킬 칸. 키: C / Q / E / X(장르 공통 배치). 순서가 PlayerState 충전 배열의 번호다.
UENUM(BlueprintType)
enum class EValorAbilitySlot : uint8
{
	Ability1,
	Ability2,
	Signature,
	Ultimate,
	Count UMETA(Hidden)
};

// 스킬 칸 하나의 설정.
USTRUCT(BlueprintType)
struct VALOR_API FValorAbilitySlotConfig
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Agent")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Agent")
	TSubclassOf<UValorAgentAbility> AbilityClass;

	// 한 번 충전 가격(크레딧). 0이면 상점에서 사지 않는다(시그니처·궁극기).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Agent", meta=(ClampMin="0"))
	int32 Cost = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Agent", meta=(ClampMin="0"))
	int32 MaxCharges = 1;

	// 매 라운드 시작에 이만큼은 채워 준다(시그니처 무료 충전).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Agent", meta=(ClampMin="0"))
	int32 FreeChargesPerRound = 0;

	// 궁극기: 필요한 궁극기 포인트. 0이면 충전 횟수로 쓴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Agent", meta=(ClampMin="0"))
	int32 UltimatePointsRequired = 0;
};

/**
 * 요원 정의(스킬 네 칸). 데이터만 있다. 요원마다 C++ 하위 클래스(기본값으로 칸 설정) 또는 이 클래스를 부모로 한 데이터 에셋/BP를 만든다.
 * PlayerState는 요원 클래스를 복제하고, 서버·클라 모두 그 클래스 기본값(CDO)에서 설정을 읽는다(에셋 경로 복제 없이 같은 데이터).
 */
UCLASS(BlueprintType, Blueprintable)
class VALOR_API UValorAgentDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	const FValorAbilitySlotConfig& GetSlot(EValorAbilitySlot Slot) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Agent")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Agent")
	FValorAbilitySlotConfig Ability1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Agent")
	FValorAbilitySlotConfig Ability2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Agent")
	FValorAbilitySlotConfig Signature;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Valor|Agent")
	FValorAbilitySlotConfig Ultimate;
};

// 스킬 경제 규칙(서버 게임 모드와 테스트가 같이 쓴다).
namespace ValorAbilityRules
{
	constexpr int32 SlotCount = static_cast<int32>(EValorAbilitySlot::Count);

	// 라운드 시작 충전: 경제 초기화 라운드(1·13라운드)면 무료 충전만, 아니면 가진 것을 유지하되 무료 충전만큼은 채운다.
	VALOR_API int32 ChargesAtRoundStart(int32 CurrentCharges, int32 FreeChargesPerRound, int32 MaxCharges, bool bEconomyReset);

	// 살 수 있나: 상점 판매 칸(Cost > 0), 최대 충전 미만, 크레딧 충분.
	VALOR_API bool CanBuyCharge(int32 CurrentCharges, int32 MaxCharges, int32 Cost, int32 Credits);

	// 궁극기 포인트 더하기(필요 포인트에서 멈춘다).
	VALOR_API int32 AddUltimatePoints(int32 Current, int32 Amount, int32 Required);

	// 요원 자동 배정: 같은 팀이 아직 안 쓴 첫 요원, 다 쓰였으면 첫 요원(봇·시간 초과용). 목록이 비면 nullptr.
	VALOR_API const UClass* PickFallbackAgent(const TArray<const UClass*>& Roster, const TArray<const UClass*>& TakenByTeam);

	// 섬광 눈부심 시간. 가림 없이 보이고 반경 안일 때: 시선과 60도 안 = 전부, 110도 안 = 일부, 그 밖(뒤돌아봄) = 없음.
	VALOR_API float ComputeFlashSeconds(float AngleDegrees, bool bLineOfSight, float DistanceCm, float RadiusCm, float FullSeconds, float PartialSeconds);
}

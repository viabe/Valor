#pragma once

#include "CoreMinimal.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"
#include "ValorInventoryTypes.generated.h"

class AActor;

// =====================================================================================================
// 무기 슬롯(인벤토리) 공용 타입
// 발로란트 규칙: 주무기(1) / 보조무기(2) / 근접(3, 칼)의 세 칸. 칼은 항상 있고 버릴 수 없다.
// 같은 종류의 총을 주우면 그 칸의 총을 바닥에 떨어뜨리고 바꾼다. 꺼내는 데는 무기마다 장착 시간이 걸리고,
// 장착이 끝나기 전에는 쏘거나 조준하거나 재장전할 수 없다.
// =====================================================================================================

UENUM(BlueprintType)
enum class EValorWeaponSlot : uint8
{
	Primary,
	Secondary,
	Melee
};

/**
 * 지금 손에 든 슬롯과 장착 시작 시각(서버 권위, 모두에게 복제).
 *
 * 왜 한 구조체인가: "어느 슬롯을, 언제부터 꺼내고 있나, 이 상태가 클라의 몇 번째 요청까지 반영했나"는 함께 바뀌어야 의미가 있다.
 *   따로 복제하면 슬롯은 새 값인데 시작 시각은 옛 값인 중간 상태가 OnRep에 보일 수 있다. 구조체 하나는 같은 묶음으로 도착한다.
 * 네트워크(소유 클라 예측):
 *   - 소유 클라는 1/2/3 입력 즉시 자기 사본(로컬 상태)을 바꿔 예측하고, 요청 번호를 붙여 서버에 보낸다.
 *   - 서버는 요청을 처리할 때마다 AckedRequestId를 그 번호로 올린다(거부해도 올린다 → 클라가 서버 결정으로 되돌아간다).
 *   - 소유 클라는 서버가 "내 마지막 요청까지 처리했다"고 알려 줄 때만 서버 상태를 받아들인다. 그 전의 복제값은
 *     아직 처리 안 된 요청 이전의 낡은 상태이므로 무시한다(빠르게 1→2를 눌렀을 때 1로 잠깐 되돌아가는 깜빡임 방지).
 *   - 줍기·버리기·사망처럼 서버가 먼저 바꾸는 경우는 요청 번호가 그대로이므로, 대기 중 요청이 없으면 바로 받아들여진다.
 * 트레이드오프: 요청 번호 1바이트를 모든 클라에게 보낸다(소유자 전용 조건은 구조체 멤버 단위로 걸 수 없다). 비용은 무시할 만하다.
 */
USTRUCT(BlueprintType)
struct FValorEquipState
{
	GENERATED_BODY()

	// 손에 든 슬롯. 시작은 칼(총을 지급받으면 서버가 주무기로 바꾼다).
	UPROPERTY(BlueprintReadOnly, Category="Valor|Inventory")
	EValorWeaponSlot ActiveSlot = EValorWeaponSlot::Melee;

	// 장착을 시작한 시각(서버 동기화 시간축, 초). 장착 완료 = 이 시각 + 그 무기의 장착 시간. 발사 시각 검증과 같은 시간축이다.
	UPROPERTY(BlueprintReadOnly, Category="Valor|Inventory")
	float EquipStartTime = -1000.0f;

	// 서버가 마지막으로 처리한 소유 클라의 교체 요청 번호(0~255 순환).
	UPROPERTY()
	uint8 AckedRequestId = 0;
};

namespace ValorInventory
{
	// 무기군 → 슬롯. 권총(Sidearm)만 보조무기 칸이고 나머지는 모두 주무기 칸이다(발로란트 상점 분류와 같다).
	VALOR_API EValorWeaponSlot SlotForCategory(EValorWeaponCategory Category);

	// 서버가 클라의 마지막 요청(LocalRequestId)까지 처리했는가. 1바이트 순환 번호라 차이를 부호 있는 8비트로 비교한다
	// (요청이 128개 넘게 밀리지 않는 한 255 → 0으로 넘어가도 올바르다).
	VALOR_API bool IsRequestAcknowledged(uint8 AckedRequestId, uint8 LocalRequestId);

	// 든 총이 없어졌을 때(버림·교체 실패) 꺼낼 슬롯: 주무기 > 보조무기 > 칼. 발로란트는 총을 버리면 남은 가장 좋은 무기를 든다.
	VALOR_API EValorWeaponSlot SelectFallbackSlot(bool bHasPrimary, bool bHasSecondary);

	// 휠 순환: 1 → 2 → 3 → 1 순서(Direction = +1) 또는 역순(-1). 비어 있는 슬롯은 건너뛴다(칼은 항상 있다).
	VALOR_API EValorWeaponSlot CycleSlot(EValorWeaponSlot Current, int32 Direction, bool bHasPrimary, bool bHasSecondary);

	// 클라가 주장한 교체 요청 시각을 서버 허용 범위 [서버 현재 - MaxRewind, 서버 현재]로 자른다.
	// 발사 시각과 같은 규칙: 너무 먼 과거를 주장해 장착 시간을 줄이는 조작을 되감기 한도 안으로 묶는다.
	VALOR_API float ClampEquipRequestTime(float ClaimedTime, double ServerNow, float MaxRewindSeconds);

	// 손에 드는 무기 액터(총·칼)의 모든 프리미티브 컴포넌트 충돌을 끈다. 블루프린트에서 추가한 컴포넌트도 포함한다.
	// 왜: 캐릭터 캡슐의 이동 스윕은 자기 액터만 무시하고, 손에 붙은 무기는 별도 액터라 무시하지 않는다.
	//   무기 BP에 충돌이 켜진 컴포넌트가 있으면(StaticMesh 컴포넌트는 기본이 BlockAllDynamic) 캡슐이 그 안에서 시작해
	//   매 이동마다 밀려나고, 무기도 따라 움직여 다시 겹친다. 그 결과 캐릭터가 비정상적으로 빠르게 미끄러진다(오퍼레이터 BP의 부품 메시).
	//   카메라 사격 트레이스도 소유 캐릭터만 무시하므로 자기 총에 맞을 수 있다.
	// 트레이드오프: 무기는 시각 전용이 된다. 판정은 모두 카메라 트레이스와 캐릭터 히트박스로 하므로 무기 충돌이 필요한 곳이 없다.
	//   바닥에 떨어진 총은 별도 줍기 액터(AValorWeaponPickup)가 자기 충돌을 가진다.
	// 네트워크: 충돌은 복제되지 않는 로컬 설정이므로 서버·모든 클라에서 각자 호출해야 한다(PostInitializeComponents).
	VALOR_API void DisableHeldWeaponCollision(AActor& WeaponActor);
}

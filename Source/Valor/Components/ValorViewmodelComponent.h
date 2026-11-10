#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Components/ValorCameraKickSpring.h"
#include "ValorViewmodelComponent.generated.h"

class AValorCharacter;
class UAnimSequence;
class USkeletalMesh;
class USkeletalMeshComponent;
class UValorCombatComponent;
struct FValorViewmodelConfig;

/**
 * 1인칭 뷰모델 피벗(로컬 플레이어 전용). FollowCamera의 자식이며, 내 화면의 총·칼이 여기에 붙는다.
 *
 * 책임: 매 프레임 피벗 자세 = 무기별 힙/조준 위치 + 장착(아래에서 올라옴) + 재장전(내림) + 시선 흔들림(sway)
 *   + 걸음 흔들림(bob) + 사격 킥. 총 액터는 피벗 원점에 손잡이가 오도록 이미 정렬돼 있으므로, 피벗만 움직이면 된다.
 *
 * 왜 총 액터를 하나만 쓰는가(1인칭 전용 복사본을 따로 만들지 않음):
 *   부착은 복제되지 않고 머신마다 각자 정한다. 사수 머신에서는 이 피벗에, 다른 머신에서는 3인칭 손에 붙이면 같은 액터로 두 시점이 된다.
 *   메시를 복사하면 BP에서 추가한 부품, 총구 화염 위치, 숨김 규칙을 두 벌로 관리해야 한다.
 *   대가: 한 머신에서 한 캐릭터의 총은 한 시점으로만 그려진다(분할 화면 2인 플레이는 지원하지 않는다).
 *
 * 축소 배율(ViewmodelScale): 피벗을 배율 s로 줄이고 위치도 s배로 카메라 쪽에 당긴다.
 *   카메라(눈) 중심으로 같은 비율로 줄인 물체는 화면에 똑같이 보인다(원근 투영은 눈 기준 배율에 불변).
 *   그래서 구도는 실제 크기 cm로 정하면서, 총 전체가 캡슐 반경(42cm) 안에 들어와 벽을 뚫고 보이지 않는다.
 *   대가: 근평면도 실제 크기 기준으로 1/s배 멀어진 셈이라 DefaultEngine.ini의 NearClipPlane을 낮춰 둔다.
 *
 * 1인칭 팔(First Person 템플릿 SK_Mannequin_Arms): 로컬 플레이어에게만 런타임에 만들어 카메라에 붙인다.
 *   팔이 총을 움직이는 게 아니라, 총(이 피벗)이 정한 자리로 두 손이 IK로 따라온다(UValorFirstPersonArmsAnimInstance).
 *   그래서 힙·조준·장착·재장전·흔들림·킥이 모두 총 기준으로 한 번만 계산되고, 총마다 다른 길이·손잡이 위치에도 손이 맞는다.
 *   팔 크기는 총과 같은 비율로 키운다(발로란트 총 메시는 실물의 약 1.45배라 템플릿 팔 그대로는 왼손이 총열 앞에 닿지 않는다).
 *
 * 네트워크: 없음. 순수 로컬 연출이다. 입력값(조준·재장전·장착 진행)은 전투 컴포넌트의 예측 상태를 그대로 읽으므로
 *   교체·조준 입력 즉시 반응한다. 판정은 카메라 기준이라 총 위치와 무관하다.
 */
UCLASS(ClassGroup=(Valor), meta=(BlueprintSpawnableComponent))
class VALOR_API UValorViewmodelComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UValorViewmodelComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 로컬 플레이어 화면일 때만 켠다(꺼지면 Tick도 멈춘다). 캐릭터가 빙의·컨트롤러 복제·사망 때 호출한다.
	void SetFirstPersonActive(bool bActive);
	bool IsFirstPersonActive() const { return bFirstPersonActive; }

	// 로컬 사수가 한 발 쏠 때. 총이 뒤로 밀리고 총구가 들렸다가 스프링으로 돌아온다(카메라 킥과 별개의 총 연출).
	void AddFireKick(const FValorViewmodelConfig& Config, bool bIsADS);

	float GetViewmodelScale() const { return ViewmodelScale; }

private:
	// 지금 손에 든 물건의 배치 정보. 총이면 조준 가능 여부·조준선 높이가 있고, 칼은 힙 자세만 쓴다.
	struct FActiveViewmodelItem
	{
		const UObject* Item = nullptr;
		const FValorViewmodelConfig* Config = nullptr;
		float SightHeightCm = 0.0f;
		float ADSInterpSpeed = 18.0f;
		bool bCanADS = false;
	};

	bool ResolveActiveItem(const UValorCombatComponent& CombatComponent, FActiveViewmodelItem& OutItem) const;

	// 1인칭 팔: 처음 켤 때 만든다(에셋이 없으면 팔 없이 총만 보인다). 매 프레임 손 목표를 넘기고 보임/숨김을 정한다.
	void EnsureArmsComponent();
	void UpdateArms(const FActiveViewmodelItem* ActiveItem, const UValorCombatComponent* CombatComponent);
	void UpdateSway(float DeltaTime, const AValorCharacter& Character);
	void UpdateBob(float DeltaTime, const AValorCharacter& Character);
	void ResetMotionState();

	// 축소 배율(위 설명). 0.35면 밴달 총구(힙 자세 약 1.2m 앞)가 카메라 앞 약 42cm에 온다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel", meta=(ClampMin="0.05", ClampMax="1.0"))
	float ViewmodelScale = 0.35f;

	// 장착 시작 때 총이 내려가 있는 거리(cm)와 총구가 숙여진 각도(도). 장착 진행도에 맞춰 제자리로 올라온다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Equip")
	float EquipDropCm = 16.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Equip")
	float EquipPitchDegrees = 35.0f;

	// 재장전 중 자세(장전 애니메이션이 생기기 전의 대체 연출): 총을 내리고 기울인다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Reload")
	float ReloadDropCm = 5.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Reload")
	float ReloadPitchDegrees = 12.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Reload")
	float ReloadRollDegrees = 25.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Reload", meta=(ClampMin="0.1"))
	float ReloadBlendSpeed = 10.0f;

	// 시선 흔들림: 마우스로 돌리는 속도(도/초)에 이 값을 곱한 만큼 총이 반대로 뒤처진다. 상한 MaxSwayDegrees.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Sway", meta=(ClampMin="0.0"))
	float SwayDegreesPerDegreePerSecond = 0.008f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Sway", meta=(ClampMin="0.0"))
	float MaxSwayDegrees = 4.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Sway", meta=(ClampMin="0.1"))
	float SwayInterpSpeed = 10.0f;

	// 흔들림 각도 1도당 총이 옆으로 밀리는 거리(cm).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Sway", meta=(ClampMin="0.0"))
	float SwayTranslationCmPerDegree = 0.3f;

	// 조준 중 흔들림 배율(조준기가 화면 중앙을 크게 벗어나지 않게).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Sway", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ADSSwayScale = 0.2f;

	// 걸음 흔들림: 최고 달리기 속도에서 초당 이만큼 걸음(좌우 한 번 = 1주기). 속도에 비례해 느려진다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Bob", meta=(ClampMin="0.0"))
	float BobFrequencyHz = 1.8f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Bob", meta=(ClampMin="0.0"))
	float BobHorizontalCm = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Bob", meta=(ClampMin="0.0"))
	float BobVerticalCm = 0.4f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Bob", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ADSBobScale = 0.1f;

	// === 1인칭 팔 ===

	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Arms")
	TSoftObjectPtr<USkeletalMesh> ArmsMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/FirstPersonArms/Character/Mesh/SK_Mannequin_Arms.SK_Mannequin_Arms")));

	// 기본 자세(손가락 모양, 팔꿈치 방향). 총을 들면 소총 대기, 칼만 들면 맨손 대기(왼팔).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Arms")
	TSoftObjectPtr<UAnimSequence> ArmsRiflePose = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/FirstPersonArms/Animations/FP_Rifle_Idle.FP_Rifle_Idle")));

	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Arms")
	TSoftObjectPtr<UAnimSequence> ArmsIdlePose = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/FirstPersonArms/Animations/FP_Idle.FP_Idle")));

	// 팔 크기(템플릿 대비). 총 메시와 같은 비율이라 손 크기가 총에 맞고 팔 길이가 총열 앞까지 닿는다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Arms", meta=(ClampMin="0.5", ClampMax="3.0"))
	float ArmsScale = 1.45f;

	// 템플릿 배치(카메라 기준 -30, 0, -151cm)에 더하는 이동(cm, 뷰 공간 실제 크기). 어깨를 화면 밖에서 앞으로 당겨 총에 닿게 한다.
	// 어깨는 카메라 가까이 화면 가장자리 밖에 있으므로 앞으로 당겨도 보이지 않는다(보이는 것은 팔뚝과 손).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Arms")
	FVector ArmsShift = FVector(35.0f, 0.0f, -6.0f);

	// 왼손(손목 뼈) 위치 보정(cm, 총 리그 공간: X 총의 왼쪽, Y 아래, Z 앞). Left_Hand_Target은 총 중심선 위의 점이라
	// 손목을 그대로 두면 손 전체가 총 오른쪽으로 밀려 손가락이 총열을 뚫는다. 템플릿 소총 자세에서 왼손목은 총 중심선보다
	// 약 6.9cm 왼쪽에 있으므로(팔 크기 1.45배 → 약 10cm) 그만큼 왼쪽으로 옮긴다. 또 Left_Hand_Target은 총열 높이에 가까워
	// 그대로면 손이 총 위를 덮어 쥐므로, 6cm 내려 총 아래를 받쳐 쥐게 한다(발로란트 자세).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Arms")
	FVector LeftHandOffset = FVector(10.0f, 6.0f, 0.0f);

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ArmsComponent;

	bool bArmsLoadAttempted = false;

	// 연사 중 킥 누적 상한.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Kick", meta=(ClampMin="0.0"))
	float KickMaxDegrees = 8.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Viewmodel|Kick", meta=(ClampMin="0.0"))
	float KickMaxBackCm = 6.0f;

	bool bFirstPersonActive = false;

	// 지금 든 물건(바뀌면 조준·킥 상태를 초기화한다).
	TWeakObjectPtr<const UObject> LastActiveItem;

	float ADSAlpha = 0.0f;
	float ReloadAlpha = 0.0f;

	FRotator LastControlRotation = FRotator::ZeroRotator;
	bool bHasLastControlRotation = false;
	FRotator SmoothedSway = FRotator::ZeroRotator;

	float BobPhase = 0.0f;
	float BobAmount = 0.0f;

	// 사격 킥: 회전(X = Pitch, Y = Yaw)과 뒤로 밀림(X = cm). 카메라 킥과 같은 임계 감쇠 스프링 해석해.
	FValorCameraKickSpring KickRotationSpring;
	FValorCameraKickSpring KickTranslationSpring;
};

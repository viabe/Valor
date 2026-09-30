#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/ValorCameraKickSpring.h"
#include "ValorCameraComponent.generated.h"

class ACharacter;
class UCameraComponent;
class USpringArmComponent;
struct FValorCameraKickConfig;

/**
 * 1인칭 카메라 연출 컴포넌트(로컬 플레이어 전용).
 *
 * 책임: FPS 카메라 설정, ADS FOV 보간, 사격 카메라 반동 연출.
 * 사격 카메라 반동은 발로란트와 같이 두 층을 더한 값이다.
 *   (1) 패턴 추종: 스프레이 상태에서 "다음 탄의 반동 위치 × CameraRecoilFollowRatio"를 매 프레임 계산.
 *       힙파이어 0.5(실제 게임 영상 측정: 탄 약 8.5° 상승 시 카메라 약 4.2~4.9°), ADS 1("Crosshair follows recoil").
 *   (2) 카메라 킥: 매 발 화면이 위로 톡 튀었다가 임계 감쇠 스프링으로 곧바로 가라앉는 톱니 성분.
 * 네트워크: 순수 로컬 연출이다. FollowCamera 상대 회전만 바꾸고 컨트롤 회전(조준 입력)은 건드리지 않으므로
 *   서버 탄도(컨트롤 회전 + 서버 계산 반동)와 섞이지 않는다. 비로컬 캐릭터에서는 Tick 자체를 끈다.
 * 트레이드오프: 카메라가 조준에서 잠시 벗어나므로 킥이 큰 순간에는 화면 중앙과 실제 탄 방향이 조금 다르다
 *   (발로란트/CS의 화면 반동과 같은 성질이며, 스프링이 곧바로 복귀시킨다).
 */
UCLASS(ClassGroup=(Valor), meta=(BlueprintSpawnableComponent))
class VALOR_API UValorCameraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UValorCameraComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// FPS 시점에 맞게 카메라 붐과 카메라 설정을 적용한다.
	void ConfigureFirstPersonView(ACharacter* OwningCharacter, USpringArmComponent* CameraBoom, UCameraComponent* FollowCamera);

	// 로컬 소유자에게만 1인칭 프레젠테이션을 적용하고, 로컬이 아니면 Tick을 꺼서 서버/원격 캐릭터 비용을 없앤다.
	void RefreshLocalPresentation(ACharacter* OwningCharacter, bool bIsLocallyControlled);

	// ADS는 로컬 카메라 연출만 즉시 반영하고, 실제 명중 판정은 서버 상태를 따른다.
	void SetADSState(bool bNewADS, float ADSFieldOfView, float InterpSpeed);

	// 로컬 사수가 한 발 쏠 때(예측 클라/리슨 호스트) 호출된다. 화면을 위로 튀게 하는 킥을 더한다.
	void AddFireKick(const FValorCameraKickConfig& KickConfig);

	// 지금 FollowCamera에 적용된 총 오프셋(패턴 추종 + 킥). HUD 디버그가 "화면 중앙 대비 실제 탄 위치"를 그릴 때 쓴다.
	FRotator GetAppliedViewOffset() const { return AppliedViewOffset; }

	float GetHipFireFOV() const { return HipFireFOV; }

private:
	void UpdateRecoilCamera(float DeltaTime);

	UPROPERTY(EditDefaultsOnly, Category="Valor|Camera")
	FVector CameraSocketOffset = FVector(0.0f, 0.0f, 64.0f);

	// 발로란트 고정 수평 FOV(103도, 16:9). ADS FOV는 무기 줌 배율(밴달 1.25배)로 여기서 계산한다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Camera", meta=(ClampMin="60.0", ClampMax="130.0"))
	float HipFireFOV = 103.0f;

	// 패턴 추종 카메라가 목표 오프셋을 따라 "올라갈 때" 보간 속도. 발사마다 계단식으로 튀지 않고 2~3프레임 안에 붙는다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Camera", meta=(ClampMin="1.0"))
	float RecoilCameraInterpSpeed = 40.0f;

	// 사격을 멈춘 뒤 카메라가 "돌아올 때" 보간 속도. 실제 게임 영상의 복귀 곡선(약 0.45초에 걸친 완만한 하강)에 맞춘 값이다.
	// 탄도 회복(Gun Recovery Time 0.375초)은 그대로 두고 카메라만 살짝 부드럽게 따라오게 하는 연출 값이다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Camera", meta=(ClampMin="1.0"))
	float RecoilCameraReturnInterpSpeed = 20.0f;

	TWeakObjectPtr<UCameraComponent> CachedFollowCamera;
	float CurrentTargetFOV = 103.0f;
	float ADSInterpSpeed = 18.0f;
	bool bIsADSActive = false;

	// (1) 패턴 추종 오프셋(도).
	FRotator CurrentViewRecoil = FRotator::ZeroRotator;

	// (2) 사격 카메라 킥 스프링과 그 누적 상한(마지막 발의 무기 설정).
	FValorCameraKickSpring KickSpring;
	float KickMaxDegrees = 3.0f;

	// FollowCamera에 실제로 적용한 값((1) + (2)).
	FRotator AppliedViewOffset = FRotator::ZeroRotator;
};

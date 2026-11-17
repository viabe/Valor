#pragma once

#include "CoreMinimal.h"
#include "ValorSettingsTypes.generated.h"

// 조작 설정(감도·조준 방식). 내 컴퓨터에만 있는 값이라 복제하지 않는다(서버는 결과인 시점 회전·조준 요청만 받는다).
USTRUCT()
struct FValorControlSettings
{
	GENERATED_BODY()

	// 발로란트와 같은 숫자: 1이면 마우스 1카운트에 0.07도. 발로란트에서 쓰던 감도를 그대로 넣으면 같은 거리로 돈다.
	// 기본 2.5 = 설정이 생기기 전 이 프로젝트의 시점 속도(입력 매핑 배율 0.07 × 엔진 배율 2.5 = 0.175도/카운트, 도구로 잰 값).
	UPROPERTY()
	float MouseSensitivity = 2.5f;

	// 조준(ADS·저격 줌) 중 감도 배율. 1이면 줌 배율만큼 느려진다(화면 중앙 근처 움직임이 힙파이어와 같게, 추정).
	UPROPERTY()
	float ScopedSensitivityMultiplier = 1.0f;

	UPROPERTY()
	bool bInvertMouse = false;

	// 조준 방식. true = 토글(누를 때마다 조준 ↔ 해제), false = 누르고 있는 동안만. 발로란트 기본은 둘 다 토글.
	UPROPERTY()
	bool bToggleADS = true;

	UPROPERTY()
	bool bToggleSniperZoom = true;

	// 쏘면 조준이 풀리는 저격총(오퍼레이터·마샬)을 다음 발 준비가 끝나는 대로 다시 조준한다.
	UPROPERTY()
	bool bAutoReScopeAfterShot = false;
};

// 조준선 한 겹(안쪽 선 / 바깥 선). 길이·두께·간격은 1080p 기준 픽셀이고 해상도에 비례해 커진다.
USTRUCT()
struct FValorCrosshairLineSettings
{
	GENERATED_BODY()

	UPROPERTY()
	bool bShow = true;

	UPROPERTY()
	float Opacity = 0.8f;

	UPROPERTY()
	float Length = 6.0f;

	UPROPERTY()
	float Thickness = 2.0f;

	UPROPERTY()
	float Offset = 3.0f;

	// 움직이면(이동 탄퍼짐) / 쏘면(연사 탄퍼짐) 그만큼 선이 벌어진다.
	UPROPERTY()
	bool bMovementError = false;

	UPROPERTY()
	bool bFiringError = true;
};

// 조준선 모양. 기본값은 지금까지 HUD에 박혀 있던 값과 같다(설정을 처음 여는 사람의 화면이 바뀌지 않게).
USTRUCT()
struct FValorCrosshairSettings
{
	GENERATED_BODY()

	FValorCrosshairSettings();

	// sRGB 색(색 견본·16진수 입력과 같은 숫자).
	UPROPERTY()
	FColor Color = FColor::White;

	UPROPERTY()
	bool bOutlines = true;

	UPROPERTY()
	float OutlineOpacity = 0.5f;

	UPROPERTY()
	float OutlineThickness = 1.0f;

	UPROPERTY()
	bool bCenterDot = false;

	UPROPERTY()
	float CenterDotOpacity = 1.0f;

	UPROPERTY()
	float CenterDotThickness = 2.0f;

	UPROPERTY()
	FValorCrosshairLineSettings Inner;

	UPROPERTY()
	FValorCrosshairLineSettings Outer;
};

// 설정 규칙(순수 함수, 단위 테스트 대상).
namespace ValorSettingsRules
{
	// 발로란트 감도 1의 마우스 1카운트당 회전(도). CS 계열 0.022에 감도 비율 3.18을 곱한 값으로 널리 쓰인다.
	inline constexpr float YawDegreesPerCount = 0.07f;

	// 값 범위(설정 창 막대와 저장값 검사에 같이 쓴다).
	inline constexpr float MinSensitivity = 0.001f;
	inline constexpr float MaxSensitivity = 10.0f;
	inline constexpr float MinScopedMultiplier = 0.01f;
	inline constexpr float MaxScopedMultiplier = 5.0f;

	// 마우스 카운트(이번 프레임 원시 이동량) → 회전(도). X = 좌우(오른쪽 +), Y = 위아래(위 +).
	// ZoomSensitivityScale: 조준 중 배율(조준 감도 배율 × 줌 비율), 조준이 아니면 1.
	FVector2D ComputeLookDegrees(const FVector2D& MouseCounts, float Sensitivity, float ZoomSensitivityScale, bool bInvertY);

	// 줌 비율 = tan(줌 FOV/2) / tan(힙 FOV/2). 화면 중앙 근처에서 같은 마우스 거리가 같은 화면 거리를 움직이게 하는 값이다.
	// 발로란트 줌 배율(밴달 1.25배)로 FOV를 만들었으므로 1 / 줌 배율과 같다(밴달 0.8, 오퍼레이터 1단 0.4).
	float ComputeZoomSensitivityRatio(float HipFieldOfView, float ZoomFieldOfView);

	// 막대 위치(0~1) ↔ 값. Step 단위로 맞춰 0.1 + 0.01 같은 부동소수 오차가 저장값에 남지 않게 한다.
	float SnapToStep(float Value, float Min, float Max, float Step);
	float FractionToValue(float Fraction, float Min, float Max, float Step);
	float ValueToFraction(float Value, float Min, float Max);

	// 저장 파일을 손으로 고쳤거나 깨졌을 때(NaN, 범위 밖) 쓸 수 있는 값으로 되돌린다.
	void Sanitize(FValorControlSettings& Settings);
	void Sanitize(FValorCrosshairSettings& Settings);

	// "FF00FF", "#ff00ff" → 색. 6자리만 받는다(알파는 항상 불투명, 투명도는 따로 있다).
	bool TryParseHexColor(const FString& Text, FColor& OutColor);

	// 조준선 색 견본(흰색, 초록, 연두, 노란 연두, 노랑, 하늘, 분홍, 빨강).
	const TArray<FColor>& GetCrosshairColorPresets();
}

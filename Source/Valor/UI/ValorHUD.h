#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ValorHUD.generated.h"

struct FValorShooterStance;
struct FValorSprayEvaluation;

/**
 * 발로란트식 크로스헤어 HUD(로컬 플레이어 전용).
 *
 * 왜 필요한가: 발로란트 힙파이어는 반동이 와도 크로스헤어(화면 중앙)가 움직이지 않고 탄만 크로스헤어 위로 올라간다.
 *   화면 중앙 기준점이 없으면 "탄이 조준점에서 벗어난다"는 것을 인지할 수 없어 반동이 없는 것처럼 느껴진다.
 *   그래서 기본 크로스헤어 + 발로란트의 "Firing Error / Movement Error" 표시(탄퍼짐만큼 선이 벌어짐)를 그린다.
 * 구현: UMG 에셋 없이 AHUD 캔버스로 그려 C++만으로 동작한다(에셋 의존 0). 서버에는 HUD가 없으므로 비용이 없다.
 * 네트워크: 로컬 스프레이 상태(예측값)를 읽기만 한다. 서버 판정과 무관.
 * 트레이드오프: 캔버스 드로잉은 UMG보다 꾸미기 어렵다. 크로스헤어 커스터마이즈 UI가 필요해지면 UMG로 옮기면 된다.
 */
UCLASS()
class VALOR_API AValorHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

protected:
	// 아래 길이/두께/간격은 1080p 기준 픽셀이며 해상도에 비례해 확대된다(발로란트 기본 크로스헤어 근사값).
	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair")
	FLinearColor CrosshairColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair")
	bool bShowOutline = true;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair")
	float OutlineThickness = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair")
	float OutlineOpacity = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair|Inner")
	float InnerLineLength = 6.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair|Inner")
	float InnerLineThickness = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair|Inner")
	float InnerLineOffset = 3.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair|Inner")
	float InnerLineOpacity = 0.8f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair|Inner")
	bool bInnerShowFiringError = true;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair|Inner")
	bool bInnerShowMovementError = false;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair|Outer")
	float OuterLineLength = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair|Outer")
	float OuterLineThickness = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair|Outer")
	float OuterLineOffset = 10.0f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair|Outer")
	float OuterLineOpacity = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair|Outer")
	bool bOuterShowFiringError = true;

	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair|Outer")
	bool bOuterShowMovementError = true;

	// 탄퍼짐(도)을 화면 픽셀로 바꾼 값에 곱하는 배율. 1이면 선이 실제 탄퍼짐 원뿔 가장자리에 맞는다.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair")
	float ErrorDisplayMultiplier = 1.0f;

	// ADS 중에는 발로란트처럼 무기 조준점(작은 점)만 표시한다. 1080p 기준 픽셀 크기.
	UPROPERTY(EditDefaultsOnly, Category="Valor|Crosshair")
	float ADSDotSize = 4.0f;

private:
	void DrawCrosshairLines(float CenterX, float CenterY, float Length, float Thickness, float Offset, float Opacity);
	void DrawRect2D(const FLinearColor& Color, float X, float Y, float Width, float Height, float Opacity);
	void DrawRecoilDebug(float CenterX, float CenterY, float FocalLengthPixels, const FValorSprayEvaluation& Spray, const FValorShooterStance& Stance, const FRotator& AppliedViewOffset);

	// 화면 중앙에서 각도(도)만큼 떨어진 지점까지의 픽셀 거리. 원근 투영이므로 tan(각도) × 초점거리(픽셀)이다.
	static float AngleToPixels(float AngleDegrees, float FocalLengthPixels);
};

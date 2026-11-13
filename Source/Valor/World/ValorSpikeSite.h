#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValorSpikeSite.generated.h"

class UBoxComponent;

/**
 * 스파이크 설치 지점(A·B…). 맵에 놓고 상자 크기를 설치 가능한 바닥 범위에 맞춘다(에디터에서 상자 범위·이름을 바꾼다).
 *
 * 판정: 설치하는 캐릭터의 발 위치가 상자 안이면 설치할 수 있다. 콜리전은 쓰지 않는다(겹침 이벤트 대신 위치 검사 —
 *   서버와 클라가 같은 계산으로 "지금 설치 가능"을 판단해야 이동 고정 예측이 맞는다).
 * 네트워크: 맵에 놓인 지점은 양쪽에 그대로 있다. 경기 모드가 임시로 만드는 지점(맵에 지점이 없을 때)은 복제되어 클라에도 생긴다.
 */
UCLASS()
class VALOR_API AValorSpikeSite : public AActor
{
	GENERATED_BODY()

public:
	AValorSpikeSite();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	FName GetSiteName() const { return SiteName; }
	void SetSiteName(FName NewName) { SiteName = NewName; }

	// 상자 크기(반폭, cm). 임시 지점을 만들 때 쓴다.
	void SetAreaExtent(const FVector& Extent);

	bool ContainsLocation(const FVector& Location) const;

	// 이 위치를 담은 지점(없으면 nullptr).
	static AValorSpikeSite* FindSiteContaining(const UWorld* World, const FVector& Location);

protected:
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category="Valor|Spike")
	FName SiteName = TEXT("A");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Valor|Spike")
	TObjectPtr<UBoxComponent> Area;
};

#pragma once

#include "CoreMinimal.h"
#include "Weapons/Data/ValorWeaponDataAsset.h"

// =====================================================================================================
// 발로란트 무기 20종의 기본 수치 테이블(데이터 에셋 생성용).
// - 왜: 수치를 에디터에서 20번 손으로 입력하면 오타가 나기 쉽고 출처를 잃는다. 여기에 출처와 함께 한 번만 적고,
//   커맨드렛(ValorGenerateWeaponAssets)이 이 결과로 DA_<무기> 에셋을 만든다. 만든 뒤에는 에셋이 기준이다.
// - 출처 우선순위: valorant-api(게임 파일에서 추출, 2026-09 기준) > 발로란트 위키 표/패치 이력 > 공식 패치노트 >
//   AimFinder 상대 반동값 × 밴달 영상 실측(추정) > 추정. 무기마다 어떤 값이 추정인지는 SourceNotes에 적는다.
// - 네트워크: 해당 없음. 만든 에셋은 서버·클라가 똑같이 로드하는 정적 데이터다.
// - 트레이드오프: 공개되지 않은 반동 크기(밴달 외)는 추정이라, 해당 총을 구현할 때 밴달처럼 영상 실측으로 교체해야 한다.
// =====================================================================================================

struct FValorWeaponPreset
{
	// FValorWeaponConfig::WeaponId와 에셋 이름(DA_<WeaponId>)에 쓴다.
	FName WeaponId;

	FValorWeaponConfig Config;

	// 에셋의 SourceNotes로 들어가는 출처(공식/추정)와 구현 상태 메모.
	FString SourceNotes;
};

namespace ValorWeaponPresets
{
	// 무기 20종(권총 → 기관단총 → 산탄총 → 소총 → 저격총 → 기관총, 무기군 안에서는 가격 순).
	// 밴달 프리셋은 FValorWeaponConfig 기본값(= 영상 실측으로 맞춘 밴달)과 같다.
	TArray<FValorWeaponPreset> BuildAll();
}

#pragma once

#include "Commandlets/Commandlet.h"
#include "ValorGenerateWeaponAssetsCommandlet.generated.h"

/**
 * 발로란트 무기 데이터 에셋(/Game/Valor/DataAsset/DA_<무기>)을 프리셋 수치(ValorWeaponPresets)로 한 번에 만드는 커맨드렛.
 *
 * 실행(에디터를 닫은 상태에서):
 *   UnrealEditor-Cmd.exe "<경로>/Valor.uproject" -run=ValorGenerateWeaponAssets [-overwrite] [-only=Phantom,Spectre] -unattended -nopause
 *
 * - 왜 커맨드렛인가: 20종 × 수십 개 필드를 에디터에서 손으로 넣으면 오타가 나기 쉽고 출처를 잃는다.
 *   수치는 코드에 출처 주석과 함께 한 번만 적고, 에셋은 그 결과물로 만든다(재현 가능).
 * - 기본 동작은 "이미 있는 에셋은 건너뛰기"다(에디터에서 튜닝한 값·DA_Vandal 보호).
 *   -overwrite를 주면 수치를 프리셋 값으로 되돌리되, 에디터에서 연결한 연출 참조(몽타주·이펙트·그립 소켓)는 유지한다.
 * - 네트워크: 해당 없음(에디터 전용 도구). 만든 에셋은 서버·클라가 똑같이 로드하는 정적 데이터다.
 * - 트레이드오프: 프리셋과 에셋이 따로 존재하므로, 에셋을 튜닝한 뒤 -overwrite로 다시 돌리면 튜닝 값이 사라진다.
 */
UCLASS()
class UValorGenerateWeaponAssetsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UValorGenerateWeaponAssetsCommandlet();

	virtual int32 Main(const FString& Params) override;
};

#pragma once

#include "Commandlets/Commandlet.h"
#include "ValorRepairCharacterBlueprintCommandlet.generated.h"

/**
 * 캐릭터 BP(기본 /Game/Valor/Character/BP_ValorCharacterBase)를 다시 컴파일해 저장하는 커맨드렛.
 *
 * 왜 필요한가: AValorCharacter가 이동 컴포넌트를 UValorCharacterMovementComponent로 바꾸기(SetDefaultSubobjectClass) 전에 저장된 BP는
 *   CDO의 이동 컴포넌트(CharMoveComp)를 옛 클래스(UCharacterMovementComponent)로 기록하고 있다. 로더는 저장된 클래스를 따르므로
 *   -game·패키징 빌드에서는 걷기·무기별 이동 속도·구매 단계 이동 잠금이 동작하지 않는다(에디터는 BP를 열 때 다시 컴파일해 가려진다).
 *   BP를 컴파일하면 네이티브 생성자로 CDO를 새로 만들어 Valor 이동 컴포넌트가 되고, 저장하면 에셋에 반영된다.
 *   엔진 ResavePackages는 BP를 컴파일하지 않고 다시 쓰기만 해서 고쳐지지 않는다.
 *
 * 실행(에디터를 닫은 상태에서):
 *   UnrealEditor-Cmd.exe "<경로>/Valor.uproject" -run=ValorRepairCharacterBlueprint [-bp=/Game/...] -unattended -nopause
 */
UCLASS()
class UValorRepairCharacterBlueprintCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UValorRepairCharacterBlueprintCommandlet();

	virtual int32 Main(const FString& Params) override;
};

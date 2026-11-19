#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Settings/ValorSettingsTypes.h"
#include "ValorUserSettings.generated.h"

/**
 * 플레이어 설정(감도·조준 방식·조준선). 내 컴퓨터의 GameUserSettings.ini에 [/Script/Valor.ValorUserSettings]로 저장한다.
 *
 * 왜 클래스 기본 객체(CDO) 하나를 쓰나: config 클래스의 CDO는 엔진이 시작할 때 ini에서 값을 읽어 오므로,
 *   따로 만들고 불러올 필요 없이 어디서나 Get()으로 같은 값을 본다(에디터 PIE 창 여러 개도 같은 값을 쓴다).
 * 왜 UGameUserSettings를 상속하지 않나: 상속하면 저장할 때 해상도·창 모드까지 같이 써서, 도구 실행(-ResX=1280)이
 *   사용자의 화면 설정을 바꿔 버린다. 이 클래스는 자기 구역만 쓴다.
 * 네트워크: 복제하지 않는다. 서버는 결과(시점 회전, 조준 요청)만 받는다.
 */
UCLASS(config=GameUserSettings, configdonotcheckdefaults)
class VALOR_API UValorUserSettings : public UObject
{
	GENERATED_BODY()

public:
	static UValorUserSettings& Get();

	virtual void PostInitProperties() override;

	const FValorControlSettings& GetControls() const { return Controls; }
	const FValorCrosshairSettings& GetCrosshair() const { return Crosshair; }

	// 고친 뒤 Save()를 부른다(설정 창·콘솔 명령). 범위 밖 값은 Save가 잘라 낸다.
	FValorControlSettings& EditControls() { return Controls; }
	FValorCrosshairSettings& EditCrosshair() { return Crosshair; }

	// 범위 검사 → ini 파일에 바로 쓴다.
	void Save();

	void ResetControls();
	void ResetCrosshair();

	// 메인 메뉴: 이름(점수판·킬피드에 보이는 이름)과 마지막으로 참가한 주소. 이름이 비어 있으면 Player#### 를 만들어 저장한다.
	FString GetPlayerName();
	void SetPlayerName(const FString& NewName);
	const FString& GetLastJoinAddress() const { return LastJoinAddress; }
	void SetLastJoinAddress(const FString& Address) { LastJoinAddress = Address; }

	// 저장 파일 경로(로그·문서·도구 확인용).
	static FString GetSaveFilePath();

private:
	UPROPERTY(config)
	FValorControlSettings Controls;

	UPROPERTY(config)
	FValorCrosshairSettings Crosshair;

	UPROPERTY(config)
	FString PlayerName;

	UPROPERTY(config)
	FString LastJoinAddress = TEXT("127.0.0.1");
};

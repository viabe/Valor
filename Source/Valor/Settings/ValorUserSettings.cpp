#include "Settings/ValorUserSettings.h"

#include "Misc/ConfigCacheIni.h"
#include "Session/ValorSessionTypes.h"

UValorUserSettings& UValorUserSettings::Get()
{
	return *GetMutableDefault<UValorUserSettings>();
}

void UValorUserSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// CDO는 여기 오기 전에 ini 값을 읽었다. 손으로 고쳤거나 깨진 값은 쓸 수 있는 범위로 돌린다(저장은 하지 않는다).
	ValorSettingsRules::Sanitize(Controls);
	ValorSettingsRules::Sanitize(Crosshair);
}

void UValorUserSettings::Save()
{
	ValorSettingsRules::Sanitize(Controls);
	ValorSettingsRules::Sanitize(Crosshair);

	// SaveConfig는 GConfig에 쓰고 파일로 내보낸다(Flush). 다른 구역(해상도 등)은 건드리지 않는다.
	SaveConfig(CPF_Config, *GGameUserSettingsIni);
}

void UValorUserSettings::ResetControls()
{
	Controls = FValorControlSettings();
}

void UValorUserSettings::ResetCrosshair()
{
	Crosshair = FValorCrosshairSettings();
}

FString UValorUserSettings::GetPlayerName()
{
	FString Sanitized = ValorSession::SanitizePlayerName(PlayerName);
	if (Sanitized.IsEmpty())
	{
		// 처음 켰을 때: 컴퓨터 계정 이름 대신(다른 사람에게 보이므로) 임의 번호 이름을 만든다.
		Sanitized = FString::Printf(TEXT("Player%04d"), FMath::RandRange(0, 9999));
		PlayerName = Sanitized;
		Save();
	}
	return Sanitized;
}

void UValorUserSettings::SetPlayerName(const FString& NewName)
{
	const FString Sanitized = ValorSession::SanitizePlayerName(NewName);
	if (!Sanitized.IsEmpty())
	{
		PlayerName = Sanitized;
	}
}

FString UValorUserSettings::GetSaveFilePath()
{
	return GGameUserSettingsIni;
}

#include "Weapons/ValorWeaponTypes.h"

void FValorShotRequest::Quantize()
{
	// NetSerialize와 똑같이 16비트로 압축했다가 복원한다. 압축→복원은 멱등이므로 몇 번 호출해도 결과가 같다.
	// 이렇게 해야 리슨 서버(직렬화를 거치지 않음)와 원격 클라(직렬화를 거침)가 같은 조준 값을 쓴다.
	AimRotation = FRotator(
		FRotator::DecompressAxisFromShort(FRotator::CompressAxisToShort(AimRotation.Pitch)),
		FRotator::DecompressAxisFromShort(FRotator::CompressAxisToShort(AimRotation.Yaw)),
		0.0f).GetNormalized();
}

bool FValorShotRequest::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << ClientShotTime;

	// 각도는 16비트(약 0.0055도 해상도)면 히트스캔 정밀도에 충분하다. 롤은 조준에 쓰지 않으므로 보내지 않는다.
	uint16 PitchShort = 0;
	uint16 YawShort = 0;
	if (Ar.IsSaving())
	{
		PitchShort = FRotator::CompressAxisToShort(AimRotation.Pitch);
		YawShort = FRotator::CompressAxisToShort(AimRotation.Yaw);
	}

	Ar << PitchShort;
	Ar << YawShort;

	if (Ar.IsLoading())
	{
		AimRotation = FRotator(FRotator::DecompressAxisFromShort(PitchShort), FRotator::DecompressAxisFromShort(YawShort), 0.0f).GetNormalized();
	}

	bOutSuccess = true;
	return true;
}

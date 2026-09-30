#include "Weapons/Data/ValorWeaponDataAsset.h"

namespace ValorWeaponDefaults
{
	struct FCurvePoint
	{
		float X;
		float Y;
	};

	// 커브 키를 선형 보간으로 채운다.
	// 왜 선형인가: 발로란트 커브는 "발 사이의 중간 값"을 갖는 곡선(0.50 패치노트)이고, 여기서는 키를 발 단위로
	// 촘촘히 찍으므로 선형이면 충분하다. 큐빅 자동 탄젠트는 키 사이에서 오버슈트해 반동이 일순간 역행할 수 있다.
	template <int32 NumPoints>
	void FillLinearCurve(FRuntimeFloatCurve& Curve, const FCurvePoint (&Points)[NumPoints])
	{
		FRichCurve* RichCurve = Curve.GetRichCurve();
		if (!RichCurve)
		{
			return;
		}

		RichCurve->Reset();
		for (const FCurvePoint& Point : Points)
		{
			const FKeyHandle KeyHandle = RichCurve->AddKey(Point.X, Point.Y);
			RichCurve->SetKeyInterpMode(KeyHandle, RCIM_Linear);
		}
	}
}

FValorRecoilProfile::FValorRecoilProfile()
{
	using namespace ValorWeaponDefaults;

	// 밴달 수직 반동(누적, 도) — 실제 게임 영상 2개(무보정 힙파이어 연사)를 프레임 단위로 측정해 맞춘 값.
	// - 측정: 카메라 상승(= 반동 × 0.5)이 3발 후 1.2°, 4발 1.8°, 5발 2.7°, 6발 3.3°, 7발 3.9°, 8발 4.2°에서 멈췄고,
	//         벽 탄흔은 2.6°, 3.5°, 5.3°를 지나 최상단 약 7.5~9.7°(중심 약 8.5°)의 띠를 만들었다.
	// - 첫 두세 발은 완만하고(탭/버스트가 정확한 이유), 4~8발째 가장 가파르게 오른 뒤 9발째부터 거의 멈춘다
	//   → "긴 수직 줄기 + 상단 좌우 흔들림(T / 거꾸로 7자)".
	static const FCurvePoint VandalVertical[] =
	{
		{0.0f, 0.00f}, {1.0f, 0.60f}, {2.0f, 1.45f}, {3.0f, 2.55f}, {4.0f, 3.80f}, {5.0f, 5.10f}, {6.0f, 6.30f},
		{7.0f, 7.20f}, {8.0f, 7.85f}, {9.0f, 8.25f}, {10.0f, 8.50f}, {11.0f, 8.62f}, {12.0f, 8.70f}, {14.0f, 8.78f},
		{24.0f, 8.95f}
	};
	FillLinearCurve(VerticalRecoilCurve, VandalVertical);

	// 밴달 수평 반동 진폭(도) — 영상 측정: 카메라 좌우가 6발째부터 벌어져 ±1.2~1.5°(탄 기준 약 ±2.5°)에서 오가고,
	// 벽 탄흔 상단 띠는 좌우 약 4~5.5° 폭이었다.
	// - 보호 탄(0~5): 0 → "보호 탄 동안은 곧게 끌어내리기만 하면 된다"(완전 결정적).
	// - 6발째부터 진폭이 서서히 커져 12발째 ±2.4°, 16발 이후 ±2.7~2.8°.
	static const FCurvePoint VandalHorizontalAmplitude[] =
	{
		{0.0f, 0.00f}, {5.0f, 0.00f}, {6.0f, 0.40f}, {8.0f, 1.20f}, {10.0f, 1.90f}, {12.0f, 2.40f}, {16.0f, 2.70f},
		{24.0f, 2.80f}
	};
	FillLinearCurve(HorizontalRecoilAmplitudeCurve, VandalHorizontalAmplitude);

	// 밴달 탄퍼짐 증가(0~1). 첫 발 0.25도에서 약 8발째 최대 1.0도에 도달한다(도달 발 수는 공개 수치가 없어 추정).
	static const FCurvePoint VandalFiringError[] =
	{
		{0.0f, 0.00f}, {1.0f, 0.10f}, {2.0f, 0.25f}, {3.0f, 0.40f}, {4.0f, 0.55f}, {5.0f, 0.70f}, {6.0f, 0.82f},
		{7.0f, 0.92f}, {8.0f, 1.00f}
	};
	FillLinearCurve(FiringErrorCurve, VandalFiringError);
}

FValorWeaponConfig::FValorWeaponConfig()
{
	DisplayName = NSLOCTEXT("ValorWeapon", "VandalDisplayName", "Vandal");

	// ADS(Alternate Fire): 발사 속도 90%(8.775), 첫 발 0.1575도, 최대 1.02도, 약간의 반동 감소, 크로스헤어가 반동을 따라감.
	AltFire.FireRate = 8.775f;
	AltFire.FirstShotError = 0.1575f;
	AltFire.MaxFiringError = 1.02f;
	AltFire.RecoilMultiplier = 0.9f;
	AltFire.CameraRecoilFollowRatio = 1.0f;

	// ADS는 조준경이 이미 패턴을 끝까지 따라 올라가므로, 매 발 킥은 힙보다 작게 두어 조준점이 과하게 흔들리지 않게 한다.
	AltFire.CameraKick.PitchDegrees = 0.35f;
	AltFire.CameraKick.YawDegrees = 0.08f;

	// 밴달은 거리 감쇠가 없다: 0~50m 표기지만 그 이상에서도 같은 피해량이므로 사거리 전체를 한 구간으로 둔다.
	FValorDamageRangeStep AllRanges;
	AllRanges.MaxDistanceCm = TraceDistanceCm;
	AllRanges.HeadDamage = 160.0f;
	AllRanges.BodyDamage = 40.0f;
	AllRanges.LegDamage = 34.0f;
	DamageRanges.Add(AllRanges);
}

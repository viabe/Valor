#pragma once

#include "CoreMinimal.h"

/**
 * 카메라 킥(사격 시 화면이 튀었다 돌아오는 연출)용 임계 감쇠 스프링.
 *
 * 왜 스프링인가: "발마다 일정한 크기로 튀고 바로 복귀"하는 발로란트식 화면 반동은 매 발 속도 충격을 주고
 *   임계 감쇠(오버슈트 없이 가장 빨리 0으로 복귀)로 되돌리면 자연스럽게 나온다. 연사 중에는 이전 킥이 다 빠지기 전에
 *   다음 킥이 들어와 화면이 살짝 들린 채 떨리고, 사격을 멈추면 스스로 가라앉는다.
 * 적분: x(t) = (A + B·t)·e^(-ωt) 해석해로 한 번에 전진시키므로 프레임레이트가 달라도 같은 궤적을 그린다.
 * 네트워크: 없음(로컬 카메라 연출 전용). 난수도 연출용이라 결정성이 필요 없다.
 */
struct FValorCameraKickSpring
{
	// 현재 킥 오프셋(도). X = Pitch(위 +), Y = Yaw(오른쪽 +), Z = Roll.
	FVector Offset = FVector::ZeroVector;

	// 현재 킥 각속도(도/초).
	FVector Velocity = FVector::ZeroVector;

	// 스프링 고유 각진동수 ω(1/초). 정지 상태에서 충격을 주면 t = 1/ω에 최고점에 도달한다.
	float AngularFrequency = 25.0f;

	// 정지 상태 기준으로 PeakTimeSeconds 뒤 최고점이 PeakDegrees가 되도록 속도 충격을 준다.
	// 정지 상태 해: x(t) = v0·t·e^(-ωt) → 최고점 = v0 / (ω·e) (t = 1/ω) → v0 = Peak·ω·e.
	void AddKick(const FVector& PeakDegrees, float PeakTimeSeconds)
	{
		AngularFrequency = 1.0f / FMath::Max(PeakTimeSeconds, 0.005f);
		Velocity += PeakDegrees * (AngularFrequency * static_cast<float>(UE_EULERS_NUMBER));
	}

	// 해석해로 DeltaTime만큼 전진한다. MaxMagnitude로 축별 누적을 제한한다.
	void Advance(float DeltaTime, float MaxMagnitude)
	{
		if (DeltaTime <= 0.0f)
		{
			return;
		}

		const float Omega = AngularFrequency;
		const float Decay = FMath::Exp(-Omega * DeltaTime);
		const FVector B = Velocity + (Offset * Omega);
		Offset = (Offset + (B * DeltaTime)) * Decay;
		Velocity = (Velocity - (B * (Omega * DeltaTime))) * Decay;

		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (FMath::Abs(Offset[Axis]) > MaxMagnitude)
			{
				// 상한에 닿으면 그 축의 바깥쪽 속도를 없애 더 밀려나지 않게 한다.
				Offset[Axis] = FMath::Sign(Offset[Axis]) * MaxMagnitude;
				if (Velocity[Axis] * Offset[Axis] > 0.0f)
				{
					Velocity[Axis] = 0.0f;
				}
			}
		}

		if (Offset.IsNearlyZero(0.0005f) && Velocity.IsNearlyZero(0.005f))
		{
			Offset = FVector::ZeroVector;
			Velocity = FVector::ZeroVector;
		}
	}

	FRotator ToRotator() const
	{
		return FRotator(Offset.X, Offset.Y, Offset.Z);
	}
};

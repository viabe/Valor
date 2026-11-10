#include "Components/SkeletalMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/ValorTestWorld.h"
#include "ValorCharacter.h"
#include "Weapons/ValorViewmodelTypes.h"
#include "Weapons/ValorWeaponBase.h"

#if WITH_DEV_AUTOMATION_TESTS

// 1인칭 뷰모델 배치 계산과 총 부착 기준(그립·3인칭 회전)을 검증한다.
// 화면 구도 자체는 숫자로 검증하기 어려우므로 개발 도구 Valor.Tools.ViewmodelCapture(-game 실행)로 눈으로 확인한다.
// 실행: 콘솔 "Automation RunTests Valor.Viewmodel".

namespace
{
	bool IsNearlySameDirection(const FVector& A, const FVector& B)
	{
		return A.Equals(B, 1.0e-4f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorViewmodelAxisMappingTest, "Valor.Viewmodel.AxisMapping", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorViewmodelAxisMappingTest::RunTest(const FString& Parameters)
{
	// 1인칭: 총 리그(Z 총구, Y 아래, X 왼쪽) → 뷰(X 앞, Y 오른쪽, Z 위).
	const FQuat GripToView = ValorViewmodel::GripToViewRotation();
	TestTrue(TEXT("리그 Z(총구) → 화면 앞"), IsNearlySameDirection(GripToView.RotateVector(FVector::UpVector), FVector::ForwardVector));
	TestTrue(TEXT("리그 Y(총 아래) → 화면 아래"), IsNearlySameDirection(GripToView.RotateVector(FVector::RightVector), -FVector::UpVector));
	TestTrue(TEXT("리그 X(총 왼쪽) → 화면 왼쪽"), IsNearlySameDirection(GripToView.RotateVector(FVector::ForwardVector), -FVector::RightVector));

	// 3인칭: 마네킹 소총 대기 자세에서 잰 손 소켓 축(캐릭터 기준)에 기본 회전을 얹으면 총구가 정면, 총의 아래가 바닥을 향해야 한다.
	// 측정값은 AValorCharacter 생성자의 값과 같다(ViewmodelCapture 도구로 잼).
	const FQuat MeasuredHandSocket = FRotationMatrix::MakeFromXZ(FVector(-0.969f, 0.207f, 0.133f), FVector(0.103f, -0.151f, 0.983f)).ToQuat();
	const FQuat WeaponInActor = MeasuredHandSocket * GetDefault<AValorCharacter>()->GetThirdPersonWeaponRotation();
	TestTrue(TEXT("3인칭: 총구(리그 Z) → 캐릭터 정면"), WeaponInActor.RotateVector(FVector::UpVector).Equals(FVector::ForwardVector, 1.0e-3f));
	TestTrue(TEXT("3인칭: 총의 아래(리그 Y) → 바닥"), WeaponInActor.RotateVector(FVector::RightVector).Equals(-FVector::UpVector, 1.0e-3f));

	// 3인칭 조준 정렬: 총구가 정확히 목표 방향, 총의 위는 시선의 위 쪽(총구 기준 수직면 안 = 기울임 0).
	const FVector AimMuzzle = FRotator(-12.0f, 37.0f, 0.0f).Vector();
	const FVector AimUp = FRotationMatrix(FRotator(-10.0f, 35.0f, 0.0f)).GetUnitAxis(EAxis::Z);
	const FQuat Level = ValorViewmodel::MakeLevelRigRotation(AimMuzzle, AimUp);
	TestTrue(TEXT("3인칭 정렬: 리그 Z(총구) = 목표 방향"), Level.RotateVector(FVector::UpVector).Equals(AimMuzzle, 1.0e-4f));
	const FVector GunUp = Level.RotateVector(-FVector::RightVector);
	TestTrue(TEXT("3인칭 정렬: 총의 위는 위쪽"), GunUp.Z > 0.9f);
	TestEqual(TEXT("3인칭 정렬: 기울임 없음(총의 위가 총구·수직축 평면 안)"), static_cast<float>(GunUp | (AimMuzzle ^ FVector::UpVector).GetSafeNormal()), 0.0f, 0.02f);

	// 1인칭 팔: 템플릿 GripPoint 축(X 총의 왼쪽, Y 총열, Z 위)을 총 리그 공간으로 옮기면 각각 리그 X, 리그 Z, 리그 -Y여야 한다.
	const FQuat ArmsGripPoint = ValorViewmodel::RigToArmsGripPointRotation();
	TestTrue(TEXT("팔: GripPoint Y(총열) → 리그 Z(총구)"), IsNearlySameDirection(ArmsGripPoint.RotateVector(FVector::RightVector), FVector::UpVector));
	TestTrue(TEXT("팔: GripPoint Z(위) → 리그 -Y(총의 위)"), IsNearlySameDirection(ArmsGripPoint.RotateVector(FVector::UpVector), -FVector::RightVector));
	TestTrue(TEXT("팔: GripPoint X(총의 왼쪽) → 리그 X"), IsNearlySameDirection(ArmsGripPoint.RotateVector(FVector::ForwardVector), FVector::ForwardVector));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorViewmodelPoseBlendTest, "Valor.Viewmodel.PoseBlend", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorViewmodelPoseBlendTest::RunTest(const FString& Parameters)
{
	FValorViewmodelConfig Config;
	Config.HipLocation = FVector(45.0f, 27.0f, -22.0f);
	Config.HipRotation = FRotator(-3.5f, 2.0f, 0.0f);
	Config.ADSForwardCm = 30.0f;
	Config.ADSRightCm = 0.5f;
	constexpr float SightHeightCm = 15.0f;

	const FValorViewmodelPose Hip = ValorViewmodel::BlendHipToADS(Config, SightHeightCm, 0.0f);
	TestTrue(TEXT("힙 = 설정 위치"), Hip.Location.Equals(Config.HipLocation));
	TestTrue(TEXT("힙 = 설정 회전"), Hip.ViewRotation.Equals(Config.HipRotation));

	// 조준: 조준선(손잡이 위 SightHeight)이 화면 중앙(Y 0, Z 0)에 오고 총구는 시선과 평행.
	const FValorViewmodelPose ADS = ValorViewmodel::BlendHipToADS(Config, SightHeightCm, 1.0f);
	TestTrue(TEXT("조준 위치 = (앞 거리, 좌우 보정, -조준선 높이)"), ADS.Location.Equals(FVector(30.0f, 0.5f, -15.0f)));
	TestTrue(TEXT("조준 회전 없음"), ADS.ViewRotation.IsNearlyZero());

	const FValorViewmodelPose Half = ValorViewmodel::BlendHipToADS(Config, SightHeightCm, 0.5f);
	TestTrue(TEXT("중간 = 두 자세의 중간"), Half.Location.Equals((Hip.Location + ADS.Location) * 0.5f));
	TestTrue(TEXT("범위 밖 알파는 잘린다"), ValorViewmodel::BlendHipToADS(Config, SightHeightCm, 2.0f).Location.Equals(ADS.Location));

	// 장착 올림: 0 → 0, 1 → 1, 처음에 빠르게 올라온다(진행 50%에 87.5%).
	TestEqual(TEXT("장착 시작 = 완전히 내림"), ValorViewmodel::EquipRaiseAlpha(0.0f), 0.0f);
	TestEqual(TEXT("장착 완료 = 제자리"), ValorViewmodel::EquipRaiseAlpha(1.0f), 1.0f);
	TestEqual(TEXT("장착 절반 = 87.5%"), ValorViewmodel::EquipRaiseAlpha(0.5f), 0.875f, 1.0e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorViewmodelSightHeightTest, "Valor.Viewmodel.SightHeight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorViewmodelSightHeightTest::RunTest(const FString& Parameters)
{
	// 경계 상자 윗면 높이: 손잡이 축 기준 "위"는 리그 -Y. 손잡이 뼈의 스케일(가져온 리그의 100)은 무시해야 cm가 나온다.
	// 손잡이가 원점, 리그 축 = 컴포넌트 축이라면 상자의 가장 작은 Y가 윗면이다.
	const FBox MeshBox(FVector(-3.0f, -16.5f, -40.0f), FVector(3.0f, 14.0f, 88.0f));
	const FTransform GripAtOriginScaled(FQuat::Identity, FVector::ZeroVector, FVector(100.0f));
	TestEqual(TEXT("윗면 높이 16.5cm(스케일 무시)"), ValorViewmodel::ComputeTopAboveGripCm(MeshBox, GripAtOriginScaled), 16.5f, 1.0e-3f);

	// 손잡이가 위로 10cm(리그 -Y) 올라가 있으면 윗면까지는 6.5cm.
	const FTransform GripRaised(FQuat::Identity, FVector(0.0f, -10.0f, 0.0f));
	TestEqual(TEXT("손잡이 기준으로 잰다"), ValorViewmodel::ComputeTopAboveGripCm(MeshBox, GripRaised), 6.5f, 1.0e-3f);

	FValorViewmodelConfig Config;
	TestEqual(TEXT("자동: 윗면 - 1cm"), ValorViewmodel::ResolveSightHeightCm(Config, 16.5f), 16.5f - ValorViewmodel::AutoSightBelowTopCm, 1.0e-5f);
	TestEqual(TEXT("자동: 음수가 되지 않음"), ValorViewmodel::ResolveSightHeightCm(Config, 0.2f), 0.0f);
	Config.SightHeightCm = 9.0f;
	TestEqual(TEXT("설정값이 있으면 그 값"), ValorViewmodel::ResolveSightHeightCm(Config, 16.5f), 9.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorViewmodelGunGripReferenceTest, "Valor.Viewmodel.AllGunsHaveGripAndMuzzle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorViewmodelGunGripReferenceTest::RunTest(const FString& Parameters)
{
	// 모든 총 BP가 손잡이 기준점(GripSocket 또는 리그 Root 뼈)과 총구 소켓을 가져야 1인칭·3인칭 모두 같은 규칙으로 잡힌다.
	FValorScopedTestWorld TestWorld(TEXT("ValorViewmodelGripTestWorld"));
	if (!TestNotNull(TEXT("테스트 월드 생성"), TestWorld.World))
	{
		return false;
	}

	int32 CheckedWeaponCount = 0;
	for (const TPair<FString, UClass*>& GunClass : LoadValorGunBlueprintClasses())
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AValorWeaponBase* Weapon = TestWorld.World->SpawnActor<AValorWeaponBase>(GunClass.Value, FTransform::Identity, SpawnParameters);
		USkeletalMeshComponent* WeaponMesh = Weapon ? Weapon->GetWeaponMesh() : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s 메시"), *GunClass.Key), WeaponMesh))
		{
			continue;
		}

		const FName GripReference = ValorViewmodel::ResolveGripReferenceName(*WeaponMesh, Weapon->GetWeaponConfig().GripSocketName);
		TestFalse(*FString::Printf(TEXT("%s: 손잡이 기준점 있음"), *GunClass.Key), GripReference.IsNone());
		TestTrue(*FString::Printf(TEXT("%s: 총구 소켓 있음"), *GunClass.Key), WeaponMesh->DoesSocketExist(Weapon->GetWeaponConfig().Effects.MuzzleSocketName));

		// 자동 조준선은 총 크기 범위(3~30cm) 안이어야 한다. 벗어나면 메시 경계 상자나 축이 예상과 다르다는 뜻이다.
		const float SightHeightCm = Weapon->GetViewmodelSightHeightCm();
		TestTrue(*FString::Printf(TEXT("%s: 조준선 높이 %.1fcm가 3~30cm 안"), *GunClass.Key, SightHeightCm), SightHeightCm >= 3.0f && SightHeightCm <= 30.0f);

		Weapon->Destroy();
		++CheckedWeaponCount;
	}

	if (CheckedWeaponCount == 0)
	{
		AddWarning(TEXT("/Game/Valor/Gun에서 총 BP를 찾지 못해 검사하지 않았습니다."));
	}

	return true;
}

#endif

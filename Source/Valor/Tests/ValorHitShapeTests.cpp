#include "Components/SkeletalMeshComponent.h"
#include "Components/ValorLagCompensationComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/ValorTestWorld.h"
#include "ValorCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

// 명중 판정 도형(뼈에 붙은 물리 에셋 바디)을 검증한다.
// 실제 애니메이션 자세에서 메시와 겹치는지는 개발 도구 Valor.Tools.HitboxCapture(-game 실행)로 눈으로 확인한다.
// 실행: 콘솔 "Automation RunTests Valor.Combat.HitShapes".

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorHitShapeIntersectionTest, "Valor.Combat.HitShapes.SegmentIntersection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorHitShapeIntersectionTest::RunTest(const FString& Parameters)
{
	// Z축 캡슐: 원통 반길이 10, 반지름 5 → 위아래 끝은 z = ±15.
	FValorLagCompHitShape Capsule;
	Capsule.ShapeType = EValorHitShapeType::Capsule;
	Capsule.HalfLength = 10.0f;
	Capsule.Radius = 5.0f;

	const FTransform Identity = FTransform::Identity;
	float T = -1.0f;

	// 옆면: x = -100에서 +X로 200cm → x = -5에서 진입(t = 95/200).
	TestTrue(TEXT("캡슐 옆면 명중"), ValorLagComp::IntersectSegmentShape(Capsule, Identity, FVector(-100, 0, 0), FVector(100, 0, 0), T));
	TestEqual(TEXT("캡슐 옆면 진입 t"), T, 95.0f / 200.0f, 1.0e-4f);

	// 위쪽 구(z = 14): 중심(0,0,10)에서 높이 4 → 반폭 3 → x = -3에서 진입.
	TestTrue(TEXT("캡슐 위쪽 구 명중"), ValorLagComp::IntersectSegmentShape(Capsule, Identity, FVector(-100, 0, 14), FVector(100, 0, 14), T));
	TestEqual(TEXT("캡슐 위쪽 구 진입 t"), T, 97.0f / 200.0f, 1.0e-4f);

	// 캡슐 끝(z = 15)보다 위, 반지름보다 옆, 반대 방향, 선분이 모자란 경우는 빗나간다.
	TestFalse(TEXT("캡슐 위로 빗나감"), ValorLagComp::IntersectSegmentShape(Capsule, Identity, FVector(-100, 0, 16), FVector(100, 0, 16), T));
	TestFalse(TEXT("캡슐 옆으로 빗나감"), ValorLagComp::IntersectSegmentShape(Capsule, Identity, FVector(-100, 6, 0), FVector(100, 6, 0), T));
	TestFalse(TEXT("캡슐 반대 방향"), ValorLagComp::IntersectSegmentShape(Capsule, Identity, FVector(-100, 0, 0), FVector(-200, 0, 0), T));
	TestFalse(TEXT("선분이 캡슐 앞에서 끝남"), ValorLagComp::IntersectSegmentShape(Capsule, Identity, FVector(-100, 0, 0), FVector(-10, 0, 0), T));

	// 축과 나란한 탄(위에서 아래로): 위쪽 구 꼭대기 z = 15에서 진입.
	TestTrue(TEXT("캡슐 축 방향 명중"), ValorLagComp::IntersectSegmentShape(Capsule, Identity, FVector(0, 0, 100), FVector(0, 0, -100), T));
	TestEqual(TEXT("캡슐 축 방향 진입 t"), T, 85.0f / 200.0f, 1.0e-4f);

	// 시작점이 안이면 t = 0(초근접 사격).
	TestTrue(TEXT("캡슐 안에서 시작"), ValorLagComp::IntersectSegmentShape(Capsule, Identity, FVector(0, 0, 3), FVector(100, 0, 3), T));
	TestEqual(TEXT("캡슐 안에서 시작 t"), T, 0.0f, 1.0e-6f);

	// 변환: 캡슐을 X축으로 눕히고(피치 90도) (0, 50, 0)으로 옮긴다. 이제 x 방향 ±15, y = 50 둘레 반지름 5.
	const FTransform Lying(FRotator(90.0f, 0.0f, 0.0f), FVector(0, 50, 0));
	TestTrue(TEXT("누운 캡슐: 긴 쪽 끝을 지나는 탄"), ValorLagComp::IntersectSegmentShape(Capsule, Lying, FVector(12, -100, 0), FVector(12, 100, 0), T));
	TestFalse(TEXT("누운 캡슐: 끝 밖을 지나는 탄"), ValorLagComp::IntersectSegmentShape(Capsule, Lying, FVector(21, -100, 0), FVector(21, 100, 0), T));
	TestTrue(TEXT("누운 캡슐: 위에서 아래로"), ValorLagComp::IntersectSegmentShape(Capsule, Lying, FVector(0, 50, 100), FVector(0, 50, -100), T));
	TestEqual(TEXT("누운 캡슐: 위쪽 면 z = 5에서 진입"), T, 95.0f / 200.0f, 1.0e-4f);

	// 구.
	FValorLagCompHitShape Sphere;
	Sphere.ShapeType = EValorHitShapeType::Sphere;
	Sphere.Radius = 10.0f;
	TestTrue(TEXT("구 명중"), ValorLagComp::IntersectSegmentShape(Sphere, Identity, FVector(-100, 0, 0), FVector(100, 0, 0), T));
	TestEqual(TEXT("구 진입 t"), T, 90.0f / 200.0f, 1.0e-4f);
	TestFalse(TEXT("구 빗나감"), ValorLagComp::IntersectSegmentShape(Sphere, Identity, FVector(-100, 11, 0), FVector(100, 11, 0), T));

	// 회전한 상자: Z로 45도 돌린 반폭 10 상자. 대각선 방향 반폭은 10 * sqrt(2).
	FValorLagCompHitShape Box;
	Box.ShapeType = EValorHitShapeType::Box;
	Box.HalfExtent = FVector(10.0f);
	const FTransform Rotated(FRotator(0.0f, 45.0f, 0.0f));
	TestTrue(TEXT("돌린 상자: 모서리 쪽 명중"), ValorLagComp::IntersectSegmentShape(Box, Rotated, FVector(-100, 0, 0), FVector(100, 0, 0), T));
	TestEqual(TEXT("돌린 상자: 모서리 x = -14.14에서 진입"), T, (100.0f - (10.0f * UE_SQRT_2)) / 200.0f, 1.0e-4f);
	TestFalse(TEXT("돌린 상자: 모서리 밖 빗나감"), ValorLagComp::IntersectSegmentShape(Box, Rotated, FVector(-100, 14.5f, 0), FVector(100, 14.5f, 0), T));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValorHitShapeBonePoseTest, "Valor.Combat.HitShapes.BonePose", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FValorHitShapeBonePoseTest::RunTest(const FString& Parameters)
{
	UClass* CharacterClass = LoadObject<UClass>(nullptr, TEXT("/Game/Valor/Character/BP_ValorCharacterBase.BP_ValorCharacterBase_C"));
	if (!TestNotNull(TEXT("BP_ValorCharacterBase 클래스"), CharacterClass))
	{
		return false;
	}

	FValorScopedTestWorld TestWorld(TEXT("ValorHitShapeTestWorld"));
	AValorCharacter* Character = TestWorld.World ? TestWorld.World->SpawnActor<AValorCharacter>(CharacterClass, FVector(0.0f, 0.0f, 200.0f), FRotator::ZeroRotator) : nullptr;
	if (!TestNotNull(TEXT("캐릭터 스폰"), Character))
	{
		return false;
	}

	// 테스트 월드는 BeginPlay를 돌리지 않으므로 도형을 직접 만든다.
	UValorLagCompensationComponent* LagComp = Character->GetLagCompensationComponent();
	LagComp->RebuildHitShapes();
	const TArray<FValorLagCompHitShape>& Shapes = LagComp->GetHitShapes();
	TestTrue(TEXT("물리 에셋에서 도형 여러 개"), Shapes.Num() > 10);

	auto FindZone = [&Shapes](const TCHAR* BoneName)
	{
		const FValorLagCompHitShape* Found = Shapes.FindByPredicate([BoneName](const FValorLagCompHitShape& Shape) { return Shape.BoneName == FName(BoneName); });
		return Found ? Found->HitZone : EValorHitZone::None;
	};
	TestEqual(TEXT("head → 머리"), FindZone(TEXT("head")), EValorHitZone::Head);
	TestEqual(TEXT("neck_01 → 몸"), FindZone(TEXT("neck_01")), EValorHitZone::Body);
	TestEqual(TEXT("spine_03 → 몸"), FindZone(TEXT("spine_03")), EValorHitZone::Body);
	TestEqual(TEXT("upperarm_l → 몸"), FindZone(TEXT("upperarm_l")), EValorHitZone::Body);
	TestEqual(TEXT("thigh_l → 다리"), FindZone(TEXT("thigh_l")), EValorHitZone::Leg);
	TestEqual(TEXT("calf_r → 다리"), FindZone(TEXT("calf_r")), EValorHitZone::Leg);

	// 뼈 위치를 정면에서 수평으로 쏘면 그 뼈의 부위가 나와야 한다(애니메이션 없는 기본 자세).
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	auto ShootBone = [&](const TCHAR* BoneName)
	{
		const FVector Target = Mesh->GetSocketLocation(BoneName);
		const FVector Start = Target + FVector(300.0f, 0.0f, 0.0f);
		FValorLagCompHitResult HitResult;
		return LagComp->ConfirmHitAtCurrentPose(Start, (Target - Start).GetSafeNormal(), 1000.0f, HitResult) ? HitResult.HitZone : EValorHitZone::None;
	};
	TestEqual(TEXT("머리 뼈를 쏘면 머리"), ShootBone(TEXT("head")), EValorHitZone::Head);
	TestEqual(TEXT("가슴(spine_04)을 쏘면 몸"), ShootBone(TEXT("spine_04")), EValorHitZone::Body);
	TestEqual(TEXT("무릎 아래(calf_l)를 쏘면 다리"), ShootBone(TEXT("calf_l")), EValorHitZone::Leg);

	// 머리 위 30cm는 아무 데도 맞지 않는다(예전 고정 상자처럼 허공이 판정되지 않는다).
	const FVector AboveHead = Mesh->GetSocketLocation(TEXT("head")) + FVector(0.0f, 0.0f, 30.0f);
	FValorLagCompHitResult MissResult;
	TestFalse(TEXT("머리 위 30cm는 빗나감"), LagComp->ConfirmHitAtCurrentPose(AboveHead + FVector(300.0f, 0.0f, 0.0f), -FVector::ForwardVector, 1000.0f, MissResult));

	Character->Destroy();
	return true;
}

#endif

#include "Components/ValorViewmodelComponent.h"

#include "Animation/AnimSequence.h"
#include "Animation/ValorFirstPersonArmsAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ValorCharacterMovementComponent.h"
#include "Components/ValorCombatComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "ValorCharacter.h"
#include "Weapons/ValorMeleeWeapon.h"
#include "Weapons/ValorViewmodelTypes.h"
#include "Weapons/ValorWeaponBase.h"

namespace
{
	// First Person 템플릿(BP_FirstPersonCharacter)의 팔 메시 배치: 카메라 기준 (-30, 0, -151.33), 회전 0. 템플릿 크기 기준이다.
	const FVector TemplateArmsOffset(-30.0f, 0.0f, -151.33f);

	// 총 손잡이 프레임의 왼손 목표 소켓(가져온 총 14종 모두 있음).
	const FName LeftHandTargetSocketName(TEXT("Left_Hand_Target"));

	bool DoesSoftAssetExist(const FSoftObjectPath& Path)
	{
		return !Path.IsNull() && FPackageName::DoesPackageExist(Path.GetLongPackageName());
	}
}

UValorViewmodelComponent::UValorViewmodelComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 원격 캐릭터·서버에서는 쓰지 않으므로 기본은 꺼 두고, 로컬 플레이어일 때만 SetFirstPersonActive로 켠다.
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UValorViewmodelComponent::SetFirstPersonActive(bool bActive)
{
	bFirstPersonActive = bActive;
	SetComponentTickEnabled(bActive);
	SetRelativeScale3D(FVector(ViewmodelScale));
	ResetMotionState();

	if (!bActive)
	{
		SetRelativeLocationAndRotation(FVector::ZeroVector, FQuat::Identity);
	}
	else
	{
		EnsureArmsComponent();
	}

	UpdateArms(nullptr, nullptr);
}

void UValorViewmodelComponent::EnsureArmsComponent()
{
	if (ArmsComponent || bArmsLoadAttempted)
	{
		return;
	}

	bArmsLoadAttempted = true;
	USceneComponent* CameraComponent = GetAttachParent();
	if (!CameraComponent || !DoesSoftAssetExist(ArmsMesh.ToSoftObjectPath()))
	{
		return;
	}

	USkeletalMesh* LoadedArmsMesh = ArmsMesh.LoadSynchronous();
	if (!LoadedArmsMesh)
	{
		return;
	}

	// 로컬 플레이어 머신에서만 만든다. 다른 플레이어 캐릭터에는 팔 컴포넌트 자체가 없어 비용이 없다.
	ArmsComponent = NewObject<USkeletalMeshComponent>(GetOwner(), TEXT("FirstPersonArms"));
	ArmsComponent->SetupAttachment(CameraComponent);
	ArmsComponent->SetSkeletalMesh(LoadedArmsMesh);
	ArmsComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ArmsComponent->SetCastShadow(false);
	ArmsComponent->SetAnimInstanceClass(UValorFirstPersonArmsAnimInstance::StaticClass());

	// 템플릿 배치를 팔 크기만큼 키우고(카메라 기준) 앞으로 당긴 뒤, 총과 같은 뷰모델 축소 배율을 적용한다(화면상 크기는 그대로).
	const FVector ArmsLocation = (TemplateArmsOffset * ArmsScale) + ArmsShift;
	ArmsComponent->SetRelativeLocation(ArmsLocation * ViewmodelScale);
	ArmsComponent->SetRelativeScale3D(FVector(ArmsScale * ViewmodelScale));

	// 손 목표는 이 피벗의 이번 프레임 자세로 계산하므로, 팔 애니메이션은 피벗을 갱신한 뒤에 돈다.
	ArmsComponent->AddTickPrerequisiteComponent(this);
	ArmsComponent->RegisterComponent();

	if (UValorFirstPersonArmsAnimInstance* ArmsInstance = Cast<UValorFirstPersonArmsAnimInstance>(ArmsComponent->GetAnimInstance()))
	{
		UAnimSequence* RiflePose = DoesSoftAssetExist(ArmsRiflePose.ToSoftObjectPath()) ? ArmsRiflePose.LoadSynchronous() : nullptr;
		UAnimSequence* IdlePose = DoesSoftAssetExist(ArmsIdlePose.ToSoftObjectPath()) ? ArmsIdlePose.LoadSynchronous() : nullptr;
		ArmsInstance->SetPoses(RiflePose, IdlePose);
	}
}

void UValorViewmodelComponent::UpdateArms(const FActiveViewmodelItem* ActiveItem, const UValorCombatComponent* CombatComponent)
{
	if (!ArmsComponent)
	{
		return;
	}

	// 조준경 화면(총도 숨김)이거나 든 것이 없으면 팔도 숨긴다.
	const bool bShowArms = bFirstPersonActive && ActiveItem && CombatComponent && !CombatComponent->IsScopeOverlayActive();
	if (ArmsComponent->IsVisible() != bShowArms)
	{
		ArmsComponent->SetVisibility(bShowArms);
	}

	UValorFirstPersonArmsAnimInstance* ArmsInstance = Cast<UValorFirstPersonArmsAnimInstance>(ArmsComponent->GetAnimInstance());
	if (!bShowArms || !ArmsInstance)
	{
		return;
	}

	// 피벗 = 총 손잡이 프레임(총 액터 루트가 피벗 원점에 그대로 붙어 있다).
	const FTransform GripWorld = GetComponentTransform();
	FValorArmsHandTargets Targets;
	Targets.GripPointWorld = FTransform(ValorViewmodel::RigToArmsGripPointRotation()) * GripWorld;
	Targets.bRightHand = true;

	const AValorWeaponBase* Weapon = Cast<AValorWeaponBase>(ActiveItem->Item);
	const USkeletalMeshComponent* WeaponMesh = Weapon ? Weapon->GetWeaponMesh() : nullptr;
	if (WeaponMesh && WeaponMesh->DoesSocketExist(LeftHandTargetSocketName))
	{
		// 총열 아래 손잡이(Left_Hand_Target)를 왼손으로 잡는다. 보정은 총 리그 공간이므로 피벗 회전·배율을 그대로 받는다.
		Targets.LeftHandWorld = WeaponMesh->GetSocketLocation(LeftHandTargetSocketName) + GripWorld.TransformVector(LeftHandOffset);
		Targets.bLeftHand = true;
		Targets.bUseRiflePose = true;
	}
	else
	{
		// 칼: 오른손만 쥐고 왼팔은 맨손 대기 자세 그대로.
		Targets.bUseRiflePose = false;
	}

	ArmsInstance->SetHandTargets(Targets);
}

void UValorViewmodelComponent::ResetMotionState()
{
	ADSAlpha = 0.0f;
	ReloadAlpha = 0.0f;
	bHasLastControlRotation = false;
	SmoothedSway = FRotator::ZeroRotator;
	BobAmount = 0.0f;
	KickRotationSpring = FValorCameraKickSpring();
	KickTranslationSpring = FValorCameraKickSpring();
}

void UValorViewmodelComponent::AddFireKick(const FValorViewmodelConfig& Config, bool bIsADS)
{
	// 연출용 무작위라 서버/클라 결정성이 필요 없다.
	const float Scale = bIsADS ? Config.ADSKickScale : 1.0f;
	const float PitchKick = Config.KickPitchDegrees * FMath::FRandRange(0.8f, 1.0f) * Scale;
	const float YawKick = FMath::FRandRange(-Config.KickYawDegrees, Config.KickYawDegrees) * Scale;

	KickRotationSpring.AddKick(FVector(PitchKick, YawKick, 0.0f), Config.KickPeakTimeSeconds);
	KickTranslationSpring.AddKick(FVector(Config.KickBackCm * Scale, 0.0f, 0.0f), Config.KickPeakTimeSeconds);
}

bool UValorViewmodelComponent::ResolveActiveItem(const UValorCombatComponent& CombatComponent, FActiveViewmodelItem& OutItem) const
{
	if (const AValorWeaponBase* Weapon = CombatComponent.GetEquippedWeapon())
	{
		OutItem.Item = Weapon;
		OutItem.Config = &Weapon->GetViewmodelConfig();
		OutItem.SightHeightCm = Weapon->GetViewmodelSightHeightCm();
		OutItem.ADSInterpSpeed = Weapon->GetADSInterpSpeed();
		// 우클릭이 공격인 총(클래식·버키)은 조준 자세가 없다.
		OutItem.bCanADS = Weapon->GetMaxZoomLevel() > 0;
		return true;
	}

	const AValorMeleeWeapon* MeleeWeapon = CombatComponent.GetMeleeWeapon();
	if (MeleeWeapon && CombatComponent.GetActiveSlot() == EValorWeaponSlot::Melee)
	{
		OutItem.Item = MeleeWeapon;
		OutItem.Config = &MeleeWeapon->GetViewmodelConfig();
		return true;
	}

	return false;
}

void UValorViewmodelComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AValorCharacter* Character = Cast<AValorCharacter>(GetOwner());
	const UValorCombatComponent* CombatComponent = Character ? Character->GetCombatComponent() : nullptr;
	FActiveViewmodelItem ActiveItem;
	if (!bFirstPersonActive || !CombatComponent || !ResolveActiveItem(*CombatComponent, ActiveItem))
	{
		UpdateArms(nullptr, CombatComponent);
		return;
	}

	// 무기를 바꾸면 새 무기는 힙 자세에서 시작한다(조준·킥은 이전 무기 것이므로 버린다). 올라오는 모습은 장착 진행도가 만든다.
	if (LastActiveItem.Get() != ActiveItem.Item)
	{
		LastActiveItem = ActiveItem.Item;
		ADSAlpha = 0.0f;
		KickRotationSpring = FValorCameraKickSpring();
		KickTranslationSpring = FValorCameraKickSpring();
	}

	// 조준 보간은 카메라 FOV와 같은 무기별 속도라, 화면 확대와 총 올리기가 같이 끝난다.
	const bool bWantsADS = ActiveItem.bCanADS && CombatComponent->IsADSForGameplay();
	ADSAlpha = FMath::FInterpTo(ADSAlpha, bWantsADS ? 1.0f : 0.0f, DeltaTime, ActiveItem.ADSInterpSpeed);
	ReloadAlpha = FMath::FInterpTo(ReloadAlpha, CombatComponent->IsReloading() ? 1.0f : 0.0f, DeltaTime, ReloadBlendSpeed);

	FValorViewmodelPose Pose = ValorViewmodel::BlendHipToADS(*ActiveItem.Config, ActiveItem.SightHeightCm, ADSAlpha);

	// 장착: 진행도 0이면 화면 아래로 내려가 총구가 숙여진 상태, 끝나면 제자리.
	const float Lowered = 1.0f - ValorViewmodel::EquipRaiseAlpha(CombatComponent->GetEquipProgress());
	Pose.Location.Z -= (EquipDropCm * Lowered) + (ReloadDropCm * ReloadAlpha);
	Pose.ViewRotation.Pitch -= (EquipPitchDegrees * Lowered) + (ReloadPitchDegrees * ReloadAlpha);
	Pose.ViewRotation.Roll += ReloadRollDegrees * ReloadAlpha;

	// 시선 흔들림·걸음 흔들림(조준 중에는 줄인다).
	UpdateSway(DeltaTime, *Character);
	UpdateBob(DeltaTime, *Character);
	const float SwayScale = FMath::Lerp(1.0f, ADSSwayScale, ADSAlpha);
	Pose.ViewRotation += SmoothedSway * SwayScale;
	Pose.Location.Y += SmoothedSway.Yaw * SwayTranslationCmPerDegree * SwayScale;
	Pose.Location.Z += SmoothedSway.Pitch * SwayTranslationCmPerDegree * SwayScale;

	// 좌우 1주기에 상하 2주기(발걸음마다 한 번씩 내려앉는 8자 궤적).
	const float BobScale = BobAmount * FMath::Lerp(1.0f, ADSBobScale, ADSAlpha);
	Pose.Location.Y += FMath::Sin(BobPhase) * BobHorizontalCm * BobScale;
	Pose.Location.Z -= FMath::Abs(FMath::Sin(BobPhase)) * BobVerticalCm * BobScale;

	// 사격 킥: 총구가 들리고(손잡이 기준 회전) 총이 뒤로 밀린다.
	KickRotationSpring.Advance(DeltaTime, KickMaxDegrees);
	KickTranslationSpring.Advance(DeltaTime, KickMaxBackCm);
	Pose.ViewRotation += KickRotationSpring.ToRotator();
	Pose.Location.X -= KickTranslationSpring.Offset.X;

	// 리그 축 → 뷰 축 변환을 먼저 적용하고, 그 위에 뷰 공간 회전(힙 기울기·흔들림·킥)을 얹는다(FQuat 곱은 오른쪽부터 적용).
	const FQuat PivotRotation = FQuat(Pose.ViewRotation) * ValorViewmodel::GripToViewRotation();
	SetRelativeLocationAndRotation(Pose.Location * ViewmodelScale, PivotRotation);

	// 총이 자리를 잡은 뒤 두 손 목표를 넘긴다(팔 애니메이션은 이 컴포넌트 다음에 돈다).
	UpdateArms(&ActiveItem, CombatComponent);
}

void UValorViewmodelComponent::UpdateSway(float DeltaTime, const AValorCharacter& Character)
{
	const FRotator ControlRotation = Character.GetControlRotation();
	if (!bHasLastControlRotation || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		LastControlRotation = ControlRotation;
		bHasLastControlRotation = true;
		return;
	}

	const FRotator Delta = (ControlRotation - LastControlRotation).GetNormalized();
	LastControlRotation = ControlRotation;

	// 시선을 돌리면 총이 반대쪽으로 뒤처진다: 오른쪽으로 돌리면 총은 왼쪽(Yaw -), 위로 들면 아래(Pitch -).
	// 돌리는 방향으로 살짝 기울여(Roll) 손목이 따라오는 느낌을 준다.
	const float YawRate = Delta.Yaw / DeltaTime;
	const float PitchRate = Delta.Pitch / DeltaTime;
	FRotator TargetSway(
		FMath::Clamp(-PitchRate * SwayDegreesPerDegreePerSecond, -MaxSwayDegrees, MaxSwayDegrees),
		FMath::Clamp(-YawRate * SwayDegreesPerDegreePerSecond, -MaxSwayDegrees, MaxSwayDegrees),
		0.0f);
	TargetSway.Roll = TargetSway.Yaw * 0.5f;

	SmoothedSway = FMath::RInterpTo(SmoothedSway, TargetSway, DeltaTime, SwayInterpSpeed);
}

void UValorViewmodelComponent::UpdateBob(float DeltaTime, const AValorCharacter& Character)
{
	const UValorCharacterMovementComponent* MovementComponent = Character.GetValorMovement();
	if (!MovementComponent)
	{
		return;
	}

	// 속도 비율 = 칼 달리기(6.75m/s) 대비. 공중에서는 흔들림을 빼서 점프 중에는 총이 고정된다.
	const float BaseRunSpeed = FMath::Max(MovementComponent->GetBaseRunSpeed(), 1.0f);
	const float SpeedRatio = FMath::Clamp(static_cast<float>(Character.GetVelocity().Size2D()) / BaseRunSpeed, 0.0f, 1.0f);
	const float TargetAmount = MovementComponent->IsMovingOnGround() ? SpeedRatio : 0.0f;
	BobAmount = FMath::FInterpTo(BobAmount, TargetAmount, DeltaTime, 6.0f);

	BobPhase = FMath::Fmod(BobPhase + (DeltaTime * UE_TWO_PI * BobFrequencyHz * SpeedRatio), UE_TWO_PI);
}

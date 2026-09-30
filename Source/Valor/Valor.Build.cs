// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Valor : ModuleRules
{
	public Valor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);
		PrivateIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"NetCore",
			// 트레이서/총구 화염/탄착 이펙트(나이아가라) 재생용. 엔진 기본 활성 플러그인이라 추가 설정이 필요 없다.
			"Niagara"
		});
	}
}

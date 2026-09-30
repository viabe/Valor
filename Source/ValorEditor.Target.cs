// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class ValorEditorTarget : TargetRules
{
	public ValorEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_4;
		ExtraModuleNames.Add("Valor");
		// 에디터 전용 도구(무기 데이터 에셋 생성 커맨드렛 등). 게임/서버 타깃에는 넣지 않는다.
		ExtraModuleNames.Add("ValorEditor");
	}
}

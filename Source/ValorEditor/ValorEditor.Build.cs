// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 에디터 전용 도구 모듈(무기 데이터 에셋 생성 커맨드렛, 무기 프리셋 검증 테스트).
// 왜 별도 모듈인가: 에셋 저장(UPackage::SavePackage)·커맨드렛은 에디터에서만 필요하다.
// 런타임 모듈(Valor)에 두면 게임/데디케이티드 서버 바이너리에 쓸모없는 코드와 수치 테이블이 함께 실린다.
// .uproject에 Type "Editor"로 등록돼 있어 에디터 빌드에서만 컴파일·로드된다.
public class ValorEditor : ModuleRules
{
	public ValorEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AssetRegistry",
			"Valor"
		});
	}
}

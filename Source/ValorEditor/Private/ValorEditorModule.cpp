#include "Modules/ModuleManager.h"

// 에디터 전용 도구 모듈. 시작/종료 시 할 일이 없어 기본 구현을 쓴다(커맨드렛·테스트는 리플렉션/자동 등록으로 발견된다).
IMPLEMENT_MODULE(FDefaultModuleImpl, ValorEditor);

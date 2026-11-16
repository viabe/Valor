# 요원 스킬(5단계)

요원은 스킬 네 칸(C / Q / E / X)을 가진다. 스킬은 GAS 어빌리티로 만들고, 충전·궁극기 포인트·구매는 경기 규칙이 관리한다.
요원 이름과 스킬 구성은 직접 만든다(연막·섬광·돌진 같은 장르 공통 메커닉을 조합하고, 다른 게임의 캐릭터를 옮기지 않는다).

## 1. 계획

| 단계 | 내용 | 상태 |
|---|---|---|
| 5-1 스킬 틀 | 네 칸, 충전, 상점 구매·되팔기, 시그니처 무료 충전, 궁극기 포인트, 입력, HUD, 요원 정의 | 완료 |
| 5-2 스킬 부품 | 투척물(튕김·붙음), 연막(시야 차단), 섬광(시선 각도·가림 판정), 이동(돌진·도약, 이동 예측), 지속 회복, 벽, 정찰(위치 표시) | 완료(샘플 요원 Drift·Flare) |
| 5-3 요원 | 부품을 조합한 요원 몇 명, 요원 선택 화면 | 이후 |

## 2. 스킬 틀(5-1)

| 항목 | 규칙 |
|---|---|
| 칸 | C(Ability1) / Q(Ability2) / E(Signature) / X(Ultimate). 키는 런타임 입력 컨텍스트(무기 키와 같은 곳) |
| 충전 | PlayerState에 둔다(죽고 다시 스폰돼도 남는다). 다음 라운드로 그대로 가져가고, 경제 초기화 라운드(1·13라운드)에는 무료 충전만 남는다 |
| 시그니처 | `FreeChargesPerRound`만큼 매 라운드 무료로 채워진다 |
| 구매 | `Cost > 0`인 칸만 상점(구매 메뉴 ABILITIES 열)에서 산다. 최대 충전까지. 이번 라운드에 산 충전은 되판다 |
| 궁극기 | 포인트가 `UltimatePointsRequired`만큼 모이면 쓴다(쓰면 0). 처치·사망·설치·해체 각 1점(`UltimatePointsPerEvent`) |
| 사용 조건 | 살아 있음(State.Dead로 막힘), 구매 메뉴가 닫혀 있음. 구매 단계에도 쓸 수 있다 |

- 코드:
  - `UValorAgentDefinition`(요원 정의, 네 칸 설정)
  - `UValorAgentAbility`(스킬 부모: 비용 확인·차감)
  - `ValorAbilityRules`(충전·구매·포인트 규칙)
  - `AValorMatchGameMode`(요원 지정·충전·부여·구매)
- 부여: 스폰할 때마다 캐릭터 ASC에 네 칸을 부여한다. 칸은 스펙 InputID(10 + 칸 번호)로 구분하고, 입력은 `AbilityLocalInputPressed`로 같은 번호를 찾는다.
- 네트워크:
  - 기본은 서버 실행이다. 클라 입력이 ServerTryActivateAbility로 가서 서버가 활성화한다.
  - 이동 스킬(돌진)은 LocalPredicted로, 소유 클라가 바로 움직이고 서버도 같은 발사를 한다.
  - 충전·포인트는 서버만 바꾸고 PlayerState로 복제한다.

## 3. 스킬 부품(5-2)

| 부품 | 클래스 | 동작 |
|---|---|---|
| 공통 부모 | `AValorAbilityActor` | 만든 사람의 팀을 기억, 수명, 라운드 시작에 모두 정리 |
| 투척물 | `AValorAbilityProjectile` | 던지면 튕기거나(Bounciness) 처음 면에 붙고(bStickOnHit), 퓨즈 시간·멈춤·붙음 때 효과(PayloadClass)를 만든다. 캐릭터는 통과 |
| 연막 | `AValorSmokeCloud` | 바깥 면 + 뒤집은 안쪽 면 두 겹이라 안에서도 너머가 안 보인다. 탄은 막지 않는다. 섬광·정찰의 시야 판정도 가린다 |
| 섬광 | `AValorFlashBurst` | 반경 안 플레이어마다 가림 여부 + 시선 각도로 눈부심 시간: 60도 안 1.75초, 110도 안 0.6초, 뒤 없음. 아군·자신도 눈부심. 그 플레이어 화면에만 흰 화면(`ClientFlashed`) |
| 정찰 | `AValorReconPulse` | 반경 15m 안, 가림 없이 보이는 적을 3초 동안 우리 팀 화면에 "SPOTTED"로 표시(벽 너머로 보임, 캐릭터 복제 값) |
| 벽 | `AValorAbilityWall` | 캐릭터·탄·카메라를 막는다. 체력 800, 총에 맞으면 그 무기 몸통 피해만큼 깎이고 0이면 부서진다. 20초 |
| 지속 회복 | `AValorHealOverTime` | 4초 동안 60을 나눠 준다(서버 전용, 대상에 붙음) |
| 이동 | `UValorAbility_Launch` | 수평·위 속도로 튀어 오른다(돌진·도약). 이동 예측 실행 |

범용 스킬: `UValorAbility_Throw`(투척물 클래스), `UValorAbility_Launch`, `UValorAbility_HealOverTime`, `UValorAbility_DeployWall`, `UValorAbility_Overdrive`. 수치만 다른 프리셋은 하위 클래스로 둔다(`UValorAbility_ThrowSmoke` 등, BP로도 가능).

## 4. 샘플 요원(이 프로젝트에서 만든 요원)

| 요원 | C | Q | E(시그니처, 무료) | X(궁극기) |
|---|---|---|---|---|
| Drift | Mend: 지속 회복 60(200, 1회) | Smoke Canister: 연막 통(100, 2회) | Dash: 돌진 4.8m | Overdrive: 체력·실드 가득(6점) |
| Flare | Updraft: 도약 3.3m(150, 2회) | Flash Charge: 섬광탄(200, 2회) | Recon Dart: 정찰 다트 | Barrier: 방벽(6점) |

모양은 엔진 기본 도형(구·정육면체)이다. 이펙트·소리·애니메이션은 디자인 때 BP에서 바꾼다.

## 5. 새 요원 만들기

- C++: `UValorAgentDefinition`을 상속하고 생성자에서 네 칸(스킬 클래스, 이름, 가격, 충전, 무료 충전, 궁극기 포인트)을 정한다(`UValorAgent_Drift` 참고).
- 에디터: `UValorAgentDefinition`을 부모로 BP(또는 데이터 에셋)를 만들어 칸을 채운다. 스킬은 `UValorAgentAbility`를 상속해 `ActivateAgentAbility`만 구현한다.
- 지금은 모두 `BP_ValorMatchGame`의 Default Agent Class(기본 Drift)를 받는다. 고를 수 있는 요원 목록은 Available Agents. 요원 선택 화면은 5-3, 그 전에는 `ValorAgent <이름>`.

## 6. 개발용 명령과 확인 도구

| 명령 / 도구 | 내용 |
|---|---|
| `ValorUltPoints 6` | 내 궁극기 포인트를 정한다 |
| `ValorAgent Flare` | 요원을 바꾼다(충전은 그 요원의 1라운드 상태로, 지금 캐릭터의 스킬도 바로 바뀜) |
| `ValorCredits 9000` | 크레딧(스킬 구매 확인) |
| `Valor.Match.Rules.Abilities` | 라운드 시작 충전, 구매 조건, 궁극기 포인트 상한 |
| `Valor.Tools.AgentFlow` | 혼자(Drift): 스킬 구매·되팔기, 실제 C·Q·E·X 키 사용(연막 통 착지 연막, 지속 회복, 돌진, 과충전), 충전 소모, 궁극기, 다음 라운드 충전 유지·시그니처 재충전, 화면 캡처(`Saved/AgentFlow`) |
| `Valor.Tools.AbilityBlocks` | 혼자 + 적 봇(Flare): 정찰 다트 → 적 표시, 섬광탄 → 눈부심과 회복, 도약, 방벽 → 총에 맞아 체력 감소(앞을 가린 캐릭터에 막힌 탄은 벽에 안 닿음), 다음 라운드 정리, 화면 캡처(`Saved/AbilityBlocks`) |

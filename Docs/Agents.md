# 요원 스킬(5단계)

요원은 스킬 네 칸(C / Q / E / X)을 가진다. 스킬은 GAS 어빌리티로 만들고, 충전·궁극기 포인트·구매는 경기 규칙이 관리한다.
요원 이름과 스킬 구성은 직접 만든다(연막·섬광·돌진 같은 장르 공통 메커닉을 조합하고, 다른 게임의 캐릭터를 옮기지 않는다).

## 1. 계획

| 단계 | 내용 | 상태 |
|---|---|---|
| 5-1 스킬 틀 | 네 칸, 충전, 상점 구매·되팔기, 시그니처 무료 충전, 궁극기 포인트, 입력, HUD, 요원 정의 | 완료(샘플 요원 Drift) |
| 5-2 스킬 부품 | 투척물(튕김·붙음), 연막(시야 차단), 섬광(시선 각도·가림 판정), 이동(돌진·도약, 이동 예측), 회복, 벽, 정찰(위치 표시) | 다음 |
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

## 3. 샘플 요원 Drift(틀 확인용)

| 칸 | 스킬 | 가격 / 충전 | 효과 |
|---|---|---|---|
| C | Mend | 200 / 1 | 체력 +40(최대 체력까지) |
| Q | Smoke Canister | 100 / 2 | 조준점(최대 25m)에 15초 연막(지름 9m 구, 시야만 가림) |
| E | Dash | 무료 1 / 1 | 보는 수평 방향으로 돌진(약 4.8m) |
| X | Overdrive | 궁극기 6점 | 체력·실드(50) 가득 |

모양은 엔진 기본 도형이다. 연막은 바깥에서만 가려 보이고 안에서는 보이지 않는다. 연막 안쪽 처리는 5-2에서 한다.

## 4. 새 요원 만들기

- C++: `UValorAgentDefinition`을 상속하고 생성자에서 네 칸(스킬 클래스, 이름, 가격, 충전, 무료 충전, 궁극기 포인트)을 정한다(`UValorAgent_Drift` 참고).
- 에디터: `UValorAgentDefinition`을 부모로 BP(또는 데이터 에셋)를 만들어 칸을 채운다. 스킬은 `UValorAgentAbility`를 상속해 `ActivateAgentAbility`만 구현한다.
- 지금은 모두 `BP_ValorMatchGame`의 Default Agent Class(기본 Drift)를 받는다. 요원 선택은 5-3.

## 5. 개발용 명령과 확인 도구

| 명령 / 도구 | 내용 |
|---|---|
| `ValorUltPoints 6` | 내 궁극기 포인트를 정한다 |
| `ValorCredits 9000` | 크레딧(스킬 구매 확인) |
| `Valor.Match.Rules.Abilities` | 라운드 시작 충전, 구매 조건, 궁극기 포인트 상한 |
| `Valor.Tools.AgentFlow` | 혼자: 스킬 구매·되팔기, 실제 C·Q·E·X 키 사용과 충전 소모, 궁극기, 다음 라운드 충전 유지·시그니처 재충전, 화면 캡처(`Saved/AgentFlow`) |

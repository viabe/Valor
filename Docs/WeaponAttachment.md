# 총을 캐릭터에 붙이는 방식 — 현재 구조, 발로란트 방식, 권장안

## 1. 지금 어떻게 붙어 있나 (2026-10-07 조사)

| 단계 | 내용 | 코드/에셋 |
|---|---|---|
| ① 캐릭터 쪽 기준점 | UE5 마네킹 스켈레톤(`SKM_Manny`)의 소켓 `hand_r_Socket`. `hand_r` 뼈에 붙어 있고 위치 (-8.5, 1.4, 0), 회전 0이다. | `AValorCharacter::WeaponAttachSocketName = "hand_r_socket"` (FName은 대소문자를 무시하므로 같은 소켓) |
| ② 무기 액터 붙이기 | 무기 액터를 그 소켓에 스냅한다(위치·회전만, 크기는 유지). | `AValorWeaponBase::OnEquippedBy` |
| ③ 총 메시 정렬 | 총 메시를 먼저 Identity로 초기화한다. 그다음 데이터 에셋의 Grip Socket Name(기본 `GripSocket`)이 손 소켓과 정확히 겹치도록, 그 소켓의 역변환만큼 메시를 옮기고 돌린다. 소켓이 없으면 초기화한 상태로 둔다. | `AValorWeaponBase::RefreshWeaponMeshAlignment` |
| ④ 총구 | 총구 이펙트와 트레이서는 데이터 에셋 Effects → Muzzle Socket Name(기본 `Muzzle`)에서 나간다. | `GetMuzzleLocation`, `PlayFireEffects` |

판정(탄도)은 총이 아니라 카메라 위치와 시선 방향에서 나간다. 그래서 총이 어디에 붙든 명중에는 영향이 없고, 총은 순수한 연출이다.

### 가져온 총 메시 상태 (뼈·소켓 덤프)

| 총 | 메시 임포트 방향 | GripSocket |
|---|---|---|
| 밴달(Araxys 스킨) | 총구가 -Y 방향, 메시 원점이 총보다 약 70cm 아래(바닥 피벗) | 있음 — `Root` 뼈, 위치·회전 0 |
| 팬텀·스펙터·불독·스팅어·가디언·워든·저지·버키·마샬·아웃로·오딘·아레스 | 총구가 +X 방향, 원점은 총 근처 | **없음** |

메시마다 임포트 방향은 다르지만, 13종 모두 `Root` 뼈의 축 규칙은 같다.
- Z = 총구 방향
- Y = 총의 아래쪽
- X = 총의 왼쪽

그래서 Root에 맞추면 임포트 방향 차이가 사라진다. 밴달이 손에 잘 맞은 이유가 이것이다.

### 확인된 문제
- **12종은 GripSocket이 없다.** ③이 초기화만 하고 끝나서, 메시 원점과 축이 그대로 손 소켓에 붙는다. 그래서 손에서 어긋나고 방향도 틀어진다.
- **무기 BP의 메시 회전은 무시된다.** 무기 BP에서 메시를 Y90/Y180으로 돌려 둔 값은 장착 순간 ③의 초기화로 사라진다. 바닥 픽업은 픽업 액터의 자체 표시 회전을 쓰므로 영향이 없다.
- **1인칭 화면에는 팔이 없다.** 몸만 숨기고(`OwnerNoSee`) 총은 숨기지 않는다. 그래서 팔 없이 총만, 보이지 않는 3인칭 손에 붙어 떠 있다.
  - 총 위치가 3인칭 애니메이션(대기·걷기·에임 오프셋)에 끌려다녀 화면 구도를 잡을 수 없다.
  - ADS는 FOV만 바뀌고, 조준기가 화면 중앙에 오지 않는다.
- **3인칭(다른 사람 화면)은 왼손이 총을 안 잡는다.** `ABP_ValorAnimInstance`에 IK 노드가 없어서, 총마다 다른 손잡이 위치를 왼손이 따라가지 못한다.

## 2. 지금 방식으로 총에 소켓 다는 법 (밴달에 했던 방법)

**방법 1 — 소켓 추가 (권장, 총별 미세 조정 가능)**
1. 총의 스켈레톤 에셋을 연다: `Content/Asset/GUN/<총>/<총>_Skeleton`. 버키만 이름이 `VALORANT_Weapon_Bucky_Shotgun_Skeleton`이다.
2. Skeleton Tree에서 `Root`를 우클릭 → Add Socket → 이름을 `GripSocket`으로 바꾼다.
3. 소켓을 Relative Location 0, Rotation 0, Scale 1로 둔다(밴달과 같은 규칙).
4. 저장하고 PIE에서 쥔 모습을 확인한다.
5. 손에서 조금 어긋나면 그 총의 `GripSocket` 위치만 몇 cm 옮긴다. 캐릭터의 `hand_r_Socket`은 모든 총이 함께 쓰므로 건드리지 않는다.

**방법 2 — 데이터 에셋만 수정 (가장 빠름, 미세 조정 불가)**
- `DA_<총>`의 Grip Socket Name을 `GripSocket`에서 `Root`로 바꾼다.
- 소켓 조회 함수(`DoesSocketExist`, `GetSocketTransform`)는 뼈 이름도 받는다. 그래서 결과는 방법 1과 같다.

공통: 무기 BP 메시의 Y90/Y180 회전은 0으로 돌려 두면 헷갈리지 않는다. 어차피 장착할 때 무시된다.

## 3. 발로란트는 어떻게 하나 (조사)

**1인칭과 3인칭이 따로 있다.** 내 화면에는 "1인칭 팔(viewarms) + 1인칭 총"을, 다른 사람 화면에는 "3인칭 요원 몸 + 3인칭 총"을 그린다.
- **Riot 인터뷰:** 시니어 프로듀서 Steven Eldredge가 왼손 뷰모델(1인칭 팔·총 좌우 반전) 작업을 설명하며 "first person weapons and viewarms"를 따로 언급했다.
  - 1인칭만 좌우로 뒤집을 수 있다는 것 자체가 1인칭 팔+총이 3인칭과 분리된 리그라는 뜻이다.
- **Riot 기술 블로그 (셰이더와 게임플레이 명확성):** 그림자는 1인칭 오브젝트, 즉 내 손과 무기에만 드리운다.
- **"How the VALORANT Arsenal was built":**
  - 무기마다 1인칭과 3인칭 양쪽에서 알아볼 수 있는 시각 표식을 둔다.
  - 총구 화염도 1인칭과 3인칭을 따로 구분해 만들었다.

**총 리그 (가져온 메시 13종에서 확인)**

| 뼈 | 있는 총 | 역할 (뼈 이름으로 본 추정) |
|---|---|---|
| `Root` | 13종 전부 | 손잡이 부근. 손(무기 뼈)에 붙는 기준점 |
| `Muzzle` | 13종 전부 | 총구 이펙트 위치 |
| `Left_Hand_Target` | 13종 전부 | 왼손이 잡을 위치. 왼손 IK 목표 |
| `Charging_Handle` | 대부분 | 장전 손잡이 |
| `Magazine`, `Ejection`, `Bullet` 등 | 총마다 다름 | 장전·탄피 애니메이션 부품 |

→ 정리하면 이런 구조로 보인다.
- 오른손이 `Root`를 잡는다.
- 왼손은 IK로 `Left_Hand_Target`에 붙인다.
- 총별 애니메이션이 부품 뼈를 움직인다.

Riot이 리그를 공개한 적은 없으므로, 이 부분은 뼈 이름에서 읽은 추정이다.

## 4. 비교와 판단

| 항목 | 지금 (3인칭 몸의 손 소켓 하나) | 발로란트식 (1인칭/3인칭 분리) |
|---|---|---|
| 내 화면 | 팔 없이 총만 떠 있고, 3인칭 애니메이션에 끌려다닌다 | 카메라 기준 팔+총. 구도·반동 킥·장전·ADS 정렬을 1인칭 전용으로 제어한다 |
| 다른 사람 화면 | 3인칭 몸+총. 왼손이 총을 안 잡는다 | 3인칭 몸+총 + 왼손 IK |
| ADS | FOV만 바뀐다 | 조준기를 화면 중앙에 정렬한다 |
| 벽 클리핑 | 총이 벽을 뚫고 보이기 쉽다 | 1인칭 메시를 따로 다뤄 줄이기 쉽다 |
| 총구 이펙트 | 한 곳에서 재생 | 내 화면은 1인칭 총구, 남의 화면은 3인칭 총구에서 따로 재생 |
| 필요한 것 | 메시 1개, 3인칭 애니메이션 1세트 | 1인칭 팔 메시, 1인칭 애니메이션, 메시 가시성 관리 |
| 네트워크 | 연출만, 판정 무관 | 1인칭은 로컬 전용(복제·서버 비용 없음), 판정 무관 |

**판단**
- 발로란트식 분리가 맞다. FPS의 손맛(반동 킥, 장전, 조준)은 대부분 1인칭 뷰모델에서 나오고, 포트폴리오에서도 가장 먼저 보이는 부분이다.
- 지금 구조(무기 액터, 그립 정렬, 데이터 에셋, 카메라 기준 판정)는 버릴 게 없다. 3인칭은 지금 방식을 그대로 쓰고 1인칭을 덧붙이면 된다.
- 판정이 카메라 기준이라 1인칭을 추가해도 서버 권한·결정론 규칙에는 영향이 없다.

## 5. 다음 개발 후보

| 후보 | 내용 | 크기 |
|---|---|---|
| A. 그립 정리 | GripSocket이 없으면 `Root` 뼈로 자동 대체하는 코드 몇 줄을 넣는다. 또는 2절의 방법 1·2를 에디터에서 한다. 12종이 바로 손에 맞는다 | 작음 |
| B. 3인칭 왼손 IK | `UValorAnimInstance`가 총의 `Left_Hand_Target` 위치를 받고, 애님 BP의 Two Bone IK로 왼손을 붙인다 | 중간 |
| C-1. 1인칭 총 (에셋 없이 시작) | 무기에 1인칭 메시(나만 보임)를 추가해 카메라 기준 위치에 둔다. 총별 힙·ADS 위치(조준기 정렬)는 데이터 에셋에 넣는다. 발사 킥·이동 흔들림·시선 sway는 C++로 계산한다. 3인칭 메시는 지금처럼 손 소켓에 두고 남에게만 보이게 한다 | 중간~큼 |
| C-2. 1인칭 팔 + 애니메이션 | 1인칭 팔 메시와 1인칭 애님 BP(대기·발사·장전·장착)를 만들고, 왼손 IK는 B와 같은 계산을 재사용한다. 프로젝트에 1인칭 팔 에셋이 없으므로 UE First Person 템플릿 콘텐츠의 팔·라이플 애니메이션으로 시작할 수 있다 | 큼 (에셋 필요) |
| D. 엔진 1인칭 렌더링 | UE First Person Rendering(1인칭 전용 FOV, 클리핑 방지) | 5.4.4 엔진 소스에 없음 → 엔진 업그레이드 필요 |

권장 순서: **A → C-1 → C-2 (B는 C-2에서 같이)**
- A는 버그 수정 수준이고, 1인칭이든 3인칭이든 필요하다.
- C-1만으로도 화면이 크게 달라진다. 총이 카메라에 고정되고, ADS 때 조준기가 중앙에 온다.
- 왼손 IK는 1인칭 팔과 3인칭 몸이 같은 계산(`Left_Hand_Target`)을 쓰므로 한 번에 만드는 게 효율적이다.

## 출처
- ONE Esports, Riot 왼손 뷰모델 인터뷰 (Steven Eldredge, "first person weapons and viewarms"): https://www.oneesports.gg/valorant/riot-is-exploring-a-left-handed-view-mode-for-valorant/
- Riot, "VALORANT Shaders and Gameplay Clarity" (1인칭 손·무기에만 그림자): https://www.riotgames.com/en/news/valorant-shaders-and-gameplay-clarity
- Riot, "How the VALORANT Arsenal was built" (1인칭·3인칭 무기 표식, 총구 화염): https://playvalorant.com/en-us/news/dev/how-the-valorant-arsenal-was-built/
- Epic, First Person Rendering 문서: https://dev.epicgames.com/documentation/en-us/unreal-engine/first-person-rendering
- 프로젝트 조사 (2026-10-07)
  - 총 메시 뼈·소켓 덤프: 임시 자동화 테스트로 추출한 뒤 삭제
  - `ABP_ValorAnimInstance` 노드 확인: IK 노드 없음
  - 엔진 소스 확인: `SkinnedMeshComponent.cpp`의 소켓 조회가 뼈 이름도 받음, 5.4.4에 First Person Rendering 없음

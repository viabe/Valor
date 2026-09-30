# 밴달 반동 시스템 (현재 발로란트 기준)

## 1. 조사로 확인한 사실 (근거)

| 항목 | 값 / 동작 | 출처 |
|---|---|---|
| 반동 모델 | "하이브리드": 앞쪽 몇 발은 완전 결정적, 이후 의사난수 편차 | Riot 개발자 답변(Riot_Classick, 2020) |
| 앉기 / ADS | 둘 다 반동 감소, **서로 곱해짐** | 같은 답변 |
| 보호 탄 수 | 밴달 **6발**(PC, 4 → 6) | 패치 11.08 |
| 수평 방향 전환 확률 | 발마다 **10%**(6% → 10%) | 패치 11.08 |
| 수평 방향 전환 시간 | **0.6초**(0.37 → 0.6) | 패치 11.08 |
| 수직 반동 | "Vertical (Pitch) recoil curve" + 총량(total recoil) | 패치 11.08, 6.11 |
| Gun Recovery Time | 밴달 **0.375초** | 패치 0.50 |
| Tap Efficiency | 밴달 **6**, 값이 클수록 회복 전 재사격 시 부정확도 누적이 느림 | 패치 0.50 |
| 앉기(정지) 수평 반동 | **-15%** | 패치 0.50 |
| 달리며 쏠 때 수직 반동 | **×1.8** | 패치 6.11 |
| Error Power(중심 편향) | 정지 시 중앙 쏠림, 이동 중에는 "거의 균일" | 패치 6.11 |
| 이동 오차(라이플) | 걷기 3° / 달리기 6° / 앉아서 이동 0.8° / 공중 10° / 착지 7°(0.225초) / 데드존 27.5% | 9.10, 2.02, 위키, 1.09 |
| 발사 속도 | 힙 9.75 / ADS 8.775 발/초 | valorant-api |
| 첫 발 탄퍼짐 | 힙 0.25° / ADS 0.1575°, 최대 1.0° / 1.02°, 앉기 ×0.85 | valorant-api, 위키 |
| ADS | 1.25배 줌, **"Crosshair follows recoil"** | 위키 |
| 힙파이어: 패턴 | **크로스헤어는 스프레이 패턴을 따라가지 않고 탄이 크로스헤어 위로 올라감** ("Shooting without ADS, your crosshair does not follow your spray") | r/ValorantCompetitive 등 다수 |
| 힙파이어: 화면 | **매 발 화면이 흔들림(카메라 킥)** — "it shakes the whole screen ... its not the recoil, because recoil is managed by the spray pattern", "camera recoil is fairly consistent ... whether or not your spray pattern is small, or is maxed out" | r/VALORANT, VALORANT Forums(2021~2024 불만 글) |
| 넷코드 | 클라가 자기 입력을 즉시 예측, 발사 시각을 서버로 보내면 서버가 그 시각으로 되감아 판정 | Riot 기술 블로그 |

**추정치(공개 수치 없음, 데이터 자산에서 조정 가능)**: 수직 반동 발당 크기(커뮤니티 측정값: 보호 탄 동안 0.58 → 0.28°/발, 총 약 3.5°), 수평 드리프트 크기(0.2 → 0.05°/발), 탄퍼짐이 최대에 도달하는 발 수(8발), ADS 반동 배율(0.9), 정지 Error Power(1.0), 카메라 킥 크기(힙 1.0° / ADS 0.4°, 0.04초에 최고점).

### 이전 구현이 "발로란트 같지 않았던" 이유
1. **힙파이어에서 카메라가 스프레이 패턴을 1:1로 따라 올라갔다.** 발로란트 힙파이어는 크로스헤어가 패턴을 따라가지 않고(탄만 위로), 대신 매 발 화면이 튀었다 돌아온다. 패턴을 끝까지 따라가는 것은 ADS일 때만이다.
   - 1차 수정(2026-09-29)에서 패턴 추종을 0으로 바꾸면서 매 발 화면 킥까지 빠져 "카메라 반동이 아예 없는" 상태가 됐고, 2차 수정에서 카메라 킥을 추가했다.
2. **크로스헤어 HUD가 없었다.** 기준점이 없으니 "탄이 조준점에서 벗어난다"는 발로란트식 반동을 느낄 수 없었다(예전에 "반동이 아예 없다"고 느낀 원인).
3. **패턴 크기가 약 2배 컸다.** 수직 약 10.5°, 좌우 ±3.2°(실제 밴달은 약 3.5°, 좌우 ±1~1.5°).
4. 회복이 "완전 초기화 아니면 0" 방식이라 탭/버스트 사격의 부분 회복(Tap Efficiency)이 없었다.
5. 반동이 서버 멀티캐스트를 거쳐 적용돼 원격 클라에서는 RTT만큼 늦었다.
6. `DA_Vandal`이 무기 블루프린트에 연결돼 있지 않아 발사 몽타주가 재생되지 않았다.

## 2. 동작 모델

```
스프레이 진행도(발 단위 실수) ──► 수직 반동 커브 ──► Pitch
        │                    └─► 탄퍼짐 커브 ──► 첫 발 오차 ~ 최대 오차
        │
        ├─ 풀오토 간격: +1발씩
        ├─ 회복: 마지막 발 후 (발사 간격 × 1.25)까지 유지 → Gun Recovery Time(0.375초)에 0으로 회복
        └─ 회복 중 재사격: 증분이 1 → 1/TapEfficiency로 감소 (탭/버스트가 유리)

보호 탄(6발) 이후 수평: 시드로 좌/우 선택 → 발마다 10% 확률로 반대편 전환 → 0.6초에 걸쳐 넘어감
최종 탄 방향 = 조준(컨트롤 회전) × 반동(로컬 Pitch/Yaw) × 탄퍼짐(반경 = 오차 × U^ErrorPower)
카메라 = 반동 × CameraRecoilFollowRatio (힙 0, ADS 1)  +  매 발 카메라 킥(임계 감쇠 스프링)
         └ 킥: 한 발마다 위로 약 1°(ADS 0.4°) 튀고 0.04초에 최고점, 연사 중에는 약 0.6~1.2° 들린 채 떨리고
                멈추면 약 0.2초 안에 복귀. 조준·서버 탄도에는 영향 없음(연출).
```

오프라인 시뮬레이션과 자동화 테스트로 확인한 동작:
- 풀오토: 발마다 진행도 +1 (패턴을 그대로 따라감)
- 0.3초 간격 탭: 진행도 약 0.2 (거의 첫 발 정확도) / 0.2초 간격: 약 1.8 / 0.15초 간격: 약 6
- 3발 버스트 → 0.2초 쉼 → 3발: 두 번째 버스트는 진행도 약 2부터 다시 시작

## 3. 코드 구조

| 파일 | 책임 |
|---|---|
| `Weapons/Data/ValorWeaponDataAsset.h/.cpp` | 무기 데이터(발로란트 용어 그대로). 구조체 기본값 = 밴달 |
| `Weapons/ValorWeaponTypes.h/.cpp` | 사격 공용 타입(자세, 스프레이 상태, 발사 요청 RPC 구조체 + 8바이트 압축) |
| `Weapons/ValorSpraySimulation.h/.cpp` | **결정적 반동/탄퍼짐 계산(순수 함수)**. 서버와 클라가 같은 코드를 실행 |
| `Weapons/ValorWeaponBase.h/.cpp` | 탄약(서버 권위), 스프레이 상태 보관, 시드 복제(소유자만), 발사 연출 |
| `Components/ValorCombatComponent.h/.cpp` | 로컬 발사 루프 → 예측 → 서버 RPC → 검증 → GAS 어빌리티 → 권위 판정 |
| `Components/ValorCameraComponent.h/.cpp` | 카메라 반동 = ADS 패턴 추종(매 프레임 스프레이 상태에서 계산) + 매 발 카메라 킥, 로컬만 Tick |
| `Components/ValorCameraKickSpring.h` | 카메라 킥용 임계 감쇠 스프링(해석해 적분, 프레임레이트 무관) |
| `UI/ValorHUD.h/.cpp` | 발로란트식 크로스헤어 + Firing/Movement Error 표시 + 반동 디버그 |
| `Tests/ValorSpraySimulationTests.cpp` | 자동화 테스트 5개(결정성, 패턴, 회복, 탭 효율, 자세 배율) |

### 사격 네트워크 흐름
```
[소유 클라]                                   [서버]
입력 유지 → 발사 루프(연사 속도 타이머)
  요청 = {발사 시각, 조준 방향} (8바이트)
  ├─ 예측: SimulateShot → 트레이서/탄흔/
  │        ADS 카메라/크로스헤어 즉시 갱신
  └─ ServerFireShot (Reliable) ───────────►  검증: 시각 범위, 연사 간격, 실수신 속도(토큰 버킷),
                                             탄약, 조준 정합성
                                             → GA_WeaponFire 활성화(GAS)
                                             → 서버 스프레이 상태로 반동/탄퍼짐 재계산
                                             → 발사 시각으로 되감아 히트스캔 → 피해
                     ◄──── 탄약·서버 발사 수 복제(소유자만) ── MulticastSimulateFire(다른 클라 연출)
```
- 클라가 계산한 반동/탄퍼짐/피격 결과는 서버로 보내지 않는다(입력만 전송).
- 예측과 서버가 어긋나면(발사 거부 등) 다음 스프레이 시작 시 난수 번호를 서버 값으로 맞춰 자동 복구된다.
- 트레이드오프: 클라가 반동 시드를 알아서 이론상 "탄퍼짐 예측 핵"이 가능하다. 발로란트도 예측 정확도를 택했고 이는 안티치트 영역이다.

## 4. 언리얼 에디터에서 할 일

**필수**
1. **에디터를 완전히 닫았다가 다시 연다.** 새 클래스(`AValorHUD`)와 구조체 레이아웃 변경은 Live Coding으로 반영되지 않는다. (코드는 이미 커맨드라인으로 빌드해 두었다.)
2. `Content/Valor/Gun/BP_PrototypeRifle` → Class Defaults → **Weapon Data Asset = `DA_Vandal`** 지정 후 저장.
   - 현재는 비어 있어서 C++ 기본값(밴달)으로는 동작하지만, `DA_Vandal`에 들어 있는 **발사 몽타주가 재생되지 않는다.**
3. `DA_Vandal`을 열어 새 항목(Hip Fire / Alt Fire / Recoil Profile / Movement Accuracy / Effects)이 밴달 값으로 채워져 있는지 확인하고 **한 번 저장**한다.
4. `BP_ValorGameMode` → HUD Class가 `ValorHUD`인지 확인한다(C++ 기본값을 상속하므로 보통 손댈 필요 없음).

**권장(발로란트 느낌을 완성하는 연출)**
5. 트레이서 나이아가라 시스템(`NS_Tracer`)을 만든다: Beam 렌더러 + **User Parameter `BeamEnd`(Vector)**를 빔 끝점에 연결 → `DA_Vandal` → Effects → Tracer FX에 지정.
6. 탄흔 데칼 머티리얼(Material Domain = Deferred Decal)을 만들어 Effects → Impact Decal Material에 지정. 벽에 스프레이 모양이 남는 것이 발로란트 반동 학습의 핵심 피드백이다.
7. 총구 화염(Muzzle Flash FX), 탄착 이펙트(Impact FX), 발사 사운드(Fire Sound) 지정.
   - 총구 소켓 이름은 `Muzzle`(Araxys 메시의 본 이름)이다. 다른 메시를 쓰면 Effects → Muzzle Socket Name을 바꾼다.
8. 에셋을 지정하기 전까지는 개발 빌드에서 디버그 선(트레이서)과 노란 점(탄흔, 4초 유지)으로 대신 표시된다(`Valor.Debug.DrawShots 0`으로 끔).

## 5. 확인 방법
- 벽 앞에서 **마우스를 움직이지 말고 힙파이어로 연사**: 화면은 매 발 튀며 떨리고(카메라 킥), 노란 탄흔은 첫 발이 크로스헤어 위치에 맞은 뒤 위로 수직 상승 → 6발 이후 좌우 흔들림, 크로스헤어 선은 탄퍼짐만큼 벌어짐.
- 비교용: 콘솔 `Valor.Debug.HipCameraFollow 1`이면 예전처럼 화면이 패턴을 끝까지 따라 올라가고, `-1`(기본)이면 데이터 자산 값(0 = 발로란트)으로 돌아간다.
- **우클릭(ADS) 후 연사**: 화면(조준점)이 반동을 따라 올라가고, 사격을 멈추면 약 0.13초 뒤부터 0.25초에 걸쳐 원위치.
- 콘솔 `Valor.Debug.Recoil 1`: 다음 탄의 반동 중심(빨간 점)과 탄퍼짐 원, 수치 표시.
- 자동화 테스트: Session Frontend → Automation → `Valor.Weapons.Spray` (또는 콘솔 `Automation RunTests Valor.Weapons.Spray`).
- 멀티플레이 확인: PIE 인원 2명, Net Mode = Play As Client, 에디터 환경설정의 Network Emulation으로 지연을 주어도 사수 화면의 반동/트레이서가 즉시 반응해야 한다.

## 6. 튜닝 가이드 (`DA_Vandal`)
| 바꾸고 싶은 것 | 조정할 값 |
|---|---|
| 수직으로 얼마나 올라가나 | Recoil Profile → Vertical Recoil Curve (Y값) |
| 초반 몇 발이 곧게 올라가나 | Protected Bullet Count |
| 좌우 흔들림 크기 | Horizontal Recoil Curve, Max Horizontal Recoil |
| 좌우 전환 빈도/속도 | Yaw Switch Chance / Yaw Switch Time |
| 사격을 멈춘 뒤 리셋 시간 | Gun Recovery Time |
| 탭/버스트 유리함 | Tap Efficiency |
| 힙파이어에서도 화면이 패턴을 따라 올라가게 | Hip Fire → Camera Recoil Follow Ratio (발로란트는 0) |
| 매 발 화면이 튀는 세기/속도 | Hip Fire(Alt Fire) → Camera Kick → Pitch/Yaw/Roll Degrees, Peak Time Seconds |
| 반동 전체 크기(탄 패턴 + ADS 카메라) | Hip Fire(Alt Fire) → Recoil Multiplier |
| 탄퍼짐 크기 | Hip/Alt Fire → First Shot Error / Max Firing Error, Firing Error Curve |

## 출처
- VALORANT 패치노트 11.08(Protected bullet, Yaw switch time/chance): https://playvalorant.com/en-us/news/game-updates/valorant-patch-notes-11-08/
- VALORANT 패치노트 6.11(달리기 수직 반동 ×1.8, Error Power): https://playvalorant.com/en-us/news/game-updates/valorant-patch-notes-6-11/
- VALORANT 패치노트 9.10(걷기/달리기 오차): https://playvalorant.com/en-us/news/game-updates/valorant-patch-notes-9-10/
- VALORANT 패치노트 2.02(라이플 이동 오차): https://playvalorant.com/en-us/news/game-updates/valorant-patch-notes-2-02/
- VALORANT 패치노트 0.50(Gun Recovery Time, Tap Efficiency, Firing Error 커브 정의): https://playvalorant.com/en-us/news/game-updates/valorant-patch-notes-0-50/
- Riot 개발자 답변(하이브리드 반동, 앉기·ADS 곱연산): https://devtrackers.gg/valorant/p/0642c4d6-why-is-there-rifle-first-shot-deviation-and-rng-spray-patterns-not-a-rant-thread
- Riot 기술 블로그 "Peeking into VALORANT's Netcode": https://www.riotgames.com/en/news/peeking-valorants-netcode
- Epic 기술 블로그 "VALORANT's foundation is Unreal Engine": https://www.unrealengine.com/en-US/tech-blog/valorant-s-foundation-is-unreal-engine
- 밴달 스탯(valorant-api): https://valorant-api.com/v1/weapons/9c82e19d-4575-0200-1a81-3eacf00cf872
- 밴달 위키(탄퍼짐 표, ADS "Crosshair follows recoil"): https://valorant.fandom.com/wiki/Vandal
- 반동 크기 커뮤니티 측정값 참고(AimFinder 스프레이 트레이너): https://aimfinder.pro/valorant/spray-patterns
- 사격 시 화면 흔들림 불만 스레드(2021~2024): https://valorantforums.com/d/2210-massive-screenshake-while-shooting-makes-us-dizzy-in-valorant
- 힙파이어 크로스헤어가 스프레이를 따라가지 않는다는 스레드: https://www.reddit.com/r/ValorantCompetitive/comments/g94qrp/is_there_a_proper_reason_why_your_crosshair_does/
- 사격 시 화면 흔들림(반동과 별개) 스레드: https://www.reddit.com/r/VALORANT/comments/gc09e6/how_to_get_screen_to_not_shake_when_shooting/
- 카메라 반동이 스프레이 크기와 무관하게 일정하다는 스레드: https://www.reddit.com/r/VALORANT/comments/tnr0x6/is_there_a_way_to_increase_camera_shake_amount/

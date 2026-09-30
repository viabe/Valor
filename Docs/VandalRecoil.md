# 밴달 반동 시스템 (현재 발로란트 기준)

반동은 두 가지 근거로 만들었다.
- **구조 파라미터**(보호 탄, 좌우 전환, 회복, 자세 배율): Riot 공식 패치노트와 개발자 답변.
- **크기와 카메라 동작**: 실제 게임 영상을 프레임 단위로 측정한 값(3절).

## 1. 공식 자료로 확인한 사실

| 항목 | 값 / 동작 | 출처 |
|---|---|---|
| 반동 모델 | "하이브리드": 앞쪽 몇 발은 완전 결정적, 이후 의사난수 편차 | Riot 개발자 답변(Riot_Classick, 2020) |
| 앉기 / ADS | 둘 다 반동 감소, **서로 곱해짐** | 같은 답변 |
| 보호 탄 수 | 밴달 **6발**(PC, 4 → 6) | 패치 11.08 |
| 수평 방향 전환 확률 | 발마다 **10%**(6% → 10%) | 패치 11.08 |
| 수평 방향 전환 시간 | **0.6초**(0.37 → 0.6), "한쪽에서 반대쪽으로 넘어가는 속도" | 패치 11.08 |
| 수직 반동 | "Vertical (Pitch) recoil curve" + 총량(total recoil) | 패치 11.08, 6.11 |
| Gun Recovery Time | 밴달 **0.375초** | 패치 0.50 |
| Tap Efficiency | 밴달 **6**, 값이 클수록 회복 전 재사격 시 부정확도 누적이 느림 | 패치 0.50 |
| 앉기(정지) 반동 | 수평 **-15%** | 패치 0.50 |
| 달리며 쏠 때 수직 반동 | **×1.8** | 패치 6.11 |
| Error Power(중심 편향) | 정지 시 중앙 쏠림, 이동 중에는 "거의 균일" | 패치 6.11 |
| 이동 오차(라이플) | 걷기 3° / 달리기 6° / 앉아서 이동 0.8° / 공중 10° / 착지 7°(0.225초) / 데드존 27.5% | 9.10, 2.02, 위키, 1.09 |
| 발사 속도 | 힙 9.75 / ADS 8.775 발/초 | valorant-api |
| 첫 발 탄퍼짐 | 힙 0.25° / ADS 0.1575°, 최대 1.0° / 1.02°, 앉기 ×0.85 | valorant-api, 위키 |
| ADS | 1.25배 줌, **"Crosshair follows recoil"** | 위키 |
| 넷코드 | 클라가 자기 입력을 즉시 예측, 발사 시각을 서버로 보내면 서버가 그 시각으로 되감아 판정 | Riot 기술 블로그 |

## 2. 커뮤니티 설명이 틀렸던 부분

"힙파이어에서는 크로스헤어가 스프레이를 따라가지 않는다"는 커뮤니티 설명이 많아서, 1차 수정에서는 힙파이어 카메라 추종을 0으로 두었다. PIE에서 "카메라 반동이 아예 없다"는 문제가 나왔고, 실제 영상을 측정해 보니 **카메라는 반동의 약 절반을 따라 올라가고 탄은 그보다 더 위로 가는** 구조였다. 커뮤니티 설명은 "크로스헤어가 탄 위치를 그대로 보여 주지는 않는다"는 뜻이었다.

## 3. 영상 측정 (2026-09-29)

**방법**
- 밴달 무보정 힙파이어 연사 유튜브 영상 2개를 브라우저에서 프레임 단위로 멈춰 캔버스로 추출했다.
  - `VALORANT: Vandal Recoil Pattern`(1080p60)
  - `Valorant vs CS:GO spray`(720p)
- 카메라 회전량: 발사 직전 프레임 대비 배경의 이동량을 블록 매칭(SAD)으로 구했다.
- 탄 위치: 사격 후 카메라가 원위치로 돌아온 프레임에서 벽 탄흔 위치를 쟀다.
- 각도 변환: 발로란트 수평 FOV 103°(1920px 기준 초점거리 763.7px)로 환산했다.
- 두 영상 모두 사격 후 카메라가 정확히 원위치로 돌아와 마우스 보정이 없었음을 확인했다.

| 항목 | 1번 영상 | 2번 영상 | 적용 값 |
|---|---|---|---|
| 탄 수직 반동(최상단 띠) | 7.5~8.8° | 7.5~9.7° | 커브 최대 8.95° |
| 카메라 상승(유지 구간) | 4.2~5.1° | 약 4.9° | 힙 추종 비율 0.5 |
| 상승 완료 | 약 8발 | 약 7발 | 8발 7.85°, 9발부터 정체 |
| 발마다 카메라 톱니 | 약 0.8~0.9° 튀고 0.1초 안에 가라앉음 | 비슷함 | 킥 0.8°, 0.025초에 최고점 |
| 카메라 좌우 | +0.5 → −1.3 → +1.0° | −0.9 → +1.5 → −0.6° | 탄 진폭 최대 ±2.8° × 0.5 |
| 상단 탄흔 좌우 폭 | 약 4° | 약 5.5° | 진폭 ±2.4~2.8° |
| 사격 후 복귀 | 약 0.45초, 일정한 속도 | 빠름(영상 편집으로 끊김) | 각도 선형 회복 + 카메라 복귀 보간 20 |

**검증:** 새 모델을 1번 영상과 같은 발사 타이밍으로 돌린 결과,
- 연사 중 카메라 높이의 평균 오차는 약 0.3°였다.
- 유지 구간 평균은 실측 4.68° vs 모델 4.84°였다.

**주의:** 두 영상은 2020년 무렵 것으로 보인다(발사속도 9.25, 패치 1.07 이전). 이후 바뀐 좌우 전환 값(11.08)은 공식 최신값을 썼다.

### 이전 구현이 발로란트 같지 않았던 이유
1. **카메라 추종 비율이 틀렸다.**
   - 원래 구현: 카메라가 패턴을 1:1로 따라가 탄이 늘 크로스헤어에 맞았다.
   - 1차 수정: 추종을 0으로 바꿨다(카메라 반동 없음).
   - 실제 발로란트: 약 0.5.
2. **패턴 모양과 크기가 달랐다.**
   - 원래 구현: 수직 10.5°에, 상단 좌우 지그재그가 매번 똑같았다.
   - 1차 수정: 수직 3.8°로, 너무 작았다.
   - 실제 발로란트: 수직 약 8.5°이고, 상단 좌우는 매번 무작위이며 0.6초에 걸쳐 전환된다.
3. **크로스헤어 HUD가 없었다.** 기준점이 없어 탄이 조준점에서 벗어나는 것을 볼 수 없었다.
4. **탭/버스트 사이의 부분 회복(Tap Efficiency)이 없었다.**
5. **반동이 서버 멀티캐스트를 거쳐 적용됐다.** 그래서 원격 클라에서는 RTT만큼 늦었다.
6. **`DA_Vandal`이 무기 블루프린트에 연결돼 있지 않았다.** 지금은 연결됨.

## 4. 동작 모델

```
스프레이 진행도(발 단위 실수) ──► 수직 반동 커브 ──► Pitch (0 → 8발 7.85° → 최대 8.95°)
        │                    └─► 탄퍼짐 커브 ──► 첫 발 오차 ~ 최대 오차
        ├─ 풀오토 간격: +1발씩
        ├─ 회복: 마지막 발 후 (발사 간격 × 1.1)까지 유지 → Gun Recovery Time(0.375초)에 0
        │        반동 "각도"가 시간에 비례해 줄고, 진행도는 커브 역함수로 되찾는다
        │        (실측 카메라가 처음부터 일정한 속도로 내려왔기 때문)
        └─ 회복 중 재사격: 증분이 1 → 1/TapEfficiency로 감소 (탭/버스트가 유리)

수평: 보호 탄(6발) 이후 시드로 좌/우 선택 → 그쪽 진폭(커브, 최대 ±2.8°)을 목표로 이동
      발마다 10% 확률로 목표가 반대편으로 바뀜 → 한쪽 끝에서 반대쪽 끝까지 0.6초 속도로 넘어감

탄 방향 = 조준(컨트롤 회전) × 반동(로컬 Pitch/Yaw) × 탄퍼짐(반경 = 오차 × U^ErrorPower)
카메라   = 반동 × CameraRecoilFollowRatio (힙 0.5, ADS 1)  +  매 발 카메라 킥(임계 감쇠 스프링)
           └ 올라갈 때 보간 40, 돌아올 때 보간 20. 조준·서버 탄도에는 영향 없음(연출).
```

자동화 테스트와 오프라인 시뮬레이션으로 확인한 탭 동작:
- 0.3초 간격 탭: 진행도 약 0.2(거의 첫 발 정확도)
- 0.2초 간격: 약 2
- 0.15초 간격: 약 5
- 3발 버스트 → 0.2초 쉼 → 3발: 두 번째 버스트는 진행도 약 2부터 다시 시작

## 5. 코드 구조

| 파일 | 책임 |
|---|---|
| `Weapons/Data/ValorWeaponDataAsset.h/.cpp` | 무기 데이터(발로란트 용어 그대로). 구조체 기본값 = 밴달 |
| `Weapons/ValorWeaponTypes.h/.cpp` | 사격 공용 타입(자세, 스프레이 상태, 발사 요청 RPC 구조체 + 8바이트 압축) |
| `Weapons/ValorSpraySimulation.h/.cpp` | **결정적 반동/탄퍼짐 계산(순수 함수)**. 서버와 클라가 같은 코드를 실행 |
| `Weapons/ValorWeaponBase.h/.cpp` | 탄약(서버 권위), 스프레이 상태 보관, 시드 복제(소유자만), 발사 연출 |
| `Components/ValorCombatComponent.h/.cpp` | 로컬 발사 루프 → 예측 → 서버 RPC → 검증 → GAS 어빌리티 → 권위 판정 |
| `Components/ValorCameraComponent.h/.cpp` | 카메라 반동 = 패턴 추종(힙 0.5 / ADS 1) + 매 발 카메라 킥, 로컬만 Tick |
| `Components/ValorCameraKickSpring.h` | 카메라 킥용 임계 감쇠 스프링(해석해 적분, 프레임레이트 무관) |
| `UI/ValorHUD.h/.cpp` | 발로란트식 크로스헤어 + Firing/Movement Error 표시 + 반동 디버그 |
| `Tests/ValorSpraySimulationTests.cpp` | 자동화 테스트 7개(결정성, 패턴, 회복, 탭 효율, 자세 배율, 수평 반동, 카메라 킥) |

### 사격 네트워크 흐름
```
[소유 클라]                                   [서버]
입력 유지 → 발사 루프(연사 속도 타이머)
  요청 = {발사 시각, 조준 방향} (8바이트)
  ├─ 예측: SimulateShot → 트레이서/탄흔/
  │        카메라 반동/킥/크로스헤어 즉시 갱신
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

## 6. 언리얼 에디터에서 할 일

**완료**
- `BP_PrototypeRifle` → Weapon Data Asset = `DA_Vandal` 연결(발사 몽타주 재생).
- `BP_ValorGameMode`는 C++ 기본 HUD Class(`ValorHUD`)를 상속한다.

**권장(연출 완성)**
1. **트레이서:** 나이아가라 시스템(`NS_Tracer`)을 만든다.
   - Beam 렌더러 + **User Parameter `BeamEnd`(Vector)**를 빔 끝점에 연결한다.
   - `DA_Vandal` → Effects → Tracer FX에 지정한다.
2. **탄흔:** 데칼 머티리얼(Material Domain = Deferred Decal)을 만들어 Effects → Impact Decal Material에 지정한다.
3. **기타 연출:** 총구 화염(Muzzle Flash FX), 탄착 이펙트(Impact FX), 발사 사운드(Fire Sound)를 지정한다.
   - 총구 소켓 이름은 `Muzzle`(Araxys 메시의 본 이름)이다.
4. **임시 표시:** 에셋을 지정하기 전까지는 개발 빌드에서 디버그 선과 노란 점(4초 유지)으로 대신 표시된다.
   - `Valor.Debug.DrawShots 0`으로 끈다.

## 7. 확인 방법
- **힙파이어로 연사**(벽 앞, 마우스는 움직이지 않음):
  - 화면이 발마다 튀며 약 5°까지 따라 올라간다.
  - 탄흔은 크로스헤어보다 더 위(최상단 약 8.5°)까지 올라간다.
  - 6발 이후 좌우로 흔들린다.
  - 멈추면 약 0.4초에 걸쳐 원위치로 돌아온다.
- **우클릭(ADS) 후 연사:** 조준점이 반동을 끝까지 따라가 탄이 조준점 근처에 맺힌다.
- **추종 비율 비교:** 콘솔 `Valor.Debug.HipCameraFollow`
  - `0` = 화면 고정, `1` = 탄 위치까지 따라감
  - `-1` = 데이터 자산 값(0.5)
- **반동 디버그:** 콘솔 `Valor.Debug.Recoil 1`. 다음 탄의 실제 위치(빨간 점)와 탄퍼짐 원, 수치를 표시한다.
- **자동화 테스트:** Session Frontend → Automation → `Valor.`(또는 콘솔 `Automation RunTests Valor.`).
- **멀티플레이:** PIE 인원 2명, Net Mode = Play As Client로 확인한다.
  - Network Emulation으로 지연을 줘도 사수 화면의 반동/트레이서가 즉시 반응해야 한다.

## 8. 튜닝 가이드 (`DA_Vandal`)
| 바꾸고 싶은 것 | 조정할 값 |
|---|---|
| 수직으로 얼마나 올라가나 | Recoil Profile → Vertical Recoil Curve (Y값) |
| 초반 몇 발이 곧게 올라가나 | Protected Bullet Count |
| 좌우 흔들림 폭 | Horizontal Recoil Amplitude Curve, Max Horizontal Recoil |
| 좌우 전환 빈도/속도 | Yaw Switch Chance / Yaw Switch Time |
| 사격을 멈춘 뒤 리셋 시간 | Gun Recovery Time |
| 탭/버스트 유리함 | Tap Efficiency |
| 화면이 패턴을 얼마나 따라가나 | Hip Fire / Alt Fire → Camera Recoil Follow Ratio (힙 0.5, ADS 1) |
| 매 발 화면이 튀는 세기/속도 | Hip Fire / Alt Fire → Camera Kick → Pitch/Yaw/Roll Degrees, Peak Time Seconds |
| 반동 전체 크기(탄 패턴 + 카메라) | Hip Fire / Alt Fire → Recoil Multiplier |
| 탄퍼짐 크기 | Hip/Alt Fire → First Shot Error / Max Firing Error, Firing Error Curve |
| 카메라 복귀 부드러움 | 캐릭터 → Camera Logic Component → Recoil Camera Return Interp Speed |

**추정치(공개 수치 없음):**
- 탄퍼짐이 최대에 도달하는 발 수(8발)
- ADS 반동 배율(0.9)
- 정지 Error Power(1.0)
- ADS 카메라 킥(0.35°)

ADS 카메라 동작은 영상으로 측정하지 않았다.

## 출처
- 측정 영상 1 — VALORANT: Vandal Recoil Pattern: https://www.youtube.com/watch?v=XNsMpt2dMIs
- 측정 영상 2 — Valorant vs CS:GO spray (Vandal & AK-47): https://www.youtube.com/watch?v=M2cR84PIUTY
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
- 사격 시 화면 흔들림 스레드: https://valorantforums.com/d/2210-massive-screenshake-while-shooting-makes-us-dizzy-in-valorant

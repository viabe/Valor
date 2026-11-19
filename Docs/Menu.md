# 메인 메뉴·서버 참가(6-5, 데디 서버 기준)

이 프로젝트는 데디 서버 기준이다(`ValorServer.Target.cs`, PIE `Play As Client`). 게임(클라)을 켜면 메인 메뉴가 나오고, 서버 주소로 참가한다. 서버는 따로 띄운다.

## 1. 메인 메뉴

| 칸 | 내용 |
|---|---|
| NAME | 점수판·킬피드에 보이는 이름. 영문·숫자·`_`·`-`, 16자. 처음 켜면 `Player####`를 만든다(컴퓨터 계정 이름은 쓰지 않는다) |
| JOIN A SERVER | 서버 주소(`127.0.0.1`, `192.168.0.5:7777`)를 넣고 `JOIN` 또는 Enter. `Ctrl+V` 붙여넣기. 마지막 주소를 기억한다(처음은 127.0.0.1) |
| LOCAL SERVER (DEV) | 개발용. 모드를 고르고 `START LOCAL SERVER` → 이 PC에 데디 서버 프로세스를 띄우고 바로 붙는다. 떠 있으면 `STOP LOCAL SERVER` |
| SETTINGS | 6-4 설정 창(감도·조준 방식·조준선) |
| QUIT | 게임 끄기(띄워 둔 로컬 서버도 같이 꺼진다) |

- 입력 칸은 클릭해서 고른다. 빈 곳 클릭이나 Esc로 빠져나오고, Tab은 이름 칸과 주소 칸을 오간다.
- 자판은 영문 기준이다. 한글은 받지 않는다(기본 폰트에 없다).
- 접속하는 동안 `CONNECTING TO ...`와 `CANCEL`이 보인다. 아무도 없는 주소면 엔진 기본 시간(약 60초) 뒤 실패 문구가 뜬다.
- 실패하거나 끊기면 메뉴로 돌아와 빨간 칸에 이유를 보여 준다(`OK`로 지움).
  - 주소 오류: 예) `PORT MUST BE 1-65535`, `IP MUST BE FOUR NUMBERS 0-255`
  - 접속 실패: `COULD NOT CONNECT TO ...`
  - 서버가 꺼짐·끊김: `CONNECTION TO THE SERVER WAS LOST`
  - 버전 다름: `VERSION MISMATCH WITH THE SERVER`

## 2. 서버 띄우기

| 방법 | 명령 |
|---|---|
| 에디터 PIE | Net Mode `Play As Client`(에디터 안에서 데디 서버 + 클라). 지금까지처럼 바로 경기 |
| 개발용(쿠킹 없이) | `UnrealEditor.exe Valor.uproject "/Game/Maps/ThridPersonExampleMap?Lobby=1?MinPlayers=2" -server -log` |
| 메뉴에서 | `START LOCAL SERVER`(위 명령을 창 없이 띄움, 로그는 `Saved/Logs/LocalServer.log`, 최소 1명) |
| 패키지 | Server 대상(`ValorServer`)으로 패키징한 `ValorServer.exe "/Game/Maps/ThridPersonExampleMap?Lobby=1?MinPlayers=2"`. 클라 폴더에 같이 두면 메뉴의 로컬 서버 버튼이 그것을 띄운다 |

서버 URL 옵션:

| 옵션 | 뜻 |
|---|---|
| `?Lobby=1` | 대기실: 시간 없이 사람을 기다린다 |
| `?MinPlayers=2` | 이만큼 모이면 카운트다운(기본 2, 1~10) |
| `?LobbyCountdown=10` | 카운트다운 초(기본 10). 그사이 인원이 줄면 다시 기다린다 |
| `?game=DM` | 데스매치(바로 시작·부활) |
| 그 외 | `?BuyTime=` `?RoundTime=` `?SelectTime=` 등 기존 경기 옵션 |

- 대기실 화면: 위 가운데 `WAITING FOR PLAYERS` 아래 `LOBBY | 1 / 2 PLAYERS | WAITING FOR PLAYERS TO JOIN`. 다 모이면 시간이 카운트다운이 되고 `MATCH STARTS SOON`.
- 방장이 없으므로 시작은 서버 규칙이다. 개발 중에 바로 시작하려면 콘솔 `ValorSkipPhase`(개발 빌드).
- 대기실 옵션 없이 띄우면(PIE 포함) 예전처럼 첫 사람이 들어오고 3초 뒤 시작한다.
- 한 PC에서 서버는 7777 하나만 쓸 수 있다. 이미 서버(손으로 띄운 서버, 다른 클라 창의 로컬 서버)가 7777을 쓰고 있으면:
  - 새로 띄운 서버는 로그에 `포트 7777번을 열 수 없어 서버를 끝냅니다`를 남기고 꺼진다. 엔진 기본값은 7778로 조용히 옮기는 것인데, 그러면 127.0.0.1로 참가한 클라가 먼저 떠 있던 서버에 붙어 헷갈린다(`DedicatedServerEngine.ini`의 `MaxPortCountToTry=0`).
  - 메뉴의 `START LOCAL SERVER`는 띄우지 않고 `PORT 7777 IS IN USE ...`를 보여 준다.
  - 서버 둘을 같이 띄우려면 두 번째에 `-port=7778`을 주고 클라는 `127.0.0.1:7778`로 참가한다.

## 3. 나가기

- 게임 중 `Esc`/`F10` → 설정 창 오른쪽 위 `LEAVE MATCH`. 한 번 누르면 3초 동안 `CONFIRM LEAVE`로 바뀌고, 그때 다시 누르면 메뉴로 간다.
- 콘솔 `ValorMainMenu`도 같다(PIE에서 메뉴를 볼 때도 쓴다).
- 서버가 정상 종료하면(접속을 닫고 끔) 클라들은 바로 `CONNECTION TO THE SERVER WAS LOST`와 함께 메뉴로 돌아간다.
  - 서버 로그 창에서 `Ctrl+C` 한 번이 정상 종료다(엔진이 끄기 전에 접속을 닫는다).
- 서버 프로세스가 갑자기 죽으면 접속을 닫는 신호가 오지 않는다. 그동안 클라 화면은 마지막 상태로 멈춰 있다(다른 사람이 나가도 캐릭터가 남아 보인다).
  - 2초 동안 서버에서 아무것도 오지 않으면 화면 가운데에 `CONNECTION PROBLEM - NO RESPONSE FROM THE SERVER`와 메뉴로 돌아가기까지 남은 초가 뜬다.
  - 20초(`DefaultEngine.ini`의 `ConnectionTimeout`, 엔진 기본 60초) 뒤 `CONNECTION TIMED OUT`으로 메뉴에 돌아간다. 그 전에 F10 → LEAVE MATCH로 나가도 된다.
  - 서버도 같은 20초로 응답 없는 클라를 내보낸다.
  - 에디터 PIE는 엔진이 시간 초과를 끈다. 경고는 뜨지만 저절로 끊기지 않는다.
  - 크래시, 작업 관리자 강제 종료, 서버 로그 창의 X 닫기(Windows가 몇 초 안에 끝내라고 해서 엔진이 바로 끊는다), `Ctrl+C` 두 번, `-unattended`로 띄운 서버의 `Ctrl+C`가 여기에 해당한다.
  - 메뉴의 `STOP LOCAL SERVER`·게임 끄기도 로컬 서버를 강제로 끈다. 그 서버에 다른 PC가 붙어 있었다면 그쪽은 20초 뒤에 안다.

## 4. 다른 컴퓨터와 하기

- 같은 공유기: 서버 PC의 IP를 넣는다. 로컬 서버를 띄웠으면 메뉴에 `OTHERS JOIN WITH <IP>`가 보인다. 서버 PC의 Windows 방화벽이 처음 묻는 창은 허용해야 한다.
- 인터넷 너머: 서버 PC 공유기에서 UDP 7777을 포트 포워딩하고 공인 IP를 넣는다. 서버 목록·NAT 통과는 아직 없다.
- 서버와 클라가 같은 빌드여야 한다.

## 5. 구조

| 부분 | 파일 | 하는 일 |
|---|---|---|
| 메뉴 게임 모드 | `ValorMenuGameMode` | 캐릭터 없이 컨트롤러만, 시점은 첫 PlayerStart 위(경기 맵이 배경). 데디 서버가 실수로 메뉴 모드로 열리면 경기 맵 기본 모드로 다시 연다 |
| 접속 관리 | `Session/ValorSessionSubsystem` | 참가(`ClientTravel`), 취소, 메뉴로, 로컬 데디 서버 띄우기·끄기(프로세스), 엔진의 네트워크·이동 실패를 문구로. 게임 인스턴스 서브시스템이라 맵이 바뀌어도 남는다 |
| 규칙 | `Session/ValorSessionTypes` | 서버 URL, 로컬 서버 명령줄, 이름 거르기, 주소 검사, 같은 이름 번호(`Ace 2`), 키 → 글자 |
| 화면 | `UI/ValorHUDMainMenu.cpp` | 메뉴 그리기·클릭. 대기실 안내는 `ValorHUD.cpp`, LEAVE 버튼은 설정 창 |
| 입력·이름 | `ValorPlayerController` | 메뉴 입력 칸 키 처리, 이름을 서버에 알림(`ServerSetProfileName`) |
| 대기실 | `ValorMatchGameMode` | `?Lobby=1`: 최소 인원·카운트다운. `GameState::LobbyMinPlayers`로 클라에 알림 |

- 이름: 접속 URL의 `Name`은 온라인 서브시스템 별명(`viabe-...`)이 덮어써서 쓸 수 없다. 그래서 들어간 뒤 서버 RPC로 바꾼다. 서버가 다시 거르고, 같은 이름이 있으면 ` 2`를 붙인다.
- 저장: 이름과 마지막 주소는 설정과 같은 `GameUserSettings.ini`의 `[/Script/Valor.ValorUserSettings]`에 있다.

### 설정 파일

- `DefaultEngine.ini`
  - `GameDefaultMap`: `ValorEntry` → 경기 맵(`ThridPersonExampleMap`).
    - `ValorEntry`는 레벨 블루프린트가 켜지자마자 `OpenLevel 127.0.0.1`로 서버에 붙는 클라 입구였다.
    - 이제 메뉴가 그 역할을 한다(주소 기본값 127.0.0.1). `ValorEntry`는 쓰지 않으니 지워도 된다(에디터 작업).
  - `LocalMapOptions=?game=Menu`: 클라가 기본 맵을 열 때(켤 때, 끊겨서 엔진이 기본 맵으로 보낼 때) 메인 메뉴가 된다.
  - `+GameModeClassAliases=(Name="Menu", ...)`: `?game=Menu` 별칭.
- `DedicatedServerEngine.ini`(데디 서버에서만 읽는다): `LocalMapOptions=`를 비워 서버가 메뉴 모드로 열리지 않게 한다. 서버 기본 맵은 원래대로 `ServerDefaultMap`(경기 맵).
- 에디터 PIE와 도구는 맵을 직접 열어서 이 설정을 타지 않는다.

## 6. 확인 도구

| 도구 | 내용 |
|---|---|
| `Valor.Session.Rules` | 서버 URL, 로컬 서버 명령줄(에디터·패키지), 이름 거르기, 주소 검사(포트, IPv4 0~255, IPv6 거부), 같은 이름 번호, 키 → 글자 |
| `Valor.Tools.MenuFlow` | 맵 없이 켜서 메뉴. 실제 키로 이름 입력(Shift·`_`·공백 거름)·저장, 잘못된 포트 문구, 아무도 없는 포트 접속 중 → 취소, 설정 창 열고 Esc로 닫기, 7777을 도구가 먼저 잡은 채 START LOCAL SERVER → `PORT 7777 IS IN USE`(띄우지도 붙지도 않음), START LOCAL SERVER → 데디 서버 프로세스에 붙음(대기실 최소 1명, 내 이름) → 카운트다운 뒤 저절로 시작 → F10 → LEAVE MATCH 두 번 → 메뉴(오류 없음) → STOP LOCAL SERVER로 프로세스 종료. 캡처 `Saved/MenuFlow` |
| `Valor.Tools.DediNet.Server/ClientA/ClientB` | 데디 서버 + 클라 둘(메뉴에서 127.0.0.1로 참가). 1/2명이면 시간 없이 대기 → 2명이면 카운트다운 → 시작, 서버가 받은 이름, B 나가기(오류 없음) → 경기 중 다시 참가 → 서버가 A에게만 보내기를 멈춤(갑자기 죽은 서버 흉내) → A: 2초부터 연결 경고, 20초 뒤 `CONNECTION TIMED OUT`으로 메뉴(서버도 A를 시간 초과로 내보냄) → 서버 정상 종료(접속 닫기) → B: 바로 `CONNECTION TO THE SERVER WAS LOST`로 메뉴. 캡처 `Saved/DediNet`. 실행 스크립트: 서버 → 25초 뒤 A → 20초 뒤 B |

## 7. 나중에

- 게임플레이 네트워크 도구(MatchNet·TagNet·AimNet)는 아직 리슨 서버 + 클라로 돈다. 데디 서버 + 클라 둘로 옮겨야 한다.
- 디자인: 메뉴·대기실을 UMG로(지금은 캔버스, 영어 기본 폰트).
- 서버 목록(같은 망 찾기), 초대·친구, NAT 통과(Steam 등 온라인 서브시스템), 대기실 팀 바꾸기, 맵 고르기.

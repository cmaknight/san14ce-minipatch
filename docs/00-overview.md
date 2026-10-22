# 구현 총람

대상: 삼국지14 CE 한국어 Steam판 `SAN14CE_KO.exe` 1.0.1(PE timestamp 0x6A983C25). 주소는 모두 RVA(ImageBase 0x140000000 기준 상대 주소)다.

이 폴더는 **지금 구현된 것을 무엇을, 어디서, 어떻게 바꿨는지**와 **게임이 업데이트됐을 때 같은 지점을 다시 찾는 법**을 정리한다. 값은 `src/`·`patcher/`에서 옮겼다. 구현을 바꾸면 이 폴더도 같이 고친다.

## 0. 문서 지도

| 문서 | 내용 |
| --- | --- |
| 이 문서 §1~§2 | 전체 구조, 업데이트 때 바뀔 수 있는 값 전부 |
| 이 문서 §3 | **업데이트 대응 절차**(새 버전이 나오면 여기부터) |
| 이 문서 §4~§6 | DLL 골격, 공통 게임 구조, 패처 골격 |
| 이 문서 §7 | 미해결·불확실 목록 |
| [data-formats.md](data-formats.md) | 데이터 파일 형식: .s14 컨테이너, LWC 압축, fixdata 헤더 서명, DLC 시나리오 XOR, zp1 |
| [01-expert.md](01-expert.md) | 그룹 1 숙련 모드 보강: 설정 행 2개(부대능력 배율), 범위 표 사본(인접효과 2000, 무장경험 0), 전투력 배율, 세이브·슬롯·확인 화면 |
| [02-traits.md](02-traits.md) | 그룹 2 개성 조정: 개성 데이터(fixdata·시나리오·DLC) + 구심·악주 주위 저돌 무효(DLL) |
| [03-autosearch.md](03-autosearch.md) | 그룹 3 자동 탐색: 턴 종료 훅 + 자동탐색 화면 4번째 행 |
| [04-tooltip.md](04-tooltip.md) | 그룹 4 툴팁: 무장 목록 개성·전법 말풍선, 전법 효과량·범위 줄 |
| [05-formation.md](05-formation.md) | 그룹 5 진형: fixdata 진형 표 값 |
| [06-logs.md](06-logs.md) | 그룹 6 로그: 사망·이벤트 포로·내정 Lv 로그, 메인 로그 창 크기 조절 |
| [07-post.md](07-post.md) | 그룹 7 관직 임명 완화 |
| [08-traincost.md](08-traincost.md) | 그룹 8 훈련비 비례 차감 |
| [09-develop.md](09-develop.md) | 그룹 9 내정 AI 개선 |

그룹 문서는 모두 같은 순서다: 하는 일 → 구현 방식 → 패치 지점(RVA, 방식, 원래 값, 다시 찾는 근거) → 쓰는 게임 함수·구조 → **새 버전에서 다시 맞추는 순서** → 검증 → 함정·결정.

## 1. 전체 구조 한눈에

```
패처 (San14CE_MiniPatch.exe 또는 python -m patcher)
 ├─ 데이터 그룹: 0010_*/fixdataexce.s14, 0010_*/scedaexce*.s14, DLC/0010_*/scedaexce*.s14 를 원본에서 다시 만든다
 └─ DLL 그룹: dinput8.dll 설치 + san14patch.ini [groups] 작성
게임 실행 → 게임이 dinput8.dll(프록시)을 올림 → ini를 읽고 켠 기능만 게임 코드에 패치
```

| 위치 | 파일 | 쓰는 쪽 | 내용 |
| --- | --- | --- | --- |
| 게임 폴더 | `dinput8.dll` | 패처 | 프록시 DLL(`src\dll\build.cmd`로 빌드). DLL 그룹이 하나도 없으면 지운다 |
| 게임 폴더 | `san14patch.ini` | 패처 | `[groups]` 키=0/1. DLL은 시작할 때 한 번만 읽는다(바꾸면 게임 재시작) |
| 게임 폴더 | `_san14patch/backup/<상대 경로>` | 패처 | 원본 백업 |
| 게임 폴더 | `_san14patch/state.json` | 패처 | 적용 그룹 key, 파일별 원본·출력 해시, DLL 해시. schema 2 |
| `%LOCALAPPDATA%\san14patch\` | `settings.ini` | DLL | 세이브와 무관한 사용자 값: `[expert] slotN_self/other`(숙련 설정 슬롯), `[log] rows`(로그 창 행 수) |
| 〃 | `dll.log` | DLL | 시작 줄 + 실패만. 1 MB를 넘으면 시작할 때 비운다 |
| 〃 | `patcher.log` | 패처 GUI | GUI 로그 이어 쓰기. 2 MB를 넘으면 `.old`로 바꾼다 |

### 패치 그룹

목록 순서 = 표시 번호 = 적용 순서(`patcher/groups/__init__.py` GROUPS). 상태 파일에는 번호가 아니라 key가 남는다.

| 번호 | 이름 | key | ini 플래그 | 방식 | 소스 | 세이브 영향 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 숙련 모드 보강 | `expert` | `expert_scaling` | DLL | `feat_expert.c` | 배율은 새 게임 때 월드 +0x17AC 바이트 1·2(자신/타 %, 0 = 100%)에 기록. 기존 세이브는 100%. 슬롯 값은 settings.ini |
| 2 | 개성 조정 (새 게임 필요) | `traits` | `trait_balance` | 데이터 + DLL | `groups/traits.py`, `feat_jeodol.c` | 데이터(효과2·설명문)는 새 게임부터(세이브에 개성 사본). 저돌 무효(DLL)는 진행 중 세이브에도 적용. 쓰는 칸 없음 |
| 3 | 페이즈 종료시 자동 탐색 | `autosearch` | `auto_search` | DLL | `feat_autosearch.c`, `feat_autosearch_ui.c` | 켬/끔 = 월드 +0x17AC 바이트 0(0 끔, 1 켬). 조건은 게임 칸 +0x17B0/+0x17B4/+0x17B8 |
| 4 | 개성·전법 툴팁 개선 | `tooltip` | `trait_tooltip` | DLL | `feat_tooltip.c` | 없음 |
| 5 | 진형 밸런스 조정 | `formation` | (없음) | 데이터 | `groups/formation.py` | 없음. 기존 세이브에도 반영(출진 편성 화면에서 확인) |
| 6 | 로그 기능 보강 | `logs` | `log_plus` | DLL | `feat_logs.c`, `feat_logwnd.c` | 쓰는 칸 없음. 로그 창 행 수는 settings.ini |
| 7 | 관직 임명 완화 | `post` | `post_busy` | DLL | `feat_post.c` | 없음 |
| 8 | 훈련비 비례 차감 | `traincost` | `train_cost` | DLL | `feat_train.c` | 없음 |
| 9 | 내정 AI 개선 | `develop` | `even_develop` | DLL | `feat_develop.c` | 없음(지역 +0x72 중점을 게임 값으로 바꿈) |
| 100 | 정보 단계 | `fog` | `fog_of_war` | 미구현(`_Planned`) | — | GUI에 안 보이고 적용 시 거부. DLL은 플래그만 읽고 init이 없다 |

- 데이터 그룹 소스는 `patcher/groups/`, DLL 소스는 `src/dll/`. DLL 전용 그룹은 `_DllOnly`(대상 파일 없음).
- 세이브 칸 원칙: 원본이 새 게임 때 0으로 채우고 직렬화만 하는 칸을 쓴다. 0 = 기본값이라 패치 전 세이브·원본 게임 모두 안전(§5.3).

## 2. 버전 의존성 목록

업데이트 때 바뀔 수 있는 값 전부. 다시 맞추는 순서는 §3.

### 2.1 1.0.1 식별 값

- 실행 파일 리소스 FileVersion `1.0.1.0`, PE timestamp `0x6A983C25`. 버전은 **해시로 판별**한다.
- 모든 실행 파일에 SteamStub 3.1이 걸려 있고 ImageBase는 `0x140000000`이다. 파일에서는 `.text`만 암호화돼 있고(엔트로피 8.0) `.rdata`·`.data`·`.rsrc`는 평문이다(RTTI·문자열은 파일에서도 보인다). EP는 `.bind` 섹션에 있고, 실행하면 SteamStub이 `.text`를 복호화한 뒤 OEP로 넘어간다. 실행 파일 바이트를 고치는 방식은 쓰지 않는다.

| 파일 | 크기 | SHA-256 | EP RVA | OEP RVA |
| --- | --- | --- | --- | --- |
| SAN14CE.exe (JP) | 30,627,296 | `f4b8268eedc0aa5f21efa9597d2bb4d6045b185fabcea740fed4ad6f1a163030` | 0x29C9310 | 0xFFC678 |
| SAN14CE_KO.exe | 30,609,888 | `87ed4e46590e0939c9efcaef14fa0f892917604dfda86e0a4be319fccffa2d6a` | 0x29C5310 | 0xFFA2C8 |
| SAN14CE_SC.exe | 30,619,616 | `ceee6a94eb939b40c329b8ec5aecd938e9d3a51a6663e8e7d27a1498627afb88` | 0x29C8310 | 0xFFC568 |
| SAN14CE_TC.exe | 30,615,520 | `0751b4fed7a5783371b9541674c0587a6afd3140e60e706e32eaf35a9432f464` | 0x29C6310 | 0xFFB6B8 |

| 파일 | 크기 | SHA-256 |
| --- | --- | --- |
| 0010_JP/fixdataexce.s14 | 89,837 | `7810ddea9005b62e4b450bea5b8b98bdbc579016e66e854a7010d4b3b63f644c` |
| 0010_KO/fixdataexce.s14 | 81,527 | `d3fac398d257407dcd81b0620bfd23f0956c3382c8926a5bd7ee4913da105a25` |
| 0010_SC/fixdataexce.s14 | 89,340 | `e5467c718534597927b8e21d572ddd947f65961edb359c9dc5d2ebc492e1ebba` |
| 0010_TC/fixdataexce.s14 | 83,902 | `e5da4d26b194f1268278106b47228a1fb631f1e91d83c2c99605bab4a035e0a4` |

### 2.2 패처 쪽

| 항목 | 현재 값 | 정의 위치 | 다시 구하는 법 |
| --- | --- | --- | --- |
| 실행 파일 해시 4개 | §2.1 | `patcher/versions.py` VERSIONS['A']['files'] | 새 실행 파일의 SHA-256. 새 키(예 'B')로 추가 |
| fixdata 원본 해시 4개 | §2.1 | 같은 곳 | Steam 무결성 확인 직후 원본의 SHA-256. versions.py에 직접 적는다 |
| 시나리오 원본 해시 212개 | — | `patcher/known_files.json` 키 'A' | 그룹 2 대상 시나리오(`0010_{JP,KO,SC,TC}/scedaexce*.s14` 50개씩 + DLC 10·32·33)의 원본 SHA-256을 새 키 아래에 넣는다. versions.py가 json의 키마다 `VERSIONS[키]`를 찾으므로 **versions.py에 키를 먼저 추가**한다 |
| 지원 버전 표시 | `SUPPORTED = '1.0.1'` | versions.py | 새 제품 버전(VS_FIXEDFILEINFO). 오류 안내(낮음/높음/같음)가 이 값과 비교한다 |
| 패처 버전 | `PATCHER_VERSION = '1.2'` | versions.py | 배포할 때 올림. 창 제목과 zip 이름에 들어간다 |
| 실행 파일 이름 | SAN14CE·_KO·_SC·_TC.exe | versions.py `EXES` | 이름이 바뀔 때만 |
| fixdata 서명 상수 | 오프셋 463, 법 1071514531, 서명 지수 5, 검증 지수 35714969 | `patcher/s14.py` SIG_* | 새 원본에서 `signature_ok`가 참인지로 확인(아니면 진형·개성 그룹이 "헤더 서명을 확인할 수 없어" 거부). [data-formats.md](data-formats.md) §4 |
| DLC 표 | `DLC_SCENARIOS` 10·11·19·29·32·33 | s14.py | 실행 파일 문자열 `DLCFile::OpenToRead\0` 끝을 8바이트 정렬한 곳이 표. [data-formats.md](data-formats.md) §5.1 |
| 진형 레코드 패턴·편집값 | 봉시 `01 14 6E B4 00 2D 0D 08 12`, 학익 `01 10 1E 3C 00 12 0C 0D 1E`, 방어 칸 +8 | `groups/formation.py` BONGSI/HAKIK/EDITS | 패처는 패턴을 해제 데이터에서 유일 검색하므로 오프셋이 바뀌어도 된다. 원래 값이 다르면 PatchError |
| 진형 오프셋(참고) | 봉시 방어 KO·SC·TC 0x2D5BF, JP 0x2D581 / 학익 방어 KO·SC·TC 0x2D787, JP 0x2D749 | — | LWC 해제 데이터 기준. 패처는 쓰지 않는다 |
| 개성 표 상수 | 청크 (22, 356), 다음 청크 23, 레코드 fixdata 170/150 B, 시나리오 60 B, 설명 44~끝−24 | `groups/traits.py` CHUNK/COUNT/NEXT_CHUNK/FIX_STRIDES/SCEN_STRIDE/DESC_OFF/Layout | 청크 헤더 뒤 표 끝 다음 u32가 23인 곳 하나. 못 찾으면 PatchError. [02-traits.md](02-traits.md) |
| 개성 원래 값·설명 원문 | 저돌 (0,0,1,1), 투장 (26,1,55,1), 언어별 원문 | traits.py EFFECTS/DESCS | 다르면 PatchError로 멈춘다. 새 원문을 확인해 갱신 |
| Steam 앱 | 872410 | `patcher/steam.py` | 고정 |

### 2.3 DLL 쪽

| 항목 | 현재 값 | 정의 위치 | 다시 구하는 법 |
| --- | --- | --- | --- |
| 빌드 확인 | timestamp 0x6A983C25, SizeOfImage 0x29FF000 | `src/dll/common.h` GAME_TIMESTAMP/GAME_IMAGE_SIZE | 새 `SAN14CE_KO.exe` PE 헤더의 TimeDateStamp·SizeOfImage. DLL은 이 한 쌍만 지원한다(KO 실행 파일 기준) |
| 복호화 확인 바이트 | 0x21064E `BA 64 00 00 00` | `main.c` text_decrypted | `mov edx, 100`. 복호화된 `.text`에만 있는 고정 바이트다. 새 빌드에서 같은 명령을 다시 찾거나 다른 고정 바이트로 바꾼다 |
| ROOT | 0x277DF18 | common.h ROOT_PTR_RVA | `mov rax|rcx, [rip+ROOT]`로 ROOT를 읽는 함수 7곳(0x2341F0, 0x233C4C, 0x22DC80, 0x23401C, 0x233B20, 0x234270, 0x234500)이 모두 같은 주소를 가리킨다. 새 빌드에서 같은 함수를 찾아 rip 대상을 읽고, 그 주소를 읽는 다른 코드와 맞는지 확인 |
| 월드·세이브 칸 | 월드 = [ROOT+0x871E8], 칸 +0x17AC | common.h world_extra | 세이브 직렬화 0x337A90의 필드 나열, 새 게임 초기화 0x325B80의 0 채우기, 코드 전체에서 `+0x17AC` 변위 접근이 이 둘뿐인지(§5.3) |
| 기능별 RVA | §2.4 | 각 `src/dll/feat_*.c` 머리 enum과 배열 | 그룹 문서 "새 버전에서 다시 맞추는 순서" |

### 2.4 기능별 패치 지점 정의 위치

| 그룹 | 파일 | 정의 | 지점 |
| --- | --- | --- | --- |
| 2 | `feat_jeodol.c` | `SITES[]`, `ACTIVE_SITES[]`, enum | call 0x2B2DA0 4곳(0x6B50B, 0xA1D15, 0xA1DDF, 0x7FB203, 앞 `mov edx, 1` 확인), call 0x89E650 5곳, vtable CArmyInfoLayout 0x1398A58 슬롯 2 |
| 1 | `feat_expert.c` | `POWER_CALLS[]`, `POWER_JMP`, `LEA[]`, `S[]`, enum | call 0x2A8D40 22곳 + jmp 0xBF04B, `ja` 0x3EA403·0x3EFCE3, 범위 표 0x14005C0 lea 5곳, call 0x5B919E, vtable 0x141BB18 슬롯 53, call 0x335767·0x31AB46, 슬롯·확인 화면 call 8곳 |
| 3 | `feat_autosearch.c`, `feat_autosearch_ui.c` | enum | call 0x451100 @0x44D041(앞 `xor ecx, ecx` 확인), vtable 0x1447260 슬롯 0·1·2·5 |
| 4 | `feat_tooltip.c` | enum | vtable 0x13ACF30 슬롯 1·5·8, call 0x930D20 @0x8A2E18 |
| 6 | `feat_logs.c` | enum, `S[]`, `COMMERCE[]`/`FARM[]`/`BARRACKS[]` | je 0x295C61 → 스텁, call 0x295B03, 0x29640E/1D/2C/3B, 0x296172·0x2963D3, 0x3AB312, 세터 call 30곳 |
| 6 | `feat_logwnd.c` | enum, `ORIG_A`/`ORIG_B` | 0x66B484(5 B)·0x66B53E(11 B) → jmp 스텁, 0x66B6DA `76`→`EB`, call 0x66B2C0 3곳(0x66C90F, 0x66F7DC, 0x67018B), vtable 슬롯 0x142D1A0·0x142E320·0x13FE1F8 |
| 7 | `feat_post.c` | enum | call 0x232240 @0x72D385·0x72D9DB |
| 8 | `feat_train.c` | enum | call 0x266850 @0x300EBA·0x268F10·0x2DC763 |
| 9 | `feat_develop.c` | enum | call 0x257990 @0x426B14 |

각 파일 enum에는 지점 말고도 부르는 게임 함수(FN_*), 전역(.data/BSS), vtable(VT_*)이 들어 있다. 이것도 모두 버전 의존이다.

## 3. 업데이트 대응 절차

설치 폴더를 직접 고치지 말고 게임 폴더 사본으로 작업한다.

1. **바뀐 파일 찾기.** 새 게임 폴더(사본)에 `python -m patcher --game <폴더> status`를 돌리면 "게임 버전: 지원하지 않음"과 파일별 상태(original/external)가 나온다. 실행 파일·fixdata·시나리오의 SHA-256을 §2.1·`known_files.json`과 비교한다. 실행 파일 PE 헤더의 TimeDateStamp·SizeOfImage도 적어 둔다(7단계).
2. **패처 해시 표 갱신.** `patcher/versions.py`에 새 키(예 'B')와 실행 파일·fixdata 원본 해시를 넣고, `patcher/known_files.json`에 같은 키로 시나리오 원본 해시를 넣는다(§2.2). DLC 시나리오 번호가 바뀌었으면 실행 파일의 DLC 표를 다시 읽어 s14.py `DLC_SCENARIOS`를 고친다.
3. **데이터 그룹 확인.** 진형 레코드는 패턴, 개성 표는 청크 헤더로 찾으므로 보통 그대로 맞는다. 사본 폴더에 `python -m patcher --game <사본> apply -n 2 5`(미리보기)를 돌려 PatchError가 없는지, fixdata 서명이 맞는지 본다.
4. **코드 위치 다시 찾기.** 새 실행 파일의 복호화된 코드를 디스어셈블해 각 패치 지점의 명령 패턴(상대 주소를 가린 바이트)을 다시 찾는다.
   - 먼저 ROOT(§2.3)와 공통 구조(§5)를 맞춘다. 대부분의 기능이 여기에 기댄다.
   - vtable은 RTTI 클래스 이름으로 찾는다(`.rdata`는 평문). 상태·레이아웃·목록·행동 트리 노드는 이것으로 시작한다.
   - 함수·전역은 그것을 부르거나 읽는 call/jmp/lea/rip 참조로, 특정 명령은 명령 문자열(예 `cmp edx, 0x3c`, `[reg+0x17ac]`)로 찾는다.
   - 패턴은 코드 모양이 같을 때만 맞는다. 컴파일러가 다르게 만든 곳·구조 오프셋이 바뀐 곳은 손으로 다시 맞춘다. 구조 오프셋(ROOT+…, 빌더 자리 등)은 패턴으로 찾지 않고 따로 확인한다.
5. **DLL 상수 갱신.** common.h GAME_TIMESTAMP·GAME_IMAGE_SIZE(1단계 값), ROOT_PTR_RVA, main.c 복호화 확인 지점(0x21064E)과 확인 바이트(`BA 64 00 00 00`)를 고친다.
6. **기능별 지점 갱신.** 각 그룹 문서(01~09)의 "새 버전에서 다시 맞추는 순서"대로 feat_*.c의 enum·배열을 고친다. 호출 대상·vtable 원래 함수는 주소에서 계산되지만, 소스에 바이트로 직접 적은 원래 명령(로그 창 `ORIG_A`/`ORIG_B`, 관문 스텁의 `[rbp+…]` 변위·레지스터 등)은 새 코드에서 다시 확인한다. 원래 바이트 확인(`patch_apply`)이 마지막 안전장치다.
7. **빌드.** `src\dll\build.cmd`(MSVC x64, /W4 /WX). 배포본은 `tools\build_gui.cmd`(DLL도 다시 빌드).
8. **테스트.** `python -m unittest discover -s tests -t .`. 배포본은 `San14CE_MiniPatch.exe --selftest <게임 폴더 사본> <결과 파일> [그룹…]`(관리자 권한 요청 없이 하려면 `__COMPAT_LAYER=RunAsInvoker`). 결과 파일 끝이 `SELFTEST OK`여야 한다.
9. **게임 확인.** `%LOCALAPPDATA%\san14patch\dll.log`를 먼저 본다(아래). 그룹 문서의 "검증" 항목을 다시 확인한다.
10. **기록.** versions.py `SUPPORTED`·`PATCHER_VERSION`을 올리고, 이 폴더의 값(§2, 그룹 문서)을 새 값으로 고친다.

### dll.log로 깨진 곳 보기

새 빌드에서 dll.log는 다음 순서로 신호를 준다.

1. 상수를 고치기 전: `지원하지 않는 게임 빌드: timestamp … size …. 아무것도 바꾸지 않음` 한 줄. 빌드 확인이 복호화 확인보다 먼저라 기능 init까지 가지 않는다.
2. 빌드는 맞췄는데 복호화 확인 바이트가 옛 값이면: `.text가 아직 복호화되지 않음. 초기화 중단`.
3. 그다음부터는 기능 단위로 남는다. **`[묶음 이름] 원래 바이트 불일치 @295C61. 이 기능은 적용하지 않음`**(RVA는 `0x` 없이 대문자 6자리)이 가장 흔한 첫 신호다. 그 묶음의 지점 중 하나라도 원래 바이트가 다르면 그 묶음 전체가 빠지고 게임은 그대로 진행한다. 다른 줄: `vtable 슬롯 @… 원래 값 불일치`, `[묶음] 쓰기 실패 @… (앞의 n개는 이미 적용됨)`, `중계기 페이지를 이미지 근처에 잡지 못함`, 기능별 `… 연결 실패`, 문맥 확인 실패(`[자동 탐색] 턴 종료 지점 문맥 불일치`, `[저돌 무효] 지점 … 앞의 효과 ID 1 확인 실패`).

| 묶음 이름(로그) | 그룹 | 묶음 이름(로그) | 그룹 |
| --- | --- | --- | --- |
| 저돌 무효 / 저돌 무효 표시 | 2 | 사망 로그 / 이벤트 포로 로그 / 내정 Lv 로그 | 6 |
| 숙련 설정 행 / 숙련 새 게임 기록 / 숙련 기본 부대능력 배율 / 숙련 설정 슬롯·확인 화면 | 1 | 로그 창 | 6 |
| 자동 탐색 | 3 | 관직 임명 완화 | 7 |
| 전법 효과량·범위 표시 | 4 | 훈련비 비례 | 8 |
| 내정 균등 개발 | 9 | | |

- 불일치 RVA는 그 지점이 옮겨졌다는 뜻이다. 그 RVA의 옛 원래 바이트를 그룹 문서에서 찾아, 새 코드에서 같은 앵커로 지점을 다시 찾는다.
- vtable 불일치 줄은 기능 이름이 없다. 바로 뒤의 기능별 "연결 실패" 줄로 구분한다.

## 4. 프록시 DLL 골격

소스: `src/dll/main.c`(진입·초기화), `support.c`(로그·설정·패치 엔진), `common.h`(선언·공통 구조), `build.cmd`, `dinput8.def`.

### 4.1 로드 순서

1. 게임 import에 `DINPUT8.dll`이 있어 게임 폴더의 dinput8.dll이 먼저 올라온다. 진짜 DLL은 `GetSystemDirectoryW + "\dinput8.dll"`로 따로 올리고 export 6개(`DirectInput8Create`, `DllCanUnloadNow`, `DllGetClassObject`, `DllRegisterServer`, `DllUnregisterServer`, `GetdfDIJoystick`)를 그대로 넘긴다(`dinput8.def`).
2. `DllMain(PROCESS_ATTACH)`: `g_base = GetModuleHandleW(NULL)`, 대기 스레드 하나만 만든다. 이 시점에는 SteamStub이 `.text`를 아직 복호화하지 않았다.
3. 초기화 진입은 두 길 중 먼저 오는 쪽이다. 둘 다 `InitOnceExecuteOnce`로 한 번만 실행된다.
   - 게임의 첫 `DirectInput8Create` 호출(OEP 이후, 게임 스레드).
   - 대기 스레드: 100 ms마다 최대 60초 복호화 확인 → 확인되면 500 ms 더 기다린 뒤 초기화.
4. `init_once`: `log_open` → `config_load` → 시작 줄(켠 그룹) → 빌드 확인(timestamp·SizeOfImage) → 복호화 확인(0x21064E) → 기능 init.
5. 기능 init 순서(그룹 번호와 다름): 저돌(2) → 숙련(1) → 자동 탐색(3, 성공하면 UI) → 툴팁(4) → 관직(7) → 로그 줄·로그 창(6) → 훈련비(8) → 내정(9). 꺼진 그룹은 부르지 않는다. 실패해도 다음 기능으로 간다.

### 4.2 설정 읽기

- `config_load`: 게임 폴더(`GetModuleFileNameW(NULL)`의 폴더) `san14patch.ini` `[groups]`의 9개 키를 `GetPrivateProfileIntW`로 읽는다(없으면 0). `Config g_cfg`(common.h).
- `settings_get_int/set_int`: `%LOCALAPPDATA%\san14patch\settings.ini`. 세이브와 무관한 값만 둔다(§1 표).

### 4.3 패치 기본기 (support.c)

| 함수 | 하는 일 |
| --- | --- |
| `bytes_equal(rva, want, n)` | VirtualQuery로 커밋·접근 가능 확인 후 memcmp |
| `patch_apply(이름, Patch[], n)` | 먼저 **모든** 지점의 원래 바이트를 확인하고, 하나라도 다르면 아무것도 쓰지 않고 `[이름] 원래 바이트 불일치` 로그. 다 맞으면 차례로 쓴다(VirtualProtect → memcpy → FlushInstructionCache). 확인은 전부 아니면 전무, 쓰기 도중 실패는 앞의 지점이 남는다 |
| `Patch` | `{rva, len, orig[16], repl[16]}`. 한 지점 최대 16 B |
| `patch_call(p, site, expect_callee, target)` | `E8 rel32` 지점. 원래 바이트 = `E8` + 원래 대상의 rel32(원래 대상까지 확인). 새 바이트 = 중계기로 가는 `E8` |
| `relay_new(target)` | 이미지 근처 RWX 페이지 하나에 16 B씩 `FF 25 00000000` + 절대 주소. **페이지 한 장(최대 256개)**뿐이고 다 차면 NULL |
| `near_alloc(n, prot)` | 이미지 베이스 아래로 할당 단위씩 내려가며 base − 0x7FF00000까지 VirtualAlloc. rel32·rip 상대 거리 확보용(중계기, 스텁, 표 사본) |
| `vtable_swap(slot, expect, target)` | .rdata 슬롯 값이 `base+expect`인지 확인 후 `InterlockedCompareExchange64`. patch_apply 묶음과 별개라 실패하면 기능 쪽에서 `vtable_unswap`으로 되돌린다 |
| `vtable_unswap(slot, ours, orig)` | 현재 값이 우리 함수일 때만 원래 함수로 |

- 기능 안에서 쓰는 변형: `patch_branch`(feat_expert.c, `ja`/`jmp` rel32 대상을 중계기로), 조건 분기 대상 교체(feat_logs.c `0F 84` je), `lea` 변위 교체(feat_expert.c 범위 표 사본), 명령 묶음을 `E9 jmp` + NOP으로 덮고 near_alloc RWX 페이지의 스텁으로 보냄(feat_logwnd.c 행 수), 1바이트 교체(`76`→`EB`). 모두 원래 바이트를 Patch.orig에 넣어 patch_apply로 확인한다.
- 원래 함수는 RVA로 직접 부른다(`CALL(type, rva)` 매크로). 호출 지점만 바꾸므로 원래 함수 본체는 그대로다.
- 일부 기능은 지점 확인 전에 주변 문맥도 본다: 자동 탐색은 0x44D041 앞 `33 C9`(xor ecx, ecx), 저돌은 각 지점 앞 `BA 01 00 00 00`(mov edx, 1).
- 명령어 길이 해석·함수 입구 트램펄린(E9 입구 훅)은 쓰지 않는다. 실행 중 시그니처 스캔도 하지 않는다. 지점은 모두 고정 RVA다.

### 4.4 예외 처리

- 훅 본문은 MSVC `__try/__except(EXCEPTION_EXECUTE_HANDLER)`로 감싼다. 예외가 나면 원래 함수 결과를 돌려주거나(관직·훈련비 등) 그 턴·그 순만 건너뛰고(자동 탐색, 내정) 로그를 한 줄 남긴다.
- 자동 탐색은 예외 주소를 게임 RVA로 바꿔 남긴다(`예외 %08X @… (게임 RVA …)`).
- UI 생성·정리 중 예외도 같은 방식이다(4번째 행 생성·정리).

### 4.5 로그 정책

- 시작 줄: `san14patch DLL 시작: 숙련=… 개성=… 자동탐색=… 툴팁=… 안개=… 로그=… 관직=… 훈련비=… 내정=…`.
- 그 뒤에는 실패와 예외만 남긴다(§3 목록).
- 이어 쓰기, `CRITICAL_SECTION`으로 직렬화, 줄 머리에 로컬 시각. 1 MB를 넘으면 열 때 비운다.

### 4.6 빌드

- `src\dll\build.cmd`: VS 2022+(vcvars64) 자동 탐색, 없으면 환경 변수 `VS`. `cl /O2 /MT /W4 /WX /utf-8 /GS- /LD`, `/DEF:dinput8.def`, kernel32·user32. 결과는 `out\dll\dinput8.dll`.
- 소스 목록이 build.cmd에 직접 적혀 있다: main.c, support.c, feat_jeodol.c, feat_autosearch.c, feat_autosearch_ui.c, feat_tooltip.c, feat_expert.c, feat_post.c, feat_train.c, feat_logs.c, feat_logwnd.c, feat_develop.c. **새 파일을 만들면 여기에 추가**한다.
- `/MT`라 런타임 의존이 없다. 경고 하나도 빌드 실패다(/WX).

## 5. 공통 게임 구조

여러 기능이 같이 쓰는 게임 구조. 값은 `src/dll/*.c` enum과 같다.

### 5.1 ROOT와 배열

ROOT = `[0x277DF18]`(BSS. 시나리오·세이브를 불러오기 전엔 준비되지 않을 수 있다). 배열은 `ROOT + 오프셋 + id*8`의 포인터 표다.

| ROOT+ | 내용 | 번호 | 다시 찾는 근거 |
| --- | --- | --- | --- |
| 0x158 | 무장(CPersonData) | id 1..0x1770 | 무장 객체 +0x10 = id. feat_develop은 id > 0x1770이면 ROOT+0x73A50 칸을 읽는다 |
| 0xDD38 | 도시(CCityData) | 0..0x33 | 첫 객체 vtable 0x13A6690, stride 0x168 |
| 0xDF30 | 세력(CForceData) | 0..0x33 | vtable 0x13A6840, stride 0x268 |
| 0xE0D0 | 군단 | 0..0x33 | vtable 0x13A68B0, stride 0x30. 도시 vfunc+0x80(0x22BA30)이 이 표를 쓴다 |
| 0x6CAF0 | 지역 | 1..0x1F4 | vtable 0x13A6980, stride 0x98 |
| 0x7E838 | 부대(CArmyUnitData) | 1..500 | vtable 0x1342458, stride 0x200 |
| 0x7DD18 | 개성 | 0..0x163(356개, 256 이후는 커스텀 자리) | vtable 0x13A71F0, stride 0xE8, +0x10 이름 |
| 0x774D0 | 전법 | 0..0xC8 | vtable 0x13A6DB8, stride 0x90, +0x10 이름 |
| 0x784F8 | 관직 | 0..255 | +0x37 관작(fixdata +39) |
| 0xE270 | 지도 칸 | 칸 = y*220+x | 48400칸, 칸+0x14 소유 세력 마스터 ID |
| 0x871E8 | 월드(게임 진행 객체) | — | vtable 0x13B2658. 생성자 0x31AA10(CWorldData) |
| 0x79E90 | 로그 레코드 링 버퍼(2001칸) | — | 로그 기록기 0x934400이 쓴다 |
| 0x80 | 거점 목록 | — | 순 처리 0x302FA0이 순회 |
| 0xA0 | 관문 목록(공용 노드 풀) | — | 0x22CFD0이 따라간다 |
| 0xC0 | 세력 목록 | — | 명령서 재계산 0x257990이 순회 |

- 공용 목록 노드 풀: 준비 `[0x27D10E8]`, 노드 표 `[0x27D10F0][핸들]` = {데이터, 다음}, 개수 `[0x27D1120]`(feat_train·feat_autosearch).
- ROOT 다시 찾기: §2.3. 배열이 맞는지는 첫 객체 vtable, new[] 쿠키의 개수, 포인터가 stride 간격인지로 확인할 수 있다.
- 객체 유효 판정 `0x32F500(객체)`를 거의 모든 기능이 쓴다(FN_VALID).

### 5.2 구조 필드 (기능들이 쓰는 것)

| 구조 | 필드 |
| --- | --- |
| 무장 | +0x10 id(u16), +0xE0 군단 슬롯+1, +0xE2 소속 도시, +0xE4 위치(0~51 도시, 52~61 관문, 62~561 부대, 562~ 지역), +0xE6 신분(1 군주, 2 도독, 3 태수, 4 일반, 5 재야, 6 포로, 7 미등장, 8 미발견, 9 사망), +0xE7 관직, +0x118 개성 u16[9], +0x14C 주의, +0x15E 비트 0x40·+0x1CC(탐색 후보 표시), +0x170/+0x172/+0x174 성향(상·농·병), +0x184 개성 발동 비트, +0x1E8 겸임 수(추정, 임명 직후 갱신 안 됨) |
| 도시 | +0x2E 태수, +0x30 세력(군단 슬롯+1), +0x34 금, +0x44 사기, +0xCA 모병담당, +0xCC 훈련담당, +0xCF 모병 카운터, vfunc+0x80 군단, vfunc+0xB0 세력 id |
| 지역 | +0x35 소속 거점(도시 번호), +0x36 담당관, +0x3A 중심 칸, +0x3C/+0x3E/+0x40 상업/농업/병영 개발치(최대 1000), +0x54 치안, +0x56 인접 지역 u16×8, +0x72 중점(1 상, 2 농, 3 병) |
| 군단 | +0x10 세력 마스터 ID, +0x12 군단장, +0x14 명령서(u8), +0x1A 중시(3 상, 2 농, 4 병) |
| 세력 | vfunc+0x60 종류(46~50 이민족), +0x193 bit0(의미 미상) |
| 부대 | +0x10 종류(0 빈), +0x12 대장 id, +0x2A 칸, +0x40 적접근시(1 무시, 2 공격) |
| 개성 | +0x10 이름, +0xB4 lpb0, +0xB6 효과1, +0xB8 효과치1, +0xBA 효과2, +0xBC 효과치2, +0xBE 색, +0xBF 플래그 |
| 전법 | +0x10 이름, +0x64/+0x65 범위, +0x70..+0x78 덮어쓰기 목록 |
| 월드 | +0x34 년(u16), +0x36 월, +0x37 순, +0x3A 플레이어 세력 마스터 ID, +0x450 로그 날짜 인자, +0x165A 로그 링 버퍼 쓸 칸, +0x17AC 우리 칸, +0x17B0 실행무장, +0x17B4 중시, +0x17B8 귀환, +0x2214 시작 초기화 조건 |

- 세력 따라가기: 무장·도시·관문의 세력 칸 = 군단 슬롯+1 → 군단 +0x10 = 마스터 ID → 세력 슬롯 = 마스터 ID − 1. 함수로는 `0x22CA00(무장)`, 부대는 `0x22CC40(부대)`, 플레이어 여부 `0x2337B0(세력)`, 플레이어 직할 군단 `0x32E8D0(월드)`, 플레이어 조작 군단 `0x233960(군단)`.
- 지도: 칸 = y*220 + x, 거리 `0x2AA850(칸, 칸)`, 연계범위 `[0x1A48FD8]` = 3.

### 5.3 월드 +0x17AC 세이브 칸

- 4바이트. 게임은 새 게임 초기화 0x325B80(`0x325F81 mov qword [rbx+0x17AC], r14`, +0x17B0과 함께 0)과 세이브 직렬화 0x337A90(0x337FA5/0x337FB8)에서만 다루고 읽지 않는다.
- 바이트 배치(common.h enum): 0 `WX_AUTO_ON` 자동진행(그룹 3), 1 `WX_BASE_SELF`·2 `WX_BASE_OTHER` 숙련 자신/타 %(그룹 1, 0이면 100). 3번은 빈 칸.
- 바로 앞 +0x16AC~(256 B)는 따로 직렬화되는 구조체(0x314100)라 쓰지 않는다.
- 새 게임 초기화는 한 판에 여러 번, 두 경로(0x31AB46 월드 생성자, 0x335767)로 불린다. 값을 쓰려면 모든 호출처 뒤에서 매번 쓴다.
- 다시 찾기: 직렬화 함수의 필드 나열을 보고, 코드 전체에서 `[reg+0x17AC]` 접근이 위 두 곳뿐인지 확인한다. 자동탐색 조건 칸(+0x17B0..)도 같은 방법으로 `[reg+0x17B0]`를 찾는다.

### 5.4 순 끝 처리 상태 기계

| 지점 | 내용 | 다시 찾는 근거 |
| --- | --- | --- |
| 0x42DAB0 | 디스패처. 단계 79개. 동작 표 `0x1A61040[i]`, 조건 표 `0x1A612C0[i]` | CGameState 0x44CABF에서 호출 |
| 단계 7 → 0x27BFA0 / 단계 8 → 0x27C6E0 | 수명 사망(매월) / 병 사망. 둘 다 사망 처리 0x295AA0을 부른다. 수명 사망 규칙(경과 36순 이하 건너뜀 등)은 [06-logs.md](06-logs.md) | 동작 표 칸 0x1A61078 = 0x427950(단계 7) |
| 단계 39 = 0x42F4B0 | → 순 처리 0x302FA0: 거점 목록(ROOT+0x80)마다 수입·모병 0x2BBCB0(월초) → 개발 0x295160 → 개발 로그 0x27A660 → 훈련 0x300E50 → (월초) 계절 0x302980 등 | 동작 표 단계 39 |
| 단계 55 = 0x426B10 | → 명령서 재계산 0x257990(호출 0x426B14). 세력 목록 ROOT+0xC0, 상한 `[0x1A4A234]` = 200. 다른 호출처 0x44A9DD(시작·불러오기) | 동작 표 단계 55 |
| 0x44D041 | 플레이어 턴 종료 `call 0x451100`(앞 `xor ecx, ecx`, 턴 진행 직전) | CGameState 쪽 턴 진행 코드 |

- 여기서 바꾼 중점은 다음 순 개발(단계 39)부터 반영된다. AI·위임 군단이 몇 단계에서 행동하는지는 찾지 못했다.

### 5.5 로그 기록기와 메시지

- **로그 기록기 0x934400**(13인자, this = CLogManager `0x16EFC0()`). 레코드: +0x86 종류(0~6), +0x12 군단, +0x84 위치·칸, +0x10 날짜(인자6 = 월드 +0x450), +0x88 무장 id(없으면 0x1394), 짧은 문구(0x241780)·긴 문구(0x2417A0, 없으면 `[0x1A47B98]` = L""). 짧은 문구가 있으면 메인 로그에도 올린다(0x43A630 → 0x668BE0). 종류 2·4·6은 플레이어 세력 항목만 보인다. 세력 필터가 없다. `[0x27842A0]+0x1B0 != 0`(이벤트 중)이면 아무것도 쓰지 않는다.
  - 다시 찾기: 호출처가 많다. 포획 0x2E10B2·개발 로그 0x27A660·사망 0x296172/0x2963D3이 부르는 함수.
- **메시지 빌더 전역 0x1A23440**: 초기화 `0x30A2F0(빌더)` → 자리 지정 `0x30DBB0(빌더, 무장id, 슬롯)`/`0x30D910`(세력)/`0x30D9D0(빌더, 지역, 0)` → `vfunc+8(메시지 번호)` → `0x196B50(buf, 0x1000, L"%s", text)`. 자리 칸: 무장 +0x1CD8, 세력 +0x1D00, 장소 +0x1DC8(각 int[10]). 다른 코드가 쓰는 중일 수 있어 우리가 바꾼 칸은 되돌린다(feat_logs).
- 메시지 번호 = 섹션 × 10000 + 순번. `0x30CF80(번호)`도 같은 번호. 번호와 문구의 대응은 [data-formats.md](data-formats.md) §6.1.

### 5.6 UI 기본

| 대상 | 값 | 다시 찾는 근거 |
| --- | --- | --- |
| 상태 관리자 | 0x1B620B0, `0x110E0()` | 상태 팩토리 첫 인자 |
| 상태 스택 | 개수 `[0x1B620C0]`(u32), 배열 `[0x1B620D0]`, 전환 중 `[0x1B620E0]` ≠ 0. 맨 위 = 지금 화면 | 맨 위 상태의 vtable을 RTTI로 비교(예 CUserStrategyState 0x13D63A0) |
| UI 매니저 | 0x1B62110: +0x170 마우스 아래 위젯, +0x18C ≠ 0이면 무효 | 마우스 아래 위젯 함수 0x86B3B0이 같은 계산 |
| 입력 싱글턴 | `0x11290()`: +0x34 x, +0x38 y(캔버스 정수 좌표), +0x4C 휠 | 리스트 공용 입력 처리기 0x8712D0이 쓴다 |
| 위젯 | +8 부모, +0x20/+0x24 부모 기준 위치(int), +0x28/+0x2C 크기, +0x40 플래그(`&3 == 3` 보임, 8 열기 준비, 0x80 라디오 선택), vfunc 7(+0x38) 열기. 절대 좌표 = 부모 체인의 +0x20/+0x24 합 | 위젯 판정 0x86EB40과 같은 계산 |
| 레이아웃 | CUILayout 0x178 B, vtable 0x1342530, 리소스 = +0x130(+8 노드 배열, +0x10 노드 수). 생성자 0x864C50, 리소스 생성자 0x92AB70, 로드 vfunc +0x178, 열기 준비 0x873930, 노드 id → 위젯 0x92CEE0, 보이기 0x92F810 | RTTI CUILayout, 상태 열기 함수의 레이아웃 이름 문자열 |
| 화면 애니메이션 그룹 | `0x570440(상태, 칸, 레이아웃)`, 상태 +0x60 | 2단계 열기 함수 0x724B80의 호출 순서 |
| 하단 안내줄 | 관리자 `0x84EC80([푸터+0x180])`, 등록 `0x89B1C0(관리자, 위젯, 문구, 0)` | 열기 함수의 푸터 +0x180 → 0x84EC80 → 0x89B1C0 묶음 |
| 게임 할당자 | `0x3F6BC0(크기)` 게임 new(레이아웃·컨트롤러·말풍선·목록 항목), `0x3F6E80` 할당 / `0x3F6E90` 해제(배열·표), memset `0x101B9B0` | 0x466220(탭 채우기)이 0x3F6E80/0x3F6E90을 쓴다 |

- 화면 = 상태 객체이고 슬롯 배치는 클래스마다 다시 확인한다. 소멸자에서 레이아웃을 닫지 않는 클래스가 있고, 열기 슬롯은 한 번만 불린다.
- 업데이트 때 UI 쪽 앵커: RTTI 클래스 이름, 레이아웃 이름 문자열(`06_07_01_jinji_search_cn`, `02_03_03_setup_confirm_new2` 등), 특징 명령(`cmp edx, 0x3c; ja` 등). 그룹별 앵커는 각 그룹 문서에 있다.

## 6. 패처 골격

코드 `patcher/`(Python 3 표준 라이브러리만). CLI는 저장소 루트에서 `python -m patcher [--game <폴더>|SAN14_DIR] {groups|status|apply [-n] [--data-only] <번호…>|restore}`. `--trust-current`는 해시를 모르는 파일도 원본으로 보는 개발용 옵션(업데이트 직후 작업용).

### 6.1 적용 절차 (`Engine.apply`)

1. 그룹 번호 확인(미구현이면 거부) → 대상 폴더에서 실행된 `SAN14*.exe`가 있으면 거부(프로세스 이미지 경로로 판단, `running_games`).
2. `SAN14CE*.exe` 해시로 버전 판별(`versions.identify`: 있는 실행 파일이 모두 같은 버전 키에 맞아야 함). 모르면 제품 버전으로 안내를 나눠 거부(`version_advice`). DLL 그룹을 골랐는데 DLL 본체가 없으면 거부. 모두 쓰기 전이다.
3. 모든 구현 그룹의 대상 파일(선택 여부와 무관)의 원본을 정한다(`Engine.base`): 백업이 있고 현재가 기록된 원본·출력이면 백업 / 현재가 알려진 원본 해시면 현재가 새 원본(최초, Steam 무결성 복원, 업데이트 후) / 백업이 있고 모르는 내용이면 백업 / 둘 다 아니면 `verify_unknown` 내용 확인, 실패하면 `UnknownFile`.
4. 원본을 코덱(`S14`, [data-formats.md](data-formats.md))으로 풀어 선택 그룹을 id 순서로 적용하고 다시 만든다. 같은 파일을 여러 그룹이 고쳐도 한 번에 합성된다.
5. 모든 파일 계획이 성공한 뒤에만 쓴다: 백업 → 파일 → DLL(`dll.check`·`dll.sync`) → 상태. 쓰기는 임시 파일 + `os.replace`.

### 6.2 그 밖의 규칙

- **상태 파일** `_san14patch/state.json`: schema 2(key 기록). schema 1(옛 그룹 번호 기록)은 `LEGACY_IDS`로 바꿔 읽는다.
- **verify_unknown**: 해시를 모르는 시나리오(해시 표에 없는 DLC 11·19·29 등)는 Traits가 내용으로 판정한다(풀림, DLC면 실행 파일 DLC 표의 번호와 키, 개성 1·108 원래 값). 실패한 시나리오는 건너뛰고 나머지를 적용한다. fixdata처럼 시나리오가 아닌 파일은 거부.
- **DLL 설치**(`patcher/dll.py`): 우리가 설치하지 않은 같은 이름의 dinput8.dll(다른 프로그램이 설치한 것)이 있고 DLL 그룹을 켜려 하면 쓰기 전에 거부. 켠 DLL 플래그가 있으면 DLL을 쓰고 `san14patch.ini`를 `[groups]` + 모든 dll_flag 키=0/1(정렬)로 쓴다. 하나도 없으면 우리가 설치한 DLL과 ini를 지운다.
- **원복**: `apply([])`(GUI "원본 복구", CLI `restore`).
- **진단**: `patcher/diag.py`(읽기만, 약 0.2초) — 실행 파일별 크기·제품 버전·해시, 실행 중 프로세스, 프록시·후킹 파일, 대상 파일 상태 등. GUI 오류 창의 [로그 복사]에 들어간다.

### 6.3 GUI·빌드

- `patcher/gui.py`(tkinter). `core.check_game`(상태 줄)과 `Engine.apply`만 쓴다. 상태 줄은 폴더 없음 / 실행 파일 없음 / 지원하지 않는 버전 / 상태 파일 오류 / 게임 실행 중(2초마다 재검사) / 지원 버전. 제목의 대응 버전은 `versions.SUPPORTED`.
- 진입점 `tools/gui_main.py`. `--selftest <게임 폴더> <결과 파일> [그룹…]`은 창 없이 엔진으로 적용하고 결과 파일에 로그와 `SELFTEST OK`/`SELFTEST FAIL …`을 쓴다.
- 빌드 `tools\build_gui.cmd`: 준비 `python -m venv .venv-build` + `pip install pyinstaller`. DLL 다시 빌드 → PyInstaller `--onedir --windowed --uac-admin`, `known_files.json`과 `dinput8.dll`을 묶음 → `dist\San14CE_MiniPatch_v<PATCHER_VERSION>.zip`(최상위 폴더 `San14CE_MiniPatch_v<버전>\`).
- 묶인 실행 파일에서 DLL 경로는 `sys._MEIPASS\dinput8.dll`.

### 6.4 테스트

`python -m unittest discover -s tests -t .`. 실제 게임 파일이 필요한 테스트는 게임 폴더 사본(경로는 각 테스트 파일의 `GAME`)이 없으면 건너뛴다.

| 파일 | 내용 |
| --- | --- |
| `tests/test_engine.py` | 대상 목록, 진형 적용 바이트, 서명 갱신, 원복, 재적용 안정, 무결성 복원, 모르는 파일 거부, 백업 사용, 최소 수정, 미구현 그룹 거부, DLL 플래그(빌드 있을 때), 미리보기, 상태 파일, schema 1 변환 |
| `tests/test_lwc.py` | 압축·해제 왕복(작은·무작위·실제 파일), 역참조 한도, `repatch` 동일성·수정 |
| `tests/test_traits.py` | 대상, DLL 요구·설치·제거, 남의 DLL 거부, 효과2만 변경, DLC 포장 유지, 설명문, 원복, 그룹 전환, 모르는 DLC 내용 확인, 바뀐 시나리오 건너뜀, 모르는 fixdata 거부, 진단 |

## 7. 미해결·불확실 목록

근거가 약하거나 확인하지 못해 업데이트 때 다시 봐야 하는 것.

| 항목 | 내용 | 영향 |
| --- | --- | --- |
| 개성 ID 상한 | 게임은 `cmp eax, 0x163; jg`로 0~0x163(356개)을 본다. `feat_jeodol.c`·`feat_tooltip.c`는 `> 0x162`를 버려 ID 355(0x163)를 빼고 본다 | 355는 커스텀 자리라 원본 데이터에서는 영향 없음. 고칠 때는 두 파일을 같이 고친다 |
| 최대사기 함수 | 최대사기 계산으로 0x268C50(= 100 + 세력효과 0xA + `0x22C0F0(세력, 0x47)`)도 있는데 `feat_train.c`는 0x22EA10을 쓴다. 둘의 관계 미확인 | 훈련비 게임 확인은 통과. 업데이트 때 둘 다 확인 |
| 0x32C130 | 사망 처리 앞쪽 호출 0x295B03의 원래 대상. 역할은 확인하지 않았다 | 우리는 표식만 지우고 그대로 부른다 |
| 자동 탐색 중시 변환 | 월드 +0x17B4(1 군사추천, 2 능력)를 라디오 번호로 바꿀 때 소스는 `== 2 ? 1 : 0`(0x724B80과 같은 변환). 소스 주석 "능력 우선 / 군사추천 우선"의 순서가 모호하다 | 자동 탐색 결과는 게임 확인됨 |
| 내정 세터 호출처 | 30곳과 둘러싼 함수의 대응은 주소 근접으로 맞춘 추정([06-logs.md](06-logs.md)) | 업데이트 때는 세터의 E8 호출처를 다시 모으면 된다 |
| 근거가 소스뿐인 값 | 각 그룹 문서의 "근거 없음(소스만)" 칸. 대표: 행 위젯 오프셋([01](01-expert.md)), CArmyInfoLayout +0x1F0([02](02-traits.md)), 0x40190·0x22A9D0([03](03-autosearch.md)), 0xAE0370([04](04-tooltip.md)), 0x13FE1F8 슬롯 클래스·0x1EDA40·스크롤바 vfunc([06](06-logs.md)), 노드 풀 0x27D10E8/0x27D1120([08](08-traincost.md)) | 업데이트 때 실행 중인 게임에서 다시 확인해야 한다 |
| 게임 미확인 | 그룹 9 담당관 교체 처리, 이벤트 포로 로그, 훈련비 세부(금 300 미만, AI 도시), 첫 훈련 담당 임명 직후 지출 0 표시(보류) | 게임에서 확인할 것 |

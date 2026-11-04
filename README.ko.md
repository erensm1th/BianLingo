<p align="center"><img src="resources/branding/logo.png" width="140" alt="BianLingo"></p>

# BianLingo

[English](README.md) · [简体中文](README.zh-CN.md) · [繁體中文](README.zh-TW.md) · [日本語](README.ja.md) · **한국어**<br>
[Français](README.fr.md) · [Deutsch](README.de.md) · [Español](README.es.md) · [Português (Brasil)](README.pt-BR.md) · [Русский](README.ru.md)

**World of Warcraft 실시간 채팅 번역 도구.** 선택한 언어로 채팅을 읽고 작은 오버레이에서 답장을 번역할 수 있습니다.

[소스 빌드](docs/BUILDING.md) · [개인정보 안내](PRIVACY.md)

BianLingo는 무료 비상업 팬 프로젝트입니다. Blizzard Entertainment와 제휴 관계가 없으며, 해당 회사의 승인이나 보증을 받지 않았습니다.

## 주요 기능

- **로컬 또는 온라인 번역**: Bergamot, CTranslate2, Ollama를 사용하거나 API 주소·모델·키를 직접 설정합니다. OpenAI 호환, Anthropic, Gemini 요청 형식을 지원합니다.
- **게임 실행 전 모델 준비**: 대상 언어에 필요한 언어 쌍을 확인하고 다운로드합니다. 지원 언어는 엔진과 모델에 따라 달라집니다.
- **채팅 오버레이**: 창 이동·크기, 배경 투명도, 글자 크기, 테마와 사용자 지정 스킨을 설정합니다.
- **채팅 탭**: 탭 이름과 표시할 메시지 유형을 지정하거나 애드온에서 내보낸 게임 내 채팅 구성을 가져옵니다.
- **클릭으로 답장**: 왼쪽 클릭은 해당 채널의 답장을 준비합니다. 오른쪽 메뉴에서 귓속말, 수동 레벨 조회, 차단을 선택합니다. Enter로 번역하고 복사하며 게임에 붙여 넣고 보내는 작업은 직접 합니다.
- **번역 범위 제어**: 채널, 잡음, 대상 언어와 같은 문자 체계의 닉네임을 필터링합니다. 링크와 게임 용어를 보호하고 수신 번역과 출력 번역의 프롬프트를 따로 편집합니다.
- **수동 재전송**: 애드온은 기본적으로 최근 완전한 메시지 10개를 메모리에 보관하며 한도를 1–100개로 설정할 수 있습니다. 미니맵 버튼 오른쪽 클릭 또는 `/bl replay`로 다시 보냅니다. 중복 메시지는 다시 번역하지 않습니다.
- **픽셀 전송 및 OCR**: 작은 컬러 매트릭스와 이진 그리드를 선택할 수 있습니다. 유휴 시 데이터 영역을 숨기고 작은 상태 표식만 남길 수 있습니다. Windows에서는 지정한 채팅 영역을 OCR로 읽을 수도 있습니다.
- **선택적 로그**: 기본값은 꺼짐입니다. 활성화하면 수신 메시지와 제출한 출력 번역을 날짜별 UTF-8 로그에 추가합니다.
- **포터블 저장**: 설정, 로그, 다운로드 모델과 앱 관리 캐시는 `BianLingo.exe` 옆의 `UserData`에 저장됩니다.

## 번역 방식 비교

| 방식 | 기능 | 제한 | 사용 조건 |
|---|---|---|---|
| **온라인 API** | 프롬프트와 출력 스타일, 모델 다운로드 불필요 | 네트워크, 할당량, 사용료 및 제공업체의 데이터 정책 | 로컬 추론 없이 프롬프트로 번역 |
| **Ollama** | 로컬 LLM, 프롬프트와 스타일 지원 | 런타임·모델 다운로드, 하드웨어에 따른 메모리와 지연 | 프롬프트를 사용하는 로컬 처리 |
| **Bergamot** | 오프라인 CPU 번역, API 사용료 없음 | 언어 쌍 준비 필요, 프롬프트 미지원 | 지원 언어 쌍의 오프라인 번역 |
| **CTranslate2** | 오프라인 CPU INT8 번역, API 사용료 없음 | OPUS-MT 언어 쌍 준비 필요, 프롬프트 미지원 | OPUS-MT 언어 쌍의 오프라인 번역 |

Bergamot과 CTranslate2의 자동 수신 번역은 영어 원문을 가정합니다. 대상이 영어인 경우 한자 규칙으로 중국어 입력을 선택합니다. 언어 팩을 더 받아도 임의의 원문 언어를 자동 감지하지는 않습니다. 로컬 처리는 채팅을 번역 제공업체로 보내지 않습니다. 실제 지연과 결과는 모델, 언어 쌍, 메시지, 하드웨어·네트워크 조건에 따라 달라집니다.

### Benchmark · AMD Ryzen 7 9700X

| 번역 방식 | 호출 시간 중앙값 | 초당 1건 CPU | 로컬 작업 집합 |
|---|---:|---:|---:|
| Bergamot | 20.9 ms | 0.10% | 337.5 MiB |
| CTranslate2 · Ruy CPU INT8 | 67.2 ms | 1.36% | 157.3 MiB |
| DeepSeek · `deepseek-chat` | 708.2 ms | 0.01% | 23.4 MiB |

### API 시작 설정

다음은 일반 사용자 지정 연결의 예시이며 UI 언어나 번역 대상 언어를 변경하지 않습니다. DeepSeek Flash는 비추론 모드를 지원하며 Google은 Gemini 3.5 Flash-Lite의 사용 사례에 번역을 포함합니다. 공식 문서: [DeepSeek](https://api-docs.deepseek.com/), [Gemini 모델](https://aistudio.google.com/docs/models/gemini-3.5-flash-lite), [Gemini 번역·요금](https://aistudio.google.com/docs/pricing#gemini-3.5-flash-lite).

| 설정 | DeepSeek | Google Gemini |
|---|---|---|
| 기본 URL | `https://api.deepseek.com` | `https://generativelanguage.googleapis.com/v1beta/openai` |
| 모델 | `deepseek-flash` | `gemini-3.5-flash-lite` |
| API 키 | [DeepSeek Platform](https://platform.deepseek.com/) | [Google AI Studio](https://aistudio.google.com/apikey) |
| 추가 매개변수 | `{"thinking":{"type":"disabled"}}` | `{}` (모델 기본값) |

**API & usage → Add**에서 연결을 추가합니다. 형식은 **OpenAI Chat Completions**(`openai_chat`), 인증은 **Auto**, **Full URL**은 꺼짐으로 두고 본인의 API 키를 입력합니다. 고급 설정에서 표의 추가 매개변수를 입력하고 사용자 지정 헤더는 비워 두며 잔액 조회는 끈 상태로 저장한 뒤 연결을 테스트합니다. [연결 설정](docs/PROVIDERS.md)과 Google의 [OpenAI 호환 API](https://aistudio.google.com/docs/openai)를 참고하세요.

모델 제공 여부, 가격, 할당량, 데이터 처리는 제공업체가 정합니다: [DeepSeek 요금](https://api-docs.deepseek.com/quick_start/pricing/), [Gemini 요금·데이터 사용](https://aistudio.google.com/docs/pricing), [Gemini 제공 지역](https://aistudio.google.com/docs/available-regions). Gemini 무료·유료 등급은 제한과 데이터 사용 조건이 다릅니다.

## 작동 방식

1. **BianLingo 애드온**이 게임의 애드온 API로 채팅을 읽고 게임 화면 모서리에 인코딩된 픽셀을 잠시 표시합니다.
2. **데스크톱 앱**이 픽셀을 해독하고 선택한 연결로 번역한 뒤 오버레이에 표시합니다.

게임 메모리를 읽거나 코드를 주입하거나 채팅을 자동으로 보내지 않습니다. OCR은 직접 지정한 영역을 캡처합니다. Blizzard가 애드온에 채팅 본문을 숨기면 픽셀 전송으로 이를 복구할 수 없습니다. OCR은 별도의 입력 경로입니다.

## 요구 사항과 언어

| 플랫폼 | 상태 |
|---|---|
| Windows 10/11 x64 | 사용 가능. 창 OCR에는 Windows 10 1903 이상이 필요합니다. |
| macOS | 개발 중이며 향후 버전에서 지원할 예정입니다. 아직 macOS 빌드는 제공하지 않습니다. |

- BianLingo 애드온이 설치된 World of Warcraft. 창 모드 또는 테두리 없는 창 모드를 사용합니다.
- 로컬 모델 또는 직접 준비한 API 연결. 전체 패키지에는 Bergamot 영어 ↔ 중국어 간체 모델이 포함되며 다른 지원 언어 쌍은 별도로 준비할 수 있습니다.

UI는 영어, 중국어 간체, 일본어, 한국어, 프랑스어, 독일어, 스페인어, 브라질 포르투갈어, 러시아어를 지원합니다. **채팅 모양 → 인터페이스 언어**에서 선택하고 저장하면 바로 적용됩니다. 기본값은 **시스템 설정 따르기**이며, 지원하지 않는 시스템 언어는 영어로 표시됩니다. 중국어 로캘은 간체 UI를 사용합니다. UI 언어, README 언어, 번역 대상은 서로 독립적이며 번역 지원 범위는 엔진과 모델에 따라 다릅니다. 오류와 실행 알림은 중국어 UI에서는 중국어로, 그 밖의 UI에서는 영어로 표시됩니다.

애드온은 Classic Era 및 호환 Classic/Retail 클라이언트 지원 정보를 선언합니다. 채팅 접근 여부는 클라이언트와 상황에 따라 다릅니다. HDR, 모니터 간 혼합 DPI, 게임 버전별 동작은 해당 환경에서 확인해야 합니다.

## 빠른 시작

1. **애드온 설치.** 패키지의 `AddOns/BianLingo`를 `World of Warcraft/<client>/Interface/AddOns/`에 복사합니다. `<client>`는 `_retail_`, `_classic_era_`, `_forever_` 등입니다. `Interface/AddOns/BianLingo/BianLingo.toc`를 확인하고 게임에서 애드온을 활성화한 뒤 `/reload`를 실행합니다.
2. **실행 및 이동.** `BianLingo.exe`와 `data`를 같은 폴더에 두고 EXE를 실행합니다. **오른쪽 위 고양이 아이콘을 클릭하면 설정이 열리고, 아이콘을 드래그하면 창이 이동합니다.**
3. **번역 방식 선택.** API 설정에서 연결을 선택합니다. **Bergamot / CTranslate2:** 대상 언어를 선택하고 현재 언어 팩 준비 또는 언어 팩 관리로 미리 다운로드합니다. **Ollama:** 모델을 선택하고 다운로드합니다. API 키는 필요 없습니다. **온라인 API:** 연결을 선택하거나 추가하고 Base URL, 모델명, API 키를 입력한 뒤 저장하고 연결을 테스트합니다. 초기 설정은 위 표를 참고하세요.
4. **수신 번역 설정.** API 설정에서 채팅 번역 대상 언어를 지정하고 번역 범위에서 채널을 선택한 뒤 저장합니다. 로컬 번역에는 지원되는 언어 쌍이 준비되어 있어야 합니다.
5. **답장 번역.** 기본 단축키는 **· / 백틱(&#96;) 키(보통 Esc 아래)**입니다. 입력 후 **Enter**를 누르면 번역하여 복사합니다. 게임에는 직접 붙여 넣습니다. 출력 번역 설정에서 대상 언어와 스타일을 선택하고, 단축키 칸을 클릭해 새 조합을 누른 뒤 저장하면 단축키가 변경됩니다. 출력과 수신의 대상 언어는 별도입니다.
6. **게임 채팅 구성 동기화.** WoW에서 창과 채널을 구성한 뒤 EXE를 실행한 상태로 `/bl windows`를 입력하거나 `/bl settings`에서 채팅 창 동기화를 선택합니다. 수신이 완료되면 탭이 자동으로 **교체**되며 추가 가져오기 작업은 필요 없습니다. 동기화 중에는 애드온을 켜 두세요.


동일 패키지의 앱과 애드온을 함께 사용하세요. 중국어를 다른 언어로 번역하려면 게임 내 중국어 필터가 활성화되어 있을 때 `/bl cn off`로 해제합니다.

### Windows SmartScreen

서명되지 않은 이 버전에서 Windows가 “Windows의 PC 보호” 메시지를 표시할 수 있습니다. 이는 Microsoft Defender SmartScreen의 서명·다운로드 평판 확인입니다. 새 파일이나 서명되지 않은 파일은 인식되지 않을 수 있으며, 서명만으로 경고가 즉시 사라지지는 않습니다. Microsoft 공식 설명을 참고하세요. [Microsoft Learn](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)

## 문서

[번역 연결](docs/PROVIDERS.md) · [로컬 모델](docs/LOCAL_TRANSLATION.md) · [채팅 탭](docs/CHAT_TABS.md) · [메시지 동작·차단](docs/MESSAGE_ACTIONS_AND_BLOCKLIST.md) · [스킨](docs/SKINS.md) · [OCR](docs/OCR_FALLBACK.md) · [빌드](docs/BUILDING.md)

상세 가이드와 프로젝트 안내의 기본 언어는 영어입니다. 번역된 README는 상단 언어 링크에서 선택할 수 있습니다.

## 개인정보와 라이선스

로컬 번역은 사용자 PC에서 처리됩니다. 온라인 연결은 설정한 제공업체에 원문을 보내며 해당 업체의 약관과 요금이 적용됩니다. 로그에는 플레이어 이름과 개인 메시지가 포함될 수 있습니다. [개인정보 안내](PRIVACY.md)를 확인하세요.

프로젝트 코드는 [MIT](LICENSE) 라이선스입니다. 의존성, 모델, 용어에는 각각의 조건과 출처 표시가 유지됩니다. [제삼자 고지](THIRD_PARTY_NOTICES.md)를 참고하세요. World of Warcraft 및 Blizzard Entertainment 상표는 각 권리자에게 귀속됩니다.

공개 다운로드와 모든 기능은 후원 없이 이용할 수 있습니다. 프로젝트, 개인정보 또는 라이선스 관련 문의: [@erensm1th](https://github.com/erensm1th).

# 지속적 통합

`main`으로 향하는 PR, `main` 푸시, 수동 실행에서 검증한다. 작업 브랜치 푸시와 PR 이벤트로 같은 빌드를 중복 실행하지 않는다. 같은 PR에 새 커밋을 올리면 이전 실행을 취소한다.

## 검증 구성

| 워크플로 | 실행 단위 | 검사 |
| --- | --- | --- |
| LuxEngine CI | Windows / MSVC 2022 | Debug·Release 전체 빌드, 공개 헤더 독립 컴파일, headless 회귀, 씬 퍼징, SDK·에디터·플레이어 배포 |
| Linux and macOS CI | Linux·macOS 각각 Debug·Release | 엔진·에디터·샘플·도구·공개 헤더 빌드, headless 회귀, 설치 SDK로 MinimalGame 빌드 |
| Linux and macOS CI | Linux ASan·UBSan | 엔진의 메모리·정의되지 않은 동작 검사, 씬·계층·물리·리소스·런타임 파싱 회귀, 씬 변이 입력 10,000회 |
| Code Quality | C++ 형식 | PR 또는 푸시에서 변경한 자체 소스에 clang-format 21.1.8 적용 여부, 패치 공백 검사 |
| Code Quality | 워크플로 | actionlint 1.7.11로 Actions 표현식·잡·셸 문법 검사 |
| Code Quality | Windows·Linux·macOS | .NET 네이티브 호스트 팩 탐색의 CMake 회귀 |
| Code Quality | C# | .NET 10 ScriptCore와 이를 참조하는 게임 스크립트 빌드, 경고를 오류로 처리 |
| Code Quality | Windows 배포 계약 | 잘못된 구성·Debug CRT 거부, 재배치 중 사용자 데이터 보존, Forge·C# 배포 파일 누락 거부 |

총 13개 잡으로 구성했다. headless 테스트는 화면과 GPU가 필요 없는 테스트이며 `long`·`slow` 라벨은 제외한다. 실제 GPU 렌더링 검증은 이 구성에 포함하지 않는다. 호스팅 러너의 가상 디스플레이를 실제 GPU 검증 결과로 취급하지 않는다.

## 의존성과 재현성

The Forge는 `cmake/TheForge.cmake`에 기록한 리비전의 `Common_3`를 별도 체크아웃한다. .NET 10 SDK와 Python 3.12도 명시적으로 준비한다. Windows는 Qt 6.8.3, vcpkg 의존성, Box2D 2.4.1을 설치한다. 이전 Vulkan SDK·별도 DXC 설치는 Windows D3D12 빌드에서 제거했다.

Unix의 SDL3·Box2D·stb는 `tools/ci/SetupUnixDependencies.sh`의 고정 리비전에서 미리 빌드·설치하고 캐시한다. 시스템 라이브러리는 apt 또는 Homebrew로 설치한다. Box2D 3.x를 자동 선택하지 않는다. CI의 준비 단계에서만 내려받으며 프로젝트의 CMake 구성은 다운로드를 수행하지 않는다.

`LUX_ENABLE_SANITIZERS=ON`은 Linux GCC·Clang의 엔진 타깃을 계측한다. The Forge 외부 소스와 .NET CLR은 이 검사 대상에서 제외했다. 일반 빌드에서는 기본값 `OFF`를 유지한다.

## 실패 진단

테스트와 구성 로그, JUnit 결과를 성공·실패와 관계없이 아티팩트로 보관한다. Windows는 SDK·에디터·플레이어 ZIP도 검증 후 보관한다. 의존성 구성이나 빌드가 실패하면 뒤 단계를 성공으로 처리하지 않는다. 테스트가 하나도 등록되지 않은 경우에도 실패한다.

Windows 배포에는 `LuxForge.dll`, `DIRECT3D12/`, `gpu.data`, `gpu.cfg`, Forge의 벤더·컴파일러 DLL, `managed/`, `nethost.dll`이 필요하다. 필수 파일을 검증한 다음 SDK 재배치 및 독립 실행 환경을 검사한다.

로컬에서 CI 자체를 검사하려면 다음 명령을 사용한다.

```sh
actionlint
python3 -m unittest discover -s tools/ci/tests -v
python3 tools/ci/CheckFormatting.py --base origin/main
cmake -DTEST_ROOT=/tmp/lux-host-fixture -P cmake/tests/DotnetHostTest.cmake
```

PowerShell 배포 회귀는 `tools/Tests/TestPackagingIntegrity.ps1`, `TestSdkRelocationSafety.ps1`, `TestWindowsRuntimePackaging.ps1`에서 실행한다.

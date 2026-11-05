# LuxEngine

LuxEngine은 C++20과 Native Vulkan을 기반으로 개발 중인 2D·3D 게임 엔진입니다. SDL3는 창·입력·오디오·Vulkan 표면 연결에만 사용합니다. 개발과 빌드의 기준은 이 저장소 최상위 CMake 프로젝트입니다.

## 1. 개발 환경

- **운영체제**: Windows 11
- **IDE**: Visual Studio 2026 Community
- **컴파일러**: MSVC v145, C++20 지원 컴파일러
- **언어 표준**: C++20 (`cxx_std_20`)
- **빌드 시스템**: CMake 4.2 이상, MSBuild 또는 Ninja
- **형상 관리**: Git
- **실행 검증**: Vulkan 1.1 이상 드라이버 (Linux lavapipe에서 자동 테스트)
- **플랫폼 지원 목표**: Windows, Linux, macOS

Linux(Xvfb + Mesa lavapipe)에서 빌드·테스트를 검증했습니다. Windows MSVC와 macOS(MoltenVK, `VK_KHR_portability_*` 자동 사용)는 이번 변경에서 직접 빌드하지 못했습니다.

## 2. 라이브러리 아키텍처

| 구성 요소 | 용도 | 적용 상태 |
|---|---|---|
| SDL3 (3.2 이상) | 창, 입력, 오디오, 시스템 테마 감지, Vulkan 표면 생성 | 통합 |
| Vulkan SDK / 로더 (1.1 이상) | 모든 GPU 렌더링, 검증 레이어 | 통합 |
| Vulkan Memory Allocator | GPU 메모리 할당·예산 | 통합 (Vulkan SDK 포함) |
| stb_image | PNG·JPEG·BMP 등 이미지 디코딩 | 고정 커밋으로 통합 |
| DXC 또는 glslc | HLSL을 SPIR-V로 컴파일, spirv-val로 검증 | Vulkan SDK 또는 포터블 DXC |
| 자체 ECS·수학 모듈 | 엔티티, 컴포넌트, 벡터, 행렬 | 구현 |
| 자체 테스트 실행 파일 | 기능·오류 경로·GPU 검증 | CTest 연결 |

CMake는 의존성을 내려받지 않고 설치된 SDL3·box2d·Vulkan·VMA·stb_image를 찾습니다. VMA는 `VulkanMemoryAllocator` CMake 패키지 또는 Vulkan SDK의 `vk_mem_alloc.h`를 사용합니다. 셰이더 컴파일러는 Vulkan SDK의 dxc, `Tools/SetupShaderCompiler.ps1`의 포터블 dxc, glslc 순서로 찾습니다. Windows SDK의 dxc는 SPIR-V를 생성하지 못하므로 사용하지 않습니다.

### 설치된 의존성과 오프라인 빌드

SDL3를 별도 경로에 설치했다면 `-DSDL3_DIR=경로` 또는 `CMAKE_PREFIX_PATH`로 지정합니다. 빌드 스크립트에서도 `-SDL3Dir` 인자를 지원합니다. stb_image 헤더는 `-DSTB_IMAGE_INCLUDE_DIR=경로`로 지정합니다.

`-DLUX_FETCH_DEPENDENCIES=OFF`를 설정하면 구성과 빌드 중 의존성 다운로드를 하지 않습니다. 이 경우 SDL3 개발 패키지, stb_image 헤더, 셰이더 컴파일러를 미리 준비해야 합니다. 설치된 SDL3 패키지를 사용하면 SDL3 소스 빌드용 시스템 의존성은 필요하지 않습니다.

## 3. 디렉터리 구조

```text
LuxEngine/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── Assets/
│   └── Branding/              # 다크·라이트 창 로고 원본
├── Engine/
│   ├── Core/                  # 기본 타입, 수학, 이벤트, 타이머, 로거
│   ├── Platform/              # 창, 입력, 테마별 아이콘
│   ├── Renderer/
│   │   ├── RHI/               # 공개 GPU API: 디바이스, 버퍼, 텍스처
│   │   ├── Vulkan/            # Native Vulkan 백엔드 (내부 구현)
│   │   ├── Renderer2D/        # 사각형·스프라이트 배치
│   │   └── Renderer3D/        # 메시·방향광·깊이 테스트
│   ├── Scene/                 # 씬, 엔티티, 저장·복원
│   ├── ECS/                   # 공통 컴포넌트
│   ├── Physics/               # 기본 운동과 바닥 충돌
│   ├── Audio/                 # WAV 효과음·음악
│   ├── Animation/             # 키프레임 애니메이션
│   ├── Assets/                # 이미지·OBJ·셰이더 로딩
│   ├── Application.hpp/.cpp
│   └── LuxEngine.hpp
├── Runtime/                   # 런타임 모듈
├── Sandbox/                   # 데모 애플리케이션
├── Tests/                     # 회귀·통합 테스트
├── Shaders/                   # HLSL 셰이더 (SPIR-V로 컴파일)
├── Tools/                     # 빌드·IDE 실행·DXC 설치
└── cmake/                     # 의존성·셰이더·로고 내장 설정
```

`build/`는 생성 파일과 의존성 다운로드를 담고 Git에서 제외합니다. 로고 PNG는 빌드 중 엔진 바이너리에 포함됩니다.

## 4. 빌드 및 실행 방법

### 방법 A: Visual Studio

1. Visual Studio 2026에서 최상위 프로젝트 폴더를 엽니다.
2. 최초 빌드 전에 `Tools/SetupShaderCompiler.ps1`을 실행합니다.
3. Debug 또는 Release 프리셋을 선택합니다.
4. `LuxEngineDemo`를 시작 대상으로 빌드하고 실행합니다.

### 방법 B: PowerShell 스크립트

```powershell
.\Tools\SetupShaderCompiler.ps1
.\Tools\Build.ps1 -Config Debug
.\Tools\Build.ps1 -Config Release
```

Ninja는 컴파일러 환경이 설정된 개발자 셸에서 `.\Tools\Build.ps1 -Ninja -Config Debug`로 빌드합니다.

### 방법 C: CMake CLI

```powershell
cmake --preset vs2026-x64-debug
cmake --build --preset vs2026-debug
ctest --test-dir build/vs2026 -C Debug --output-on-failure
.\build\vs2026\bin\Debug\LuxEngineDemo.exe
```

Release 빌드는 `cmake --build --preset vs2026-release`, 검증은 `ctest --test-dir build/vs2026 -C Release --output-on-failure`를 사용합니다. `LUX_BUILD_TESTS=OFF`로 테스트 빌드를 제외할 수 있습니다.

## 5. 창 표시 및 네임스페이스·타깃 규약

- **창 제목**: `LuxEngine`
- **C++ 네임스페이스**: `LuxEngine`
- **엔진 타깃**: `LuxEngine` (`LuxEngine::Engine`)
- **런타임 타깃**: `LuxRuntime` (`LuxEngine::Runtime`)
- **데모 실행 파일**: `LuxEngineDemo.exe`
- **테스트 실행 파일**: `LuxEngineTests.exe`

### 시스템 테마와 로고

| Windows 앱 모드 | 창에 표시되는 로고 |
|---|---|
| 다크 | 밝은 로고 (`Assets/Branding/dark.png`) |
| 라이트 | 어두운 로고 (`Assets/Branding/light.png`) |

엔진이 생성하는 창에 자동 적용됩니다. 실행 중 시스템 테마가 바뀌면 아이콘도 갱신하며, 사용 프로그램에서 별도 설정이나 이미지 배포를 할 필요가 없습니다.

원본 이미지를 유지하면서 표시용 아이콘의 투명 여백을 줄여 로고를 크게 채웁니다. 32·64·128·256픽셀 표현을 제공해 화면 배율에 맞는 아이콘을 사용합니다. 제목 표시줄 아이콘 영역의 실제 크기는 운영체제가 결정합니다.

### 데모 조작

데모는 움직이는 2D 사각형과 방향광을 받으며 회전하는 3D 큐브를 표시합니다. Esc 또는 창 닫기로 종료합니다. `--smoke-test`는 짧게 실행한 후 자동 종료합니다.

## 6. 구현된 기능

- SDL 창·입력, 고정 간격 업데이트, 프레임 시간과 따라잡기 횟수 제한
- Native Vulkan: 프레임당 단일 렌더 패스, 프레임 인 플라이트(1~3), 타임라인 세마포어, 지연 해제, 업로드 배치, 스왑체인 재생성, GPU 타임스탬프, 프레임 캡처
- GPU 셰이더·파이프라인, 텍스처 샘플링, 알파 블렌딩, 3D 깊이 테스트
- 2D 사각형·회전·스프라이트, 연속된 동일 텍스처의 배치 처리
- 3D 큐브·인덱스 메시, XYZ 변환과 방향광 기반의 기본 조명
- 프레임별 정점 링 버퍼, 스테이징 메모리 상한, VMA 예산 내 할당
- PNG·JPEG·BMP 등 stb 지원 이미지의 실제 디코딩과 GPU 업로드
- OBJ 위치·면 읽기, 음수 인덱스와 다각형 팬 삼각분할
- WAV 효과음·음악 재생, 반복 재생·볼륨·정지·재생 종료 감지
- 엔티티 생존·소속·씬 수명 검사, 모든 컴포넌트의 삭제 처리
- 내장 컴포넌트의 버전 있는 텍스트 저장·복원, 잘못된 입력의 원래 씬 보존
- 키프레임 정렬·중복 교체·보간·반복·마지막 키프레임 유지
- 기본 중력·운동학 이동·고정 바닥 충돌, 트리거의 비차단 처리

## 7. API 규약과 현재 범위

`GetComponent`는 없는 엔티티·컴포넌트에 예외를 발생시키며 조회 중 데이터를 만들지 않습니다. 중복 `AddComponent`도 오류입니다. 씬 복원은 새 ID를 부여하므로 기존 Entity 핸들은 무효화됩니다. `GetEntities()`로 복원한 엔티티를 얻습니다. 사용자 정의 컴포넌트가 있으면 저장은 실패하며 데이터를 조용히 버리지 않습니다.

렌더러 통계의 drawCalls는 실제 GPU 명령을 기록했을 때만 증가합니다. 장면 시작 시 통계를 초기화합니다. `GpuDevice::GetFrameStats()`·`GetCounters()`·`GetMemoryStats()`는 바인딩·제출·대기 시간·GPU 시간·메모리 사용량을 제공합니다. 엔진 객체와 에셋 캐시는 메인 스레드에서 사용합니다. GPU 버퍼·텍스처는 디바이스가 소유한 세대 핸들이므로 디바이스보다 오래 살아남아도 안전하게 무효화됩니다. 사용 중인 리소스의 해제는 GPU가 마지막 사용 제출을 끝낸 뒤 실행됩니다. 프레임 중 `GpuBuffer::SetData`는 해당 프레임 제출 전에 적용됩니다.

GPU 오류 정책: 메모리 할당 실패·텍스처 크기 초과·정점 예산 초과는 해당 호출만 실패하고 디바이스는 계속 사용합니다. 프레임 제출이 실패하면 빈 프레임을 제출해 스왑체인을 유지하고 다음 프레임을 시도합니다. 연속 3회 프레임 실패, 복구 제출 실패, 표면 손실, 디바이스 손실(`GpuStatus::DeviceLost`)은 복구 불가로 처리하며 `Application`은 종료합니다. 새 디바이스는 `Shutdown()` 후 다시 `Initialize()`할 수 있습니다.

`Application::Run()`은 정상 종료와 예외 경로에서 파생 클래스 종료 훅을 실행합니다. 초기화 후 Run을 호출하지 않는 사용 방식에서는 파생 객체가 살아 있을 때 `Shutdown()`을 호출해야 합니다.

물리는 물체 간 충돌 탐색·접촉 솔버를 제공하지 않습니다. OBJ는 위치 기반 메시만 지원하며 glTF·스켈레탈 애니메이션·PBR·MP3 재생·에디터는 구현 범위에 포함되지 않습니다. 미지원 형식과 누락된 파일은 성공으로 가장하지 않습니다.

## 8. 검증

CTest는 `headless` 라벨(core, physics2d, scene, assets, audio, window)과 `gpu` 라벨(gpu, vulkan, failures, stress, lifecycle, longrun, runtime)로 나뉩니다. GPU 스위트는 실제 Vulkan 장치에서 동기화 검증(`LUX_VULKAN_VALIDATION=sync`)을 켜고 실행하며, 검증 오류가 하나라도 보고되면 실패합니다. 디스플레이나 Vulkan 장치가 없으면 통과로 위장하지 않고 건너뜀(종료 코드 77)으로 표시됩니다. `failures`는 `LUX_GPU_FAULT_INJECTION`(테스트 빌드 기본값)으로 초기화·할당·제출·타임아웃·디바이스 손실을 주입합니다. `longrun`은 `long` 라벨이며 `LUX_LONGRUN_SECONDS`(기본 20초)로 시간을 정합니다.

```bash
ctest --test-dir build -L headless        # 디스플레이 없이
xvfb-run -a ctest --test-dir build        # Linux 가상 디스플레이 + lavapipe
```

`LUX_VULKAN_VALIDATION`(0/off, 1/on, sync)과 `LUX_VULKAN_DEVICE`(장치 이름 일부)로 검증 레이어와 장치 선택을 바꿀 수 있습니다.

# LuxEngine

LuxEngine은 C++20을 기반으로 2D와 3D를 모두 지원하는 독립적인 크로스플랫폼 게임 엔진입니다.

## 1. 개발 환경

- **운영체제**: Windows 11
- **IDE**: Visual Studio 2026 Community (Dev18)
- **컴파일러**: MSVC v145 (`14.51.36231`)
- **언어 표준**: C++20 (`cxx_std_20`)
- **빌드 시스템**: CMake 4.4.3+
- **빌드 도구**: Ninja 1.13.2 / MSBuild
- **형상 관리**: Git
- **플랫폼 지원 목표**: Windows, Linux, macOS

## 2. 라이브러리 아키텍처

- **SDL3** (`release-3.4.18`): 창 생성, 입력 이벤트, SDL GPU 백엔드 (필수 통합 완료)
- **EnTT**: ECS (의존성 모듈 준비)
- **GLM**: 수학 연산 (의존성 모듈 준비)
- **Box2D**: 2D 물리 엔진 (의존성 모듈 준비)
- **Jolt Physics**: 3D 물리 엔진 (의존성 모듈 준비)
- **miniaudio**: 크로스플랫폼 오디오 엔진 (의존성 모듈 준비)
- **spdlog**: 고성능 로깅 (의존성 모듈 준비)
- **stb_image**: 텍스처/이미지 로딩 (의존성 모듈 준비)
- **cgltf**: glTF 3D 모델 및 씬 로딩 (의존성 모듈 준비)
- **Catch2**: 단위 테스트 프레임워크 (의존성 모듈 준비)

`cmake/Dependencies.cmake`에서 `LUX_ENABLE_*` 옵션을 활성화하여 즉시 통합할 수 있습니다.

## 3. 디렉터리 구조

```text
LuxEngine/
├── CMakeLists.txt              # 최상위 CMake 빌드 스크립트 (C++20, 의존성, 타깃 구성)
├── CMakePresets.json           # VS 2026 및 Ninja 프리셋 (Debug / Release)
├── .gitignore                  # 빌드 산출물 및 IDE 설정 제외
├── README.md                   # 프로젝트 설명 및 개발 가이드
│
├── Engine/                     # LuxEngine 코어 라이브러리
│   ├── Core/                   # 기본 타입, 타이머, 로거
│   ├── Platform/               # SDL3 윈도우 및 이벤트 처리
│   ├── Renderer/
│   │   ├── RHI/                # SDL3 GPU 디바이스 및 프레임 버퍼 추상화
│   │   ├── Renderer2D/         # 2D 렌더러
│   │   └── Renderer3D/         # 3D 렌더러
│   ├── Scene/                  # 씬 및 엔티티
│   ├── ECS/                    # 컴포넌트 시스템 추상화
│   ├── Physics/                # 2D/3D 물리 추상화 계층
│   ├── Audio/                  # 오디오 서브시스템
│   ├── Animation/              # 애니메이션 서브시스템
│   ├── Assets/                 # 에셋 매니저
│   ├── Application.hpp/.cpp    # 메인 애플리케이션 프레임워크
│   └── LuxEngine.hpp           # 원클릭 인클루드 마스터 헤더
│
├── Runtime/                    # 엔진 런타임 공유 모듈
├── Sandbox/                    # 데모 애플리케이션 (LuxEngineDemo.exe)
├── Tests/                      # 단위 테스트 (LuxEngineTests.exe)
├── Shaders/                    # 셰이더 소스 (Clear.vert, Clear.frag)
├── Tools/                      # 빌드 및 실행 도구 (Build.ps1, LaunchVS.ps1)
└── cmake/                      # 컴파일러 경고 및 의존성 모듈
```

## 4. 빌드 및 실행 방법

### 방법 A: Visual Studio 2026으로 열기 (권장)
1. 바탕화면의 **LuxEngine** 바로가기를 더블클릭합니다.
2. Visual Studio 2026이 열리면 `CMakePresets.json`이 자동 인식됩니다.
3. 시작 프로젝트로 `LuxEngineDemo.exe`가 선택되어 있으므로 **F5** 키를 눌러 바로 빌드 및 실행할 수 있습니다.

### 방법 B: PowerShell 스크립트 사용
```powershell
# Debug 빌드 (Visual Studio 2026 솔루션 생성 및 컴파일)
.\Tools\Build.ps1 -Config Debug

# Release 빌드
.\Tools\Build.ps1 -Config Release

# Ninja 빌드
.\Tools\Build.ps1 -Ninja -Config Debug
```

### 방법 C: CMake CLI 직접 사용
```powershell
# Visual Studio 2026 Generator
cmake --preset vs2026-x64-debug
cmake --build --preset vs2026-debug

# 데모 실행
.\build\vs2026\bin\Debug\LuxEngineDemo.exe
```

## 5. 네임스페이스 및 타깃 규약

- C++ 네임스페이스: `LuxEngine`
- 엔진 타깃: `LuxEngine` (별칭: `LuxEngine::Engine`)
- 런타임 타깃: `LuxRuntime` (별칭: `LuxEngine::Runtime`)
- 실행 파일: `LuxEngineDemo.exe`
- 테스트 실행 파일: `LuxEngineTests.exe`

# LuxEngine

LuxEngine은 C++20과 The Forge를 사용하는 2D·3D 게임 엔진입니다. Windows는 Direct3D 12, Linux는 Vulkan, macOS는 Metal로 렌더링합니다. SDL3는 창·입력·오디오와 플랫폼 표면 연결을 담당하며, Qt 기반 LuxEditor와 독립 플레이어를 제공합니다. 개발과 빌드의 기준은 저장소 최상위 CMake 프로젝트입니다.

## 1. 개발 환경

- **플랫폼**: Windows / MSVC, Linux / GCC·Clang, macOS / Xcode
- **언어 표준**: C++20 (`cxx_std_20`), C# 스크립트는 .NET 10
- **빌드 시스템**: CMake 3.25 이상과 MSBuild 또는 Ninja. Visual Studio 2026 프리셋은 해당 생성기를 지원하는 CMake가 필요합니다.
- **에디터**: Qt 6.4 이상과 libzip
- **셰이더 도구**: Windows는 고정 The Forge 소스에 포함된 DXC, Linux는 `glslangValidator`, macOS는 Xcode Metal 컴파일러
- **형상 관리**: Git

플랫폼별 CI 구성은 [CI 문서](docs/CI.md), 실제 GPU와 SDK 검증의 환경·범위는 [런타임 기반 검증 기록](docs/RuntimeFoundationValidation.md)과 [구조 개편 검증 기록](docs/ArchitectureMigrationValidation.md)을 참고하세요. 기록된 검증은 해당 리비전과 환경에 대한 결과입니다.

## 2. 라이브러리 아키텍처

| 구성 요소 | 용도 |
|---|---|
| The Forge | 플랫폼 그래픽 API, GPU 리소스·파이프라인·셰이더 실행 |
| SDL3 3.2 이상 | 창, 입력, 오디오, 테마·아이콘, 플랫폼 표면 |
| Box2D 2.4 계열 | 씬의 2D 충돌·물리 |
| stb_image | PNG·JPEG·BMP·TGA 등 이미지 디코딩 |
| nlohmann_json 3.11 이상 | 씬·프로젝트·재질·스크립트 필드 저장 |
| ufbx | 저장소에 포함한 고정 버전의 정적 FBX 로더 |
| .NET 10 SDK·호스팅 팩 | C# 빌드와 엔티티 스크립트 실행 |
| Qt 6.4 이상·libzip | 선택적으로 빌드하는 에디터와 프로젝트 아카이브 |
| 자체 ECS·수학·런타임 모듈 | 엔티티, 계층, 장면 실행, 공통 값과 수명 관리 |

### The Forge 렌더러

렌더링 경로는 The Forge로 통합했습니다. 이전 Native Vulkan 구현과 실행 시 백엔드 선택은 제거했으며 `LUX_ENABLE_FORGE`·`LUX_RENDER_BACKEND`를 선택할 필요가 없습니다. 플랫폼별 렌더러는 `LuxForge.dll`, `libLuxForge.so`, `libLuxForge.dylib`로 분리합니다. 엔진과 렌더러는 동일한 ABI로 함께 빌드해야 합니다. 외부 소스의 고정 리비전 준비와 플랫폼별 의존성은 [The Forge 문서](docs/TheForge.md)를 따릅니다.

### 설치된 의존성과 오프라인 빌드

CMake는 의존성을 다운로드하지 않습니다. SDL3·Box2D·stb_image·JSON 라이브러리·Python·.NET 호스팅 팩·The Forge 소스를 먼저 준비합니다. 에디터를 빌드할 때는 Qt·libzip도 필요합니다. 워크스페이스 안의 포터블 도구·의존성을 사용하고 `CMAKE_PREFIX_PATH`, `SDL3_DIR`, `STB_IMAGE_INCLUDE_DIR`, `LUX_FORGE_ROOT`, `LUX_DOTNET_EXECUTABLE`로 위치를 지정할 수 있습니다.

Linux에는 Vulkan 개발 헤더·드라이버와 X11·Wayland 개발 패키지가 필요합니다. Windows의 D3D12 경로와 macOS의 Metal 경로에는 이전 Native Vulkan용 SDK·VMA 설치 절차를 적용하지 않습니다. C# 실행 컴퓨터에는 .NET 10 런타임이 필요하며 비표준 설치 위치는 `DOTNET_ROOT` 또는 런타임 설정으로 지정합니다. 자세한 준비와 배포 범위는 [C# 문서](docs/CSharpScripting.md)에 정리했습니다.

## 3. 디렉터리 구조

```text
LuxEngine/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── Engine/
│   ├── Core/, ECS/, Scene/     # 기본 타입, 엔티티, 씬
│   ├── Platform/, Application/ # 창·입력, 애플리케이션 수명
│   ├── RHI/Public/LuxEngine/   # 공개 GPU API: 디바이스, 버퍼, 텍스처
│   ├── Renderer/
│   │   ├── Public/LuxEngine/  # 2D·3D 렌더러 공개 API
│   │   └── Private/           # RHI·Forge 구현, 2D·3D 렌더러와 활성 셰이더
│   ├── Assets/, Environment/  # 메시·이미지·환경 데이터
│   ├── Physics/, Audio/, Animation/
│   ├── Runtime/, Scripting/   # 독립 플레이어·C# 호스팅
│   ├── Resources/             # 로고·캐릭터 원본
│   └── Tests/                 # 엔진·런타임 회귀 검사
├── Editor/                    # 별도 Qt 에디터
├── samples/                   # 실행 가능한 게임 샘플
├── tools/                     # 빌드·배포·셰이더 도구
└── cmake/                     # 의존성·셰이더·로고 내장 설정
```

엔진 모듈은 `Public/LuxEngine/`의 공개 헤더와 `Private/` 구현을 분리하며, 공개 헤더는 `<LuxEngine/...>`로 포함합니다. `build/`는 생성 파일을 담고 Git에서 제외합니다. `Engine/Resources/Branding/`의 로고 PNG는 빌드 중 엔진 바이너리에 포함됩니다.

## 4. 빌드 및 실행 방법

### 방법 A: Visual Studio

1. Visual Studio 2026에서 최상위 프로젝트 폴더를 엽니다.
2. [The Forge 문서](docs/TheForge.md)에 따라 고정 소스와 의존성을 준비합니다.
3. Debug 또는 Release 프리셋을 선택합니다.
4. `SampleBasic2D`를 빌드하고 실행합니다. 에디터는 구성 시 `LUX_BUILD_EDITOR=ON`을 지정하고 `LuxEditor`를 실행합니다.

### 방법 B: PowerShell 스크립트

```powershell
.\tools\Build.ps1 -Config Debug
.\tools\Build.ps1 -Config Release
```

Ninja는 컴파일러 환경이 설정된 개발자 셸에서 `.\tools\Build.ps1 -Ninja -Config Debug`로 빌드합니다.

### 방법 C: CMake CLI

```powershell
cmake --preset vs2026-x64-debug -DLUX_BUILD_EDITOR=ON
cmake --build --preset vs2026-debug
ctest --test-dir build/vs2026 -C Debug -L headless --output-on-failure
.\build\vs2026\bin\Debug\SampleBasic2D.exe
```

Release 빌드는 `cmake --build --preset vs2026-release`를 사용합니다. `LUX_BUILD_TESTS=OFF`로 테스트 빌드를 제외할 수 있습니다. 엔진만 빌드할 때는 `LUX_BUILD_EDITOR=OFF`를 사용합니다.

Linux·macOS의 단일 구성 빌드는 다음과 같이 실행합니다. 의존성이 별도 위치라면 `CMAKE_PREFIX_PATH` 등을 추가합니다.

```sh
cmake -S . -B build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLUX_BUILD_EDITOR=ON
cmake --build build/desktop
ctest --test-dir build/desktop -L headless --output-on-failure
```

주요 실행 타깃은 `LuxEditor`, `LuxPlayer`, `SampleBasic2D`, `SamplePhysics2D`, `SampleInputBindings`, `SampleForestSurvivor`, `SamplePandaGarden`, `SampleRainyCreek`입니다. 샘플 조작과 에셋 출처는 [샘플 문서](docs/Samples.md)를 참고하세요.

## 5. 창 표시 및 네임스페이스·타깃 규약

- **창 제목**: `LuxEngine`
- **C++ 네임스페이스**: `LuxEngine`
- **엔진 타깃**: `LuxEngine` (`LuxEngine::Engine`)
- **런타임 타깃**: `LuxRuntime` (`LuxEngine::Runtime`)
- **기본 샘플 실행 파일**: `SampleBasic2D.exe`
- **테스트 실행 파일**: `LuxEngineTests.exe`

### 시스템 테마와 로고

| Windows 앱 모드 | 창에 표시되는 로고 |
|---|---|
| 다크 | 밝은 로고 (`Engine/Resources/Branding/dark.png`) |
| 라이트 | 어두운 로고 (`Engine/Resources/Branding/light.png`) |

엔진이 생성하는 창에 자동 적용됩니다. 실행 중 시스템 테마가 바뀌면 아이콘도 갱신하며, 사용 프로그램에서 별도 설정이나 이미지 배포를 할 필요가 없습니다.

원본 이미지를 유지하면서 표시용 아이콘의 투명 여백을 줄여 로고를 크게 채웁니다. 32·64·128·256픽셀 표현을 제공해 화면 배율에 맞는 아이콘을 사용합니다. 제목 표시줄 아이콘 영역의 실제 크기는 운영체제가 결정합니다.

### 데모 조작

데모는 움직이는 2D 사각형과 방향광을 받으며 회전하는 3D 큐브를 표시합니다. Esc 또는 창 닫기로 종료합니다. `--smoke-test`는 짧게 실행한 후 자동 종료합니다.

## 6. 구현된 기능

- SDL 창·입력·WAV 오디오, 고정 간격 업데이트, 애플리케이션 수명 관리
- The Forge 기반 2D 스프라이트·3D 메시, PBR·HDR·톤 매핑·환경광/IBL·스카이박스·방향광 그림자·점광원
- 불투명·컷아웃·반투명 재질, 깊이·알파 처리, 양면 표시와 반투명 정렬
- 밉맵·Clamp/Repeat·이방성 샘플링, 기본색·노멀·금속성·거칠기·AO 텍스처
- OBJ·정적 FBX 가져오기, 법선·UV·축·단위 처리와 기본 FBX 모델 10종
- ECS, 부모·자식 계층, 버전 있는 씬·프로젝트 JSON, 다중 씬과 로딩·스트리밍
- Box2D 기반 2D 충돌·트리거, 기본 3D 중력·바닥 충돌
- Qt 레벨 에디터, 2D·3D 뷰포트, 선택·기즈모·스냅·Details·콘텐츠 탐색·Undo/Redo
- Transform 키프레임·타임라인·독립 애니메이션 작업 공간
- C# 수명 콜백·Transform·입력, 사용자 필드 Inspector·씬 저장·런타임 적용과 엔티티 참조 재매핑
- 프리팹 파일 저장·배치·인스턴스 원본 갱신·재정의 유지·되돌리기·연결 해제
- 독립 플레이어, 설치 SDK, 생성 게임과 리소스 패키징

| 사용하려는 기능 | 설명 |
| --- | --- |
| 재질 모드·PBR 텍스처·샘플링 | [3D 재질과 텍스처](docs/Materials.md) |
| C# 사용자 필드와 Inspector | [C# 스크립트](docs/CSharpScripting.md#사용자-필드와-inspector) |
| 프리팹 생성·배치·갱신 | [씬 관리](docs/SceneManagement.md#프리팹) |
| 레벨·애니메이션 편집 | [에디터 작업 공간](docs/EditorWorkspace.md), [애니메이션](docs/AnimationWorkspace.md) |
| 모델 가져오기 | [FBX와 기본 모델](docs/FbxModels.md) |
| 엔진 API와 JSON 형식 | [소스 구조](docs/SourceArchitecture.md), [구조화 파일 형식](docs/StructuredFileFormats.md) |

## 7. API 규약과 현재 범위

`GetComponent`는 없는 엔티티·컴포넌트에 예외를 발생시키며 조회 중 데이터를 만들지 않습니다. 중복 `AddComponent`도 오류입니다. 씬 복원은 새 ID를 부여하므로 기존 Entity 핸들은 무효화됩니다. `GetEntities()`로 복원한 엔티티를 얻습니다. 사용자 정의 컴포넌트가 있으면 저장은 실패하며 데이터를 조용히 버리지 않습니다.

엔진 객체와 에셋 캐시는 씬 소유 스레드에서 사용합니다. GPU 버퍼·텍스처는 디바이스 수명과 연결하며 사용 중인 자원의 해제는 GPU 제출 완료 뒤에 처리합니다. 초기화·프레임·그리기 호출의 반환값과 `GetLastError()`로 실패 원인을 확인합니다. 프레임 통계와 메모리 API의 현재 구현·검증 범위는 [The Forge 문서](docs/TheForge.md)와 [런타임 검증 기록](docs/RuntimeFoundationValidation.md)을 참고하세요.

이전 Native Vulkan의 오류 주입·타임라인 계측·디바이스 손실 복구 정책이 Forge에도 그대로 구현됐다고 간주하지 않습니다. 새 렌더러의 플랫폼별 복구·계측 지원은 별도 검증 범위입니다.

`Application::Run()`은 정상 종료와 예외 경로에서 파생 클래스 종료 훅을 실행합니다. 초기화 후 Run을 호출하지 않는 사용 방식에서는 파생 객체가 살아 있을 때 `Shutdown()`을 호출해야 합니다.

현재 3D 물리는 기본 중력·운동학 이동·고정 바닥 충돌 범위이며, 범용 3D 접촉 솔버는 후속 기능입니다. FBX는 정적 지오메트리 가져오기 범위로 스킨·모프·클립 재생을 포함하지 않습니다. 애니메이션은 Transform 클립·선형 보간이며 스켈레탈 애니메이션과 상태 머신은 후속 범위입니다. C# 디버거·자동 컴파일·핫 리로드와 고급 프리팹 제작 도구도 각 기능 문서의 지원 범위를 확인해야 합니다.

반투명은 오브젝트 원점 거리 정렬을 사용하므로 교차하는 투명 메시의 순서를 보장하지 않습니다. 기존 부분 알파 재질은 `Blend`를 명시해야 하며 물 전용 반사·굴절은 별도 기능입니다. 자세한 마이그레이션과 샘플링 한계는 [재질 문서](docs/Materials.md)에 정리했습니다.

## 8. 검증

CTest는 화면·GPU가 필요 없는 `headless`와 실제 렌더링을 검사하는 `gpu` 라벨을 사용합니다. Forge 회귀는 `forge` 라벨로 선택합니다. Qt headless 검사는 `QT_QPA_PLATFORM=offscreen`, `LUX_EDITOR_SOFTWARE_VIEWPORT=1`로 실행할 수 있습니다.

```powershell
$env:QT_QPA_PLATFORM = 'offscreen'
$env:LUX_EDITOR_SOFTWARE_VIEWPORT = '1'
ctest --test-dir build/vs2026 -C Debug -L headless --output-on-failure
ctest --test-dir build/vs2026 -C Debug -L forge --output-on-failure
```

마지막 명령은 실제 창 시스템과 그래픽 드라이버가 필요합니다. 지원하는 GPU 스모크 검사는 표시 환경이 없으면 종료 코드 `77`로 건너뛰며, 실제 초기화·렌더링 오류는 실패로 보고합니다. 구형 Native Vulkan 전용 이름·라벨이 남아 있다고 해당 오류 주입과 계측이 새 백엔드에서도 지원된다는 의미는 아닙니다.

검증 계층은 현재 The Forge의 빌드 설정을 따릅니다. Debug·Release 및 API별 동작은 [The Forge 검증 범위](docs/TheForge.md#검증과-남은-범위)를 참고하세요. CI의 플랫폼·배포·소비자 검사 구성과 로그 위치는 [CI 문서](docs/CI.md)에 기록합니다. 실제 GPU 실행 결과와 호스팅 러너의 headless 성공은 구분합니다.

신규 기능을 골라 검사하려면 다음 이름을 사용합니다. GPU 검사는 별도 표시 세션에서 실행합니다.

```sh
ctest --test-dir build/desktop -R 'Engine_texture_mips|Engine_scene_json|Engine_csharp|Engine_prefab_library|Editor_script_fields|Editor_prefab_ui' --output-on-failure
ctest --test-dir build/desktop -R 'Forge_materials|Forge_material_textures|Editor_picking3d_gpu|Editor_picking3d_hidpi' --output-on-failure
```

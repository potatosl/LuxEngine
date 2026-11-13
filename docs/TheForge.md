# The Forge 백엔드

LuxEngine의 기본 렌더러는 Native Vulkan입니다. 선택적 The Forge 백엔드는 Windows/MSVC에서 Direct3D 12를 사용하며, 엔진이 `LuxForge.dll`을 실행 중에 불러오는 구조입니다. `LUX_ENABLE_FORGE`의 기본값은 `OFF`이고, 현재 기본 CI도 Forge를 켜지 않습니다. 기본 CI 결과에 Forge의 빌드·GPU 검증은 포함되지 않습니다.

## 준비

Windows용 Visual Studio/MSVC와 일반 엔진 빌드에 필요한 Vulkan SDK, SDL3, Box2D, VMA, stb_image, nlohmann_json 3.11 이상, SPIR-V 셰이더 컴파일러를 준비합니다. Forge를 켜도 엔진의 Vulkan 소스와 셰이더를 함께 빌드하므로 Vulkan SDK는 필수입니다. 의존성을 별도 경로에 준비했다면 기존과 같이 `CMAKE_TOOLCHAIN_FILE`, `CMAKE_PREFIX_PATH` 또는 각 패키지 경로를 CMake에 전달합니다.

The Forge 소스는 CMake가 다운로드하지 않습니다. 기준 리비전은 `cd5046893faba2dc7869243873bf01f02a6f0df9`이며, 기본 위치는 워크스페이스 안의 `external/TheForge`입니다. 이 경로는 Git에서 제외되어 있습니다. 새로 준비할 때는 저장소 루트에서 다음 명령을 사용합니다.

```powershell
git clone --filter=blob:none --sparse https://github.com/ConfettiFX/The-Forge.git external/TheForge
git -C external/TheForge checkout --detach cd5046893faba2dc7869243873bf01f02a6f0df9
git -C external/TheForge sparse-checkout set Common_3 Tools/python-3.6.0-embed-amd64
```

이미 체크아웃이 있다면 복제 명령을 생략하고 리비전과 필요한 경로를 확인합니다. `Common_3`에는 FSL 컴파일러와 Direct3D 12용 라이브러리·DLL이, `Tools/python-3.6.0-embed-amd64`에는 FSL 실행용 포터블 Python이 필요합니다. 다른 위치를 사용하려면 `-DLUX_FORGE_ROOT=경로`를 지정합니다. 기준 리비전은 [cmake/TheForge.cmake](../cmake/TheForge.cmake)에 기록되어 있으며 CMake가 체크아웃의 Git 리비전을 자동으로 교체하거나 일치 여부를 강제하지는 않습니다.

## 빌드와 테스트

다음 명령은 저장소의 공개 Visual Studio 2026 Debug 프리셋을 사용합니다. 패키지 경로가 필요한 환경에서는 구성 명령에 해당 인자를 추가합니다.

```powershell
cmake --preset vs2026-x64-debug -DLUX_ENABLE_FORGE=ON -DLUX_BUILD_TESTS=ON
cmake --build --preset vs2026-debug --target LuxForgeProbe LuxForgeBackendTests LuxPlayer
& .\build\vs2026\bin\Debug\LuxForgeProbe.exe
ctest --test-dir build/vs2026 -C Debug -R "^Forge_backend_basic$" --output-on-failure
```

`LuxForgeProbe`는 DLL 로딩과 Direct3D 12 디바이스 초기화를 확인합니다. `Forge_backend_basic`은 프레임·텍스처·버퍼·메시·씬 렌더링 등의 회귀 검사를 실행합니다. 특정 구간만 실행하려면 테스트 실행 파일에 이름을 전달합니다.

```powershell
& .\build\vs2026\bin\Debug\LuxForgeBackendTests.exe scene3d
```

테스트 실행 파일은 자체적으로 Forge를 선택합니다. GPU 테스트에는 사용 가능한 Windows 창 시스템과 Direct3D 12 장치가 필요합니다. SDL 비디오 초기화 실패에 따른 종료 코드 `77`은 CTest에서 건너뜀으로 표시하며, Forge 디바이스 초기화 실패는 검사 실패로 보고합니다. `endurance`, `stress`, `mix`, `grow` 장시간 부하 검사는 이름으로 요청할 때만 실행합니다. 세부 검사와 인자는 [Engine/Tests/ForgeBackendTest.cpp](../Engine/Tests/ForgeBackendTest.cpp)에 있습니다.

## 실행과 배포 파일

일반 엔진 실행 파일에서 Forge를 사용하려면 실행 전에 환경 변수를 설정합니다.

```powershell
$env:LUX_RENDER_BACKEND = "forge"
& .\build\vs2026\bin\Debug\LuxPlayer.exe .\samples\ScenePlayer\Scene.lux --frames 60
Remove-Item Env:LUX_RENDER_BACKEND
```

환경 변수가 없으면 Native Vulkan을 선택합니다. 빌드 옵션은 DLL 생성 여부를, 환경 변수는 실행 시 사용할 백엔드를 결정합니다. Forge를 선택한 상태에서 DLL을 불러오지 못하면 초기화 오류를 반환합니다.

`LuxForge.dll`과 해당 빌드의 의존 DLL, `gpu.data`, `DIRECT3D12/` 셰이더 디렉터리를 실행 파일 옆에 함께 유지합니다. CMake는 Forge 빌드 출력으로 이 파일들을 복사하고 설치 대상에도 포함합니다. 의존 DLL 목록과 셰이더 생성 경로는 [cmake/TheForge.cmake](../cmake/TheForge.cmake)에 정의되어 있습니다.

엔진과 DLL 사이의 C API 버전은 현재 `6`입니다. [LuxForgeApi.h](../Engine/Renderer/Private/Forge/LuxForgeApi.h)의 버전과 DLL이 다르면 초기화를 거부하므로 API 변경 시 엔진과 `LuxForge.dll`을 함께 다시 빌드합니다.

## 제공 경로와 제약

Forge 백엔드는 프레임 제출·리사이즈·캡처, 2D 텍스처 출력, 인덱스 메시와 조명, PBR/HDR 씬·톤 매핑·환경 반사·방향광 그림자 경로를 제공합니다. 외부 환경 데이터의 CPU 준비는 비동기로 진행하며 준비된 데이터를 Forge 리소스로 업로드합니다. 엔진 측 연결은 [ForgeBackend.cpp](../Engine/Renderer/Private/Forge/ForgeBackend.cpp), DLL 구현은 [LuxForge.cpp](../Engine/Renderer/Private/Forge/LuxForge.cpp), 셰이더 목록은 [LuxForgeShaders.list](../Engine/Renderer/Private/Forge/Shaders/LuxForgeShaders.list)에 있습니다.

현재 통합 대상은 Windows/MSVC의 Direct3D 12 경로입니다. The Forge가 다른 플랫폼과 API를 지원하더라도 이 옵션이 해당 경로까지 활성화하지는 않습니다. The Forge의 전역 렌더러 상태 때문에 한 프로세스에서 Forge 디바이스는 하나만 사용할 수 있으며, 살아 있는 디바이스가 있는 상태에서 두 번째 초기화를 요청하면 실패합니다. 기존 Vulkan의 다중 디바이스 사용 흐름을 Forge에서 그대로 사용할 수는 없습니다.

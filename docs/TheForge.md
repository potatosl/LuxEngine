# The Forge 렌더러

엔진의 렌더링 경로를 The Forge로 통합했다. 이전 Native Vulkan 구현과 실행 시 백엔드 선택 분기는 제거했다. 엔진은 공개 RHI를 유지하고, 플랫폼별 그래픽 API와 The Forge의 전역 상태는 별도 `LuxForge` 공유 라이브러리 안에 두었다.

| 플랫폼 | 그래픽 API | 런타임 파일 |
| --- | --- | --- |
| Linux | Vulkan | `libLuxForge.so` |
| Windows / MSVC | Direct3D 12 | `LuxForge.dll` |
| macOS | Metal | `libLuxForge.dylib` |

세 플랫폼의 소스·셰이더·링크·배포 경로를 구성했다. Linux에서는 X11과 Wayland 각각에서 Forge 전용 GPU 회귀 6개를 통과했다. 이 변경에서 Windows·macOS의 현지 빌드 및 GPU 실행은 검증하지 못했다.

## 의존성 준비

CMake는 의존성을 다운로드하지 않는다. SDL3 3.2 이상, Box2D 2.4 계열, stb_image, nlohmann_json 3.11 이상, Python 3, .NET 10 SDK를 시스템이나 별도 개발 도구 경로에 준비한다. .NET 호스팅과 게임 스크립트 구성은 [CSharpScripting.md](CSharpScripting.md)에 정리했다.

The Forge 기준 리비전은 `cd5046893faba2dc7869243873bf01f02a6f0df9`이다. 기본 소스 위치는 Git에서 제외한 `Engine/ThirdParty/TheForge`이고, 다른 설치를 사용하려면 `LUX_FORGE_ROOT`를 지정한다. 소스 준비는 사용자가 빌드 전에 수행한다.

```sh
git clone --filter=blob:none --sparse https://github.com/ConfettiFX/The-Forge.git Engine/ThirdParty/TheForge
git -C Engine/ThirdParty/TheForge checkout --detach cd5046893faba2dc7869243873bf01f02a6f0df9
git -C Engine/ThirdParty/TheForge sparse-checkout set Common_3
```

CMake는 외부 체크아웃의 리비전을 변경하지 않는다. 기준 리비전은 [TheForge.cmake](../cmake/TheForge.cmake)에 기록했다.

Linux에는 Vulkan 드라이버·개발 헤더, `glslangValidator`, X11·Wayland 개발 패키지가 필요하다. Windows는 MSVC와 The Forge 체크아웃의 Direct3D 12 관련 라이브러리·DXC를 사용한다. macOS는 Xcode 도구의 Metal 컴파일러와 시스템 프레임워크를 사용한다.

## 빌드

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DLUX_BUILD_EDITOR=ON
cmake --build build --target LuxEditor LuxForgeBackendTests
ctest --test-dir build -L forge --output-on-failure
```

`LUX_ENABLE_FORGE`나 `LUX_RENDER_BACKEND`를 선택할 필요가 없다. 렌더러 파일이 없거나 엔진과 ABI가 맞지 않으면 초기화 오류를 반환한다. ABI 버전은 현재 `8`이며 엔진과 렌더러를 함께 다시 빌드해야 한다.

## 프레임과 자원 수명

각 창은 별도 스왑체인·명령 링·프레임 자원을 사용한다. The Forge의 렌더러·리소스 로더·정적 샘플러 루트는 프로세스 안에서 공유하며 마지막 장치가 종료될 때 해제한다. 화면 획득 세마포어는 프레임 슬롯과, 제출 완료를 기다리는 프레젠테이션 세마포어는 스왑체인 이미지와 수명을 맞췄다.

Vulkan에서도 `INIT_RS_DESC`와 `initRootSignature`를 호출해 기본 정적 샘플러 디스크립터를 생성한다. 이 초기화가 없으면 첫 파이프라인 바인딩에서 assertion이 발생했다.

기준 리비전의 리소스 로더는 업로드용 전송 큐에서 셰이더 접근 배리어를 실행한다. Vulkan 검증 계층과 호환되도록 Linux 빌드에서만 생성한 리소스 로더 사본의 업로드 큐를 그래픽 큐로 바꿨다. 외부 소스 체크아웃은 수정하지 않는다. 이 호환 처리는 CMake에 주석과 적용 범위를 기록했다.

## 실행 파일과 SDK 배포

실행 파일 옆에 플랫폼별 `LuxForge` 라이브러리, `gpu.data`, `gpu.cfg`, API별 셰이더 폴더(`VULKAN/`, `DIRECT3D12/`, `MACOS/`)를 유지한다. Windows에서는 AMD AGS·DXC·PIX 런타임 DLL도 함께 배포한다. CMake 빌드와 설치 규칙이 이 파일을 복사한다.

설치 SDK를 사용하는 프로젝트는 `lux_copy_runtime(게임타깃)`을 호출한다. 이 함수는 렌더러·셰이더와 C# 코어·nethost도 실행 폴더에 복사한다. .NET 런타임 전체를 게임에 묶는 self-contained 배포는 아직 구현하지 않았다.

## 검증과 남은 범위

Forge 전용 회귀 테스트는 초기화·캡처, 프레임 반복, 드로잉·텍스처, 메시, 3D 씬을 검사한다. 실행 환경에 창 시스템이 없으면 종료 코드 `77`을 반환하며 CTest에서 건너뜀으로 표시한다. 실제 GPU 초기화 실패와 렌더링 오류를 성공으로 처리하지 않는다.

삭제한 Native Vulkan 구현의 오류 주입 API는 Forge의 검증 기능이 아니다. 기존 오류 주입 전용 테스트는 해당 기능이 없음을 명시하며, Forge에서 동일한 오류 복구 기능이 구현됐다고 간주하지 않는다. 렌더러 이관과 별개로 각 플랫폼의 GPU 손실 복구·메모리 계측·배포 도구 검증을 계속 확장해야 한다.

검증 계층은 기준 The Forge의 빌드 설정(`ENABLE_GRAPHICS_VALIDATION`)에 따라 프로세스 단위로 구성한다. 현재 Debug는 검증을 켜고 Release는 끈다. 공유 렌더러를 사용하는 각 뷰포트의 `GpuValidation::Off`/`Auto` 요청으로 이 모드를 실행 중에 전환하지는 못한다. 장치 정보는 요청값 대신 실제 빌드 모드를 반환한다.

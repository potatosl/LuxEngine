# 엔진 샘플

`samples/`의 각 폴더는 LuxEngine을 프레임워크로 사용하는 독립 실행 파일이다. 기존 `Sandbox/`와 Demo는 제거했다. 에디터를 빌드하지 않아도 샘플을 실행할 수 있다.

| 경로 | 빌드 타깃 | 확인할 기능 |
| --- | --- | --- |
| `samples/Basic2D/` | `SampleBasic2D` | 씬·엔티티·Transform, 2D 카메라, 캐릭터 이동 |
| `samples/Physics2D/` | `SamplePhysics2D` | Box2D 동적 상자, 정적 지형, 회전과 충돌 |
| `samples/InputBindings/` | `SampleInputBindings` | `LuxKey`, 키보드·마우스·게임패드 액션, 실행 중 키 변경 |
| `samples/ForestSurvivor/` | `SampleForestSurvivor` | 자동 전투, 경험치, 강화 선택, 보스, 승패와 재시작 |
| `samples/Common/` | `LuxSampleSupport` | 앞의 세 교육용 샘플이 공유하는 실행·이미지·폰트·캡처 코드 |
| `samples/Assets/` | `LuxSampleAssets` | 검증한 CC0 원본 에셋, 라이선스와 해시 목록 |

## 빌드와 실행

엔진이 요구하는 개발 패키지와 셰이더 컴파일러를 먼저 설치한다. CMake는 네트워크에서 샘플 에셋이나 라이브러리를 다운로드하지 않는다.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DLUX_BUILD_TESTS=ON
cmake --build build --target SampleForestSurvivor
./build/bin/SampleForestSurvivor
```

Visual Studio 같은 다중 구성 생성기는 `cmake --build build --config Debug --target SampleForestSurvivor`로 빌드하고 `build/bin/Debug/`의 실행 파일을 연다. 실행 파일 옆의 `assets/`와 `shaders/`를 함께 유지한다.

서바이벌 게임의 일반 실행은 타이틀 화면에서 기다린다. **PLAY 버튼을 마우스로 눌러야 시작한다.** Enter, Space, Esc 또는 화면의 빈 곳을 눌러도 시작하지 않는다. 메뉴 버튼은 누른 곳과 뗀 곳이 같은 버튼일 때 실행한다. 일시정지·계속·재시작·종료와 강화 선택에도 화면 버튼이 있다.

플레이 중에는 WASD·방향키·게임패드 왼쪽 스틱으로 이동하고 Space·Shift·게임패드 X로 대시한다. 공격은 자동이다. 경험치를 모아 세 가지 강화 중 하나를 선택하고, 5분을 버틴 뒤 보스를 처치하면 승리한다. P·Esc·게임패드 Start는 일시정지 단축키이고 F11은 전체 화면 전환이다.

교육용 샘플 세 개는 실행 즉시 동작한다. P는 일시정지, R은 초기화, Esc는 종료다. 물리 샘플은 Space·마우스 왼쪽 버튼·게임패드 A로 상자를 추가하고, 입력 샘플은 F로 액션의 키보드 바인딩을 바꾼다.

## 공통 진입점

`main`을 직접 작성하는 대신 진입점이 있는 `.cpp` 한 곳에서만 헤더와 매크로를 사용한다.

```cpp
#include "Engine/EntryPoint.hpp"

int RunGame(int argc, char** argv) {
    // 옵션 처리, Application 생성, Initialize()와 Run()을 호출한다.
    return 0;
}

LUX_ENGINE_MAIN(RunGame)
```

콜백은 `int(int, char**)` 형식이다. 명령행 인자와 반환 코드를 그대로 전달하고, 처리되지 않은 예외는 오류 로그와 종료 코드 1로 바꾼다. SDL의 플랫폼 진입점 코드가 중복 생성되지 않도록 `EntryPoint.hpp`를 공용 헤더나 여러 `.cpp`에 포함하지 않는다. 설치한 SDK의 MinimalGame과 에디터가 생성하는 게임 템플릿도 같은 진입점을 사용한다.

## 게임 코드 구조

ForestSurvivor의 `Game/`은 전투와 성장 규칙, `App/`은 엔진 생명주기·입력·오디오 연결, `Presentation/`은 화면과 버튼 배치를 담당한다. 게임 규칙 테스트는 창이나 GPU 없이 실행한다. UI 테스트는 실제 입력 이벤트로 버튼 클릭, 드래그 취소, 메뉴 상태 전환과 창 크기 변경을 검사한다.

픽셀 이미지는 텍스처 생성 시 `TextureFilter::Nearest`를 명시한다. 일반 텍스처의 기본 필터는 `Linear`다. 현재 엔진의 `SpriteComponent`에는 텍스처 참조 기능이 개발 중이므로, 샘플의 텍스처 출력은 GPU 렌더링 API를 사용한다. 관련 코드에도 이 경계를 주석으로 표시했다.

## 회귀 테스트와 캡처

```sh
ctest --test-dir build -L sample --output-on-failure
./build/bin/SampleBasic2D --frames 2 --capture basic.ppm
./build/bin/SampleForestSurvivor --frames 10 --capture title.bmp
```

게임의 `--smoke-test`, `--autoplay`, `--showcase`는 무인 검증을 위한 명시적 개발 옵션이다. 이 옵션을 지정한 경우에만 버튼을 누르지 않고 게임 상태를 진행한다. 일반 실행의 시작 방식은 바뀌지 않는다. 전체 옵션은 각 실행 파일의 `--help`로 확인한다. GPU 테스트에는 Vulkan 장치와 사용 가능한 창 시스템이 필요하다.

## CC0 에셋

Kenney의 Tiny Town, Tiny Dungeon, UI Pack Pixel Adventure, Kenney Fonts, Interface Sounds를 사용했다. 원본 `License.txt`, 공식 배포 페이지, 원본 압축 파일과 사용 파일의 SHA-256을 각 패키지의 `source-manifest.json`에 보존했다. 오디오의 OGG 원본도 보존하고, 엔진의 WAV 입력에 맞춘 변환 이력을 기록했다.

출처 목록은 `samples/Assets/LICENSES.txt`, 전체 패키지 목록은 `samples/Assets/assets.lock.json`에 있다. `Samples_cc0_assets` 테스트는 라이선스와 파일 해시를 검사한다. 원본 에셋에 줄바꿈 변환이 적용되지 않도록 `.gitattributes`에도 지정했다.

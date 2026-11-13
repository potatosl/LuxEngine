# 구조·파일 포맷 개편 검증

2026-11-13 Linux / GCC 15.3.1 / Qt 6 환경에서 확인했다. 실제 GPU 검증은 NVIDIA GeForce RTX 2060의 X11/Vulkan 환경에서 실행했다.

| 검증 | 결과 |
| --- | --- |
| Debug 정적 엔진·에디터·도구·샘플 전체 빌드 | 통과 |
| 모듈의 공개 헤더 단독 컴파일 | 통과 |
| 헤드리스 회귀 테스트 | 43/43 통과 |
| 실제 GPU 회귀 테스트, long 제외 | 28/28 통과, 건너뜀 없음 |
| 설치 SDK만 사용하는 독립 에디터 빌드 | 통과 |
| 독립 에디터의 헤드리스 회귀 테스트 | 21/21 통과 |
| Release 공유 엔진·에디터·샘플 빌드 | 통과 |
| 정적·공유 SDK의 외부 최소 게임 빌드 | 통과 |
| 정적·공유 SDK의 생성 게임 Debug/Release 실행·ZIP 패키징 | 통과 |
| SDK 외부 소비자의 엔진 Public API 사용 | 통과 |
| SDK 소비자의 Vulkan 내부·구형 Engine 경로·에디터 헤더 접근 | 세 경우 모두 컴파일 거부 |
| 샘플 씬 7개 JSON Schema 검증 | 통과 |
| CC0/Public Domain 원본 에셋 해시 검증 | 통과 |
| C++ 소스·헤더 163개 clang-format 검사 | 통과 |
| main 대비 git diff --check | 통과 |

## 확인 범위

- 기존 씬 버전 읽기와 새 JSON 저장, 모든 지원 컴포넌트·한글·계층·씬 참조의 왕복 직렬화를 확인했다.
- JSON의 잘못된 자료형, ID 오버플로, 중복 ID·키, 잘못된 계층, 미래 스키마 버전, 과도한 중첩, 알 수 없는 컴포넌트를 거부했다. 실패한 씬 읽기는 기존 엔티티 핸들과 데이터를 유지했다.
- 기존 확장자 프로젝트의 열기·가져오기·새 JSON 저장·백업 복구를 확인했다. 백업 복구는 사용자가 열었던 `.luxproj` 경로를 유지했다.
- 생성 게임의 한글 경로, 별도 시작 씬, OBJ·PNG·HDR 파일의 복사와 ZIP 포함을 확인했다.
- 공개 include 경로에서 저장소 루트와 Private 디렉터리를 제거했다. JSON 라이브러리 타입은 엔진 SDK 헤더에 노출하지 않았다.

## 재현

```sh
cmake -S . -B build/architecture -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DLUX_BUILD_EDITOR=ON -DLUX_BUILD_TESTS=ON
cmake --build build/architecture
ctest --test-dir build/architecture -L headless -LE long --output-on-failure
ctest --test-dir build/architecture -L gpu -LE long --output-on-failure
cmake --install build/architecture --prefix /tmp/lux-sdk
cmake -S Editor -B build/editor-sdk -G Ninja \
  -DCMAKE_PREFIX_PATH=/tmp/lux-sdk -DLUX_BUILD_TESTS=ON
cmake --build build/editor-sdk
ctest --test-dir build/editor-sdk -L headless -LE long --output-on-failure
LUX_ENGINE_DIR=/tmp/lux-sdk ctest --test-dir build/architecture \
  -R '^Editor_game_build$' --output-on-failure
```

GPU 테스트는 접근 가능한 데스크톱 세션이 필요하다. 이 환경에서는 샌드박스의 디스플레이 접근 제한을 벗어난 실행으로 확인했다. 생성 게임 검증은 설치된 중복 RenderDoc 레이어와 사용하지 않는 ICD의 경고가 결과를 가리지 않도록 테스트 데이터 경로를 격리하고 NVIDIA ICD를 지정했다. 엔진의 Vulkan 검증은 유지했다.

Windows·macOS 실기 검증과 원격 CI 실행은 수행하지 않았다. CI 정의는 이동한 도구 경로와 JSON 개발 패키지 설치 항목만 맞췄다.

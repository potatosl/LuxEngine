# 씬·프로젝트 JSON 포맷

씬 `.lux`와 프로젝트 `.luxproject`를 UTF-8 JSON으로 저장하도록 변경했다. 정식 식별자는 각각 `LuxEngine.Scene`, `LuxEngine.Project`이고 JSON 스키마 버전은 1이다. 문서 버전과 게임의 `project_version`, 엔진의 `engine_version`은 서로 다른 값이다.

YAML 대신 JSON을 선택했다. 숫자·문자열·불리언·배열의 자료형을 구분하고, 이름이 있는 필드로 Diff와 외부 도구 연동이 가능하도록 했다. 중복 키를 거부하고, 과도한 중첩과 범위를 벗어난 숫자를 차단했다. 문법·정적 구조는 `docs/schemas/`의 JSON Schema, 엔티티 참조·계층·파일 경로 등의 의미는 엔진과 에디터에서 검증한다.

## 씬

```json
{
  "format": "LuxEngine.Scene",
  "version": 1,
  "settings": { "...": "배경색·물리·3D 렌더 설정" },
  "entities": [
    {
      "id": 1,
      "components": {
        "tag": { "name": "플레이어" },
        "transform": {
          "position": [0, 0, 0],
          "rotation": [0, 0, 0],
          "scale": [1, 1, 1]
        }
      }
    }
  ]
}
```

위 예시의 settings는 설명을 위한 축약이다. 완전한 파일은 `samples/ScenePlayer/Scene.lux`와 `samples/Rendering3D/`에서 확인할 수 있다.

- 지원 컴포넌트는 태그, 변환, 스프라이트, 메시/텍스처 참조, 카메라, 2D/3D 리지드바디·박스 충돌체, 타일맵, 애니메이션, 계층, 씬 참조, 인스턴스 출처다.
- 엔티티 ID를 유지하고 ID 순서로 저장한다. 같은 작성 상태는 같은 JSON으로 저장된다.
- 지원하지 않는 컴포넌트는 조용히 버리지 않고 읽기·저장을 실패시킨다.
- Rigidbody type은 0=Static, 1=Dynamic, 2=Kinematic, 씬 참조 mode는 0=Nested, 1=Inherited, 2=Additive, render3d mode는 0=Legacy, 1=Pbr이다.
- 애니메이션 재생 시각 같은 일시적인 런타임 상태는 저장하지 않는다. 씬 참조의 baseline/suspended는 기존 오버라이드 복원용 직렬화 스냅샷 문자열이다.
- 씬 파일 최대 크기는 64 MiB, 엔티티는 100,000개, JSON 중첩은 64단계다. 키프레임은 클립당 4,096개, 타일맵은 256×256까지다.
- 읽기에 실패하면 기존 씬과 엔티티 핸들을 유지한다. 파일 저장은 기존 임시 파일·원자적 교체 경로를 사용한다.

## 프로젝트

```json
{
  "format": "LuxEngine.Project",
  "version": 1,
  "name": "새 게임",
  "engine_version": "0.1.0",
  "project_version": "0.1.0",
  "template": "empty",
  "start_scene": "Scenes/Main.lux",
  "settings": {
    "icon": "",
    "autosave_seconds": 120,
    "backup_limit": 10,
    "snap_to_grid": true
  },
  "dependencies": []
}
```

새 프로젝트의 파일명은 `project.luxproject`다. 기존 `project.luxproj`도 열 수 있고 최근 프로젝트 경로도 그대로 사용할 수 있다. 시작 씬은 프로젝트 내부의 상대 경로만 허용한다. 프로젝트 파일은 64 KiB, JSON 중첩은 16단계로 제한한다.

생성되는 게임은 `LuxEngine::Runtime`의 `ReadProjectStartScene`을 사용한다. 에디터와 게임이 서로 다른 파일 형식을 읽는 문제를 방지했다. 시작 씬을 정규 경로로 확인하고 심볼릭 링크를 통해 콘텐츠 디렉터리 밖으로 벗어나는 경우도 거부한다.

## 기존 파일과 마이그레이션

- 기존 `LuxEngineScene` v1~v6과 `LUXPROJECT` v1~v2를 읽을 수 있다. 이후 저장은 JSON으로 한다.
- 기존 프로젝트를 직접 열면 파일 경로를 임의로 바꾸지 않는다. `.luxproj` 확장자 안에 새 JSON을 저장할 수 있다.
- v1 프로젝트의 기존 백업·템플릿 보완 절차를 유지한다. v1 자동 변환 시 원본 `.v1.bak`을 만든다.
- 기존 프로젝트 압축 파일을 가져오면 파일명을 `project.luxproject`로 정규화한다. 두 이름의 프로젝트 파일이 함께 들어 있으면 모호한 패키지로 거부한다.
- 공개 include 경로도 바뀌었으므로 기존 사용자 게임 소스는 `<LuxEngine/...>`로 갱신해야 한다. 구형 생성 코드의 직접 작성된 프로젝트 파서는 `<LuxEngine/Runtime/ProjectManifest.hpp>`의 `ReadProjectStartScene` 호출로 교체해야 한다. 사용자 게임 코드를 자동으로 덮어쓰지는 않는다.
- 샘플 씬은 새 JSON으로 변환했다. 기존 포맷 회귀 테스트는 별도 테스트 데이터로 유지한다.

씬을 명시적으로 변환하려면 `LUX_BUILD_TOOLS=ON`으로 빌드한 도구를 사용한다.

```sh
LuxSceneConvert old.lux converted.lux
# 동일한 경로를 지정하면 원자적으로 교체한다.
LuxSceneConvert scene.lux scene.lux
```

## 개발 의존성

`nlohmann_json` 3.11 이상이 필요하다. CMake에서 다운로드하지 않는다. Fedora의 현재 패키지명은 `json-devel`, Homebrew는 `nlohmann-json`, vcpkg는 `nlohmann-json`이다. 커스텀 설치 경로는 `CMAKE_PREFIX_PATH` 또는 `nlohmann_json_DIR`로 지정한다.

이 파서는 엔진 공개 헤더에 노출하지 않는다. 설치 SDK를 사용하는 게임은 JSON 파서 개발 패키지를 별도로 찾을 필요가 없다. 독립적으로 에디터를 빌드하는 개발 환경에는 파서가 필요하다. MIT 라이선스 고지는 `docs/licenses/LICENSE-nlohmann-json.txt`에 포함했다.

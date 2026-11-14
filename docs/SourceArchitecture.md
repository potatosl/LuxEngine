# 소스 구조와 모듈 경계

루트의 제품 코드를 `Engine/`, `Editor/`로 구분했다. 실행 예제는 `samples/`, 개발·패키징 도구는 `tools/`, CMake 지원 코드는 `cmake/`, 문서와 라이선스 고지는 `docs/`에 두었다.

## 엔진

각 모듈은 `Public/`, `Private/`, `CMakeLists.txt`로 구성했다. 공개 헤더 경로에는 제품 이름을 포함했다.

```text
Engine/Scene/
  Public/LuxEngine/Scene/Scene.hpp
  Public/LuxEngine/Scene/Entity.hpp
  Private/Scene.cpp
  Private/ScenePhysicsWorld.hpp
  CMakeLists.txt
```

사용자는 `<LuxEngine/Scene/Scene.hpp>`를 include하고 `LuxEngine::Engine`을 링크한다. 런타임 API는 `<LuxEngine/Runtime/ScenePlayer.hpp>`와 `LuxEngine::Runtime`을 사용한다. 저장소 루트와 `Private/` 경로는 소비자에게 제공하지 않는다. SDK에는 각 모듈의 `Public/` 헤더만 설치한다. 구형 `Engine/...`, `Runtime/...` include 경로는 제거했다.

모듈은 별도 컴파일 타깃으로 의존성을 선언하고, 배포할 때는 기존 엔진 라이브러리 하나로 합친다. RHI와 ECS도 작은 값 객체의 구현을 별도 컴파일하고 의존성을 명시한다. 모든 모듈을 동적 플러그인으로 만드는 구조는 아니다.

- Core: 공통 자료형, 수학, 로그, 시간.
- Platform: 창과 입력. 현재 공개 입력 API에 SDL 타입이 있어 SDL 의존성을 공개했다.
- RHI: GPU 인터페이스와 값 객체의 선언·구현. 플랫폼별 백엔드 연결은 Renderer에 두었다.
- Assets: CPU 파일 읽기·검증·캐시와 GPU 인터페이스를 통한 업로드.
- Environment: 환경 이미지 전처리. 렌더러 구현에 의존하지 않는다.
- Renderer: 2D/3D 렌더링과 The Forge 연결. Assets/RHI/Environment/Platform을 사용한다.
- ECS, Animation, Audio: 각 기능의 공개 API와 구현.
- Physics: Qt/Scene과 독립된 Jolt 월드·컴포넌트·접촉 API. 2D와 3D 모두 Jolt를 사용한다. [JoltPhysics.md](JoltPhysics.md) 참고.
- Scene: 씬, 계층, 직렬화, 로딩과 씬 전용 Jolt 어댑터.
- Scripting: .NET 10 호스팅, 씬 수명과 C# 콜백 연결.
- Application: 프레임워크 진입점과 애플리케이션 수명 관리.
- Runtime: 실행 플레이어와 실행 설정.

`ScenePhysicsWorld`는 Scene을 사용하는 씬 전용 어댑터이므로 Scene 내부로 이동했다. The Forge 연결도 Renderer 내부에 두고 별도 `LuxForge` 공유 라이브러리를 로드한다. 이전 Native Vulkan 장치·할당기 구현은 제거했다. 기존 `<LuxEngine/Testing/GpuFault.hpp>`의 Native Vulkan 오류 주입은 현재 Forge의 기능이 아니다.

엔진 테스트는 `Engine/Tests/`, 활성 셰이더는 `Engine/Renderer/Private/Forge/Shaders/`, 브랜드·기본 리소스는 `Engine/Resources/`가 소유한다.

## 에디터

에디터는 설치된 엔진 SDK만으로도 빌드할 수 있다. 엔진 모듈은 Qt와 에디터 코드를 참조하지 않는다. 에디터는 기능별 디렉터리와 CMake 소스 목록을 소유한다. Qt 헤더와 네이티브 위젯 바인딩은 `Editor/LuxUI`가 소유하며 나머지 에디터는 `<LuxUI/...>`를 사용한다. 공통 창·애플리케이션·콘솔·프로파일러·명령 서비스는 [LuxUI.md](LuxUI.md)에 정리했다.

에디터 전용 헤더는 `Editor/<기능>/Private/LuxEditor/<기능>/`에 두고 `<LuxEditor/...>`로 참조한다. 이 include 경로는 에디터와 명시적으로 내부를 검사하는 테스트에만 제공한다. 에디터 모듈은 현재 `LuxEditorUi` 라이브러리로 함께 빌드한다. 기존 EditorWindow의 기능별 제어 책임을 독립 서비스로 분리하는 작업은 이번 파일 구조 변경과 구분한다.

## 변경 규칙

1. 공개 API는 Public, 구현 파일과 내부 헤더는 Private에 추가한다.
2. 프로젝트 API는 `<LuxEngine/...>` 또는 `<LuxEditor/...>`로 참조한다. 모듈 내부 구현 헤더는 로컬 include를 사용할 수 있다.
3. 다른 모듈의 Private 헤더를 참조하거나 저장소 루트를 include 경로로 추가하지 않는다.
4. 의존성은 해당 모듈의 CMakeLists.txt에 선언한다. 컴파일 오류를 전체 include 경로 추가로 우회하지 않는다.
5. 구조 변경 시 엔진·에디터·샘플 빌드와 설치 SDK 소비자 및 독립 에디터 빌드를 검증한다.

코드 포맷은 루트 `.clang-format`을 따른다. `SeparateDefinitionBlocks: Always`를 사용해 함수·타입 정의 사이에 빈 줄을 둔다. 생성 게임의 C++ 템플릿도 같은 간격으로 정리했다.

## 선언과 구현

일반 함수·생성자·접근자는 `.hpp`에 선언하고 `.cpp`에 구현했다. 임의 ECS 컴포넌트와 콜백 타입을 지원하는 템플릿, 컴파일 시 평가가 필요한 `constexpr` 생성자는 해당 헤더 옆의 `.inl`로 분리했다. 타입을 엔진의 내장 컴포넌트로 제한하지 않았다. 공개 `.inl`도 SDK에 설치한다.

기능을 분리할 때는 새 `.cpp`를 소유 모듈의 CMake 소스 목록과 해당 기능을 독립 컴파일하는 테스트 타깃에 함께 등록한다. 구현을 옮긴 일반 함수의 `inline` 선언은 제거해서 링크 심볼이 생성되도록 한다. C#은 같은 줄 중괄호와 함수 사이 빈 줄을 사용하며 `.editorconfig`로 규칙을 관리한다.

모델링의 메시 연산·OBJ 저장은 `Editor/Modeling`의 Qt 비의존 서비스로, 화면 선택·기즈모는 Viewport로 분리했다. UI 작업공간은 LuxUI를 사용한다. 세부 계약은 [Modeling.md](Modeling.md)에 기록했다.

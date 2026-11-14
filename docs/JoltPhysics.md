# Jolt 물리

엔진의 물리 백엔드를 Jolt Physics 5.6.0으로 통일했다. Box2D 의존성·씬 어댑터와 자체 적분/바닥 충돌 구현을 제거했다. 공개 `PhysicsWorld`는 프레임워크 앱에서도 사용할 수 있고, 씬은 같은 API를 사용하는 별도의 2D·3D 월드를 소유한다.

## 설치와 구성

프로젝트 CMake는 다운로드하지 않는다. 공식 Jolt의 `Build/`를 별도 빌드·설치하고 `Jolt_DIR` 또는 `CMAKE_PREFIX_PATH`, `LUX_JOLT_ROOT`로 설치 위치를 지정한다. 사용자 로컬 `~/.local`도 탐색한다. 준비한 기준은 공식 리비전 `e77f175595e64cb44218cc9d9d56fc365ad0e36a`이다.

```sh
cmake -S JoltPhysics/Build -B jolt-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_DEBUG_POSTFIX=d \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DTARGET_UNIT_TESTS=OFF -DTARGET_HELLO_WORLD=OFF -DTARGET_SAMPLES=OFF \
  -DTARGET_VIEWER=OFF -DTARGET_PERFORMANCE_TEST=OFF \
  -DJPH_USE_DX12=OFF -DJPH_USE_VK=OFF -DJPH_USE_MTL=OFF -DJPH_USE_CPU_COMPUTE=OFF \
  -DINTERPROCEDURAL_OPTIMIZATION=OFF -DOVERRIDE_CXX_FLAGS=OFF \
  -DDEBUG_RENDERER_IN_DEBUG_AND_RELEASE=OFF -DPROFILER_IN_DEBUG_AND_RELEASE=OFF
cmake --build jolt-debug --target install --parallel
```

Release도 별도 빌드 디렉터리에서 같은 prefix에 설치한다. Debug/Release 아카이브를 섞지 않는다. Windows는 `USE_STATIC_MSVC_RUNTIME_LIBRARY=OFF`로 엔진과 같은 동적 CRT를 사용한다. `tools/InstallDeps.ps1`과 `tools/ci/SetupUnixDependencies.sh`에 준비 절차를 반영했다. Jolt GPU 컴퓨트는 사용하지 않아 DXC가 필요하지 않다.

## 씬과 프레임워크

`<LuxEngine/Physics/PhysicsWorld.hpp>`는 Jolt 타입을 노출하지 않는다. 바디 설명을 동기화한 뒤 고정 시간으로 `Step`하고 `GetState`로 결과를 읽는다. `Contacts`는 현재 접촉, `BeganContacts`는 접촉 시작 이벤트를 제공한다. 메시·렌더러·Qt에 의존하지 않는다.

- 3D 박스 바디 사이의 충돌과 고정·동적·키네마틱·트리거를 Jolt가 처리한다. `BoxCollider3DComponent`만 있는 엔티티는 정적 충돌체다.
- 2D는 Jolt의 XY 평면 자유도 제약을 사용한다. 2D 렌더 순서용 Z는 씬에 보존하며 물리적 깊이로 사용하지 않는다. Box2D와 수치적으로 같은 결과를 보장하지 않는다.
- 씬은 월드 좌표에서 시뮬레이션하고 계층의 부모부터 로컬 Transform으로 환산한다. 크기·충돌체·질량·트리거 변경과 엔티티 삭제는 네이티브 바디에 반영한다.
- 바닥은 사용자가 배치한 정적 충돌체다. Y=0에서 물체를 멈추는 숨겨진 바닥은 없다.
- `Scene::GetCollisionEvents2D/3D`로 접촉 시작 이벤트를 읽는다. 3D 이벤트에는 월드 접점과 법선이 포함된다.

`PhysicsEngine::Step2D/Step3D` 단일 바디 호환 함수는 제거했다. 기존 프레임워크 앱은 `Scene::OnUpdate` 또는 `PhysicsWorld`로 옮긴다. 기존 Rigidbody/BoxCollider 씬 필드는 유지한다. 월드 좌표는 여전히 `float`이다.

현재 충돌체 어댑터는 박스를 지원한다. 구·캡슐·메시 충돌체·캐릭터 컨트롤러·조인트는 별도 확장 범위다. Jolt가 제공하는 모든 기능을 에디터에서 이미 노출했다는 뜻은 아니다.

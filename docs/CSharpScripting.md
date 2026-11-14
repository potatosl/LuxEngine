# C# 스크립트 기반

게임 스크립트 언어를 C#으로 정하고 .NET 10을 호스팅하는 기반을 추가했다. 엔진의 Scene·ECS는 C++로 유지했다. C#은 ABI 테이블을 통해 엔티티 수명과 Transform을 조회·변경하며, Scene의 원시 포인터를 보관하지 않는다.

## 준비와 빌드

.NET 10 SDK와 네이티브 호스팅 팩(`nethost.h`, `hostfxr.h`, `coreclr_delegates.h`, nethost 라이브러리)을 준비한다. CMake는 `dotnet` 실행 파일의 설치 루트에서 호스팅 팩을 찾는다. Fedora처럼 배포판 RID를 사용하는 SDK도 `dotnet --info`의 RID를 읽어 탐색한다.

일반 설치를 찾지 못하면 `LUX_DOTNET_EXECUTABLE`, `LUX_DOTNET_RID`, `LUX_DOTNET_HOST_INCLUDE_DIR`, `LUX_NETHOST_LIBRARY`로 경로를 명시할 수 있다. CMake 구성과 스크립트 빌드는 NuGet 패키지를 다운로드하지 않도록 했다.

빌드 결과는 실행 파일 옆의 `managed/`와 플랫폼별 nethost 라이브러리로 배치한다. 실행할 컴퓨터에는 호환되는 .NET 10 런타임도 필요하다. 런타임 설치 루트가 표준 경로와 다르면 `ScriptRuntimeConfig::dotnetRoot`를 지정할 수 있다.

## 스크립트 작성

```csharp
using LuxEngine;

namespace Game {
    public sealed class Mover : EntityScript {
        protected override void OnUpdate(double deltaTime) {
            Transform value = Entity.Transform;
            value.Position.X += (float)deltaTime;
            Entity.Transform = value;
        }
    }
}
```

`Entity`는 세계 ID와 엔티티 ID를 보관한 불변 참조 객체다. `Transform`은 값 스냅샷이므로 읽은 값을 편집한 뒤 다시 대입한다. `OnCreate`, `OnUpdate`, `OnDestroy`는 씬 소유 스레드에서 호출한다. `Entity.IsValid`는 엔티티 삭제와 씬 수명 종료를 반영한다.

스크립트 프로젝트는 `net10.0`을 대상으로 하고 `LuxEngine.ScriptCore.dll`을 참조한다. 생성 게임 템플릿은 `Scripts/GameScripts.csproj`, `Scripts/Mover.cs`, 오프라인 NuGet 설정과 `.editorconfig`를 제공한다. 사용자 코어 DLL을 게임 출력으로 중복 복사하지 않도록 참조의 `Private` 값을 `false`로 둔다.

C#도 C++과 같이 여는 중괄호를 같은 줄에 두고 함수 사이에는 빈 줄을 둔다. 저장소와 생성 프로젝트의 `.editorconfig`에 규칙을 기록했다.

## C++ 프레임워크 연결

```cpp
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scripting/ScriptRuntime.hpp>

LuxEngine::ScriptRuntime scripts;
LuxEngine::ScriptRuntimeConfig config;
config.assemblyPath = "Content/Scripts/GameScripts.dll";

auto entity = scene.CreateEntity("플레이어");
entity.AddComponent<LuxEngine::ScriptComponent>() = {"Game.Mover", true};
if (!scripts.Initialize(scene, config))
    throw std::runtime_error(scripts.GetLastError());

// 애플리케이션의 업데이트 안에서, scene.OnUpdate 이전에 호출한다.
if (!scripts.Update(deltaTime))
    throw std::runtime_error(scripts.GetLastError());

// 씬 파괴 또는 교체 전에 종료한다.
scripts.Shutdown();
```

`ScriptComponent`의 클래스 이름과 활성 상태는 씬 JSON과 계층 복제에 포함한다. `ScenePlayer`는 `--scripts 어셈블리경로`를 지원하며, 생략하면 실행 폴더의 `Content/Scripts/GameScripts.dll`을 사용한다. 스크립트가 붙은 플레이어에는 기존 내장 WASD 동작을 중복 적용하지 않는다.

## 수명과 오류 처리

각 런타임 세계는 별도 collectible `AssemblyLoadContext`를 사용한다. 스크립트 클래스 변경·활성 상태 변경·엔티티 삭제는 다음 업데이트에서 반영한다. 게임 어셈블리 컨텍스트는 종료 시 해제 요청하고, 프로세스에 로드한 .NET 호스트는 프로세스 종료까지 유지한다.

C# 예외는 네이티브 ABI 경계를 넘어 전파하지 않고 오류 문자열로 반환한다. 잘못된 스레드에서 업데이트하거나 수명이 끝난 씬에 접근하면 요청을 거부한다. 관리 코드에서 임의의 백그라운드 스레드로 씬·입력 API를 호출하는 흐름은 지원하지 않는다.

현재 연결한 범위는 수명 콜백, 엔티티 유효성, Transform, 키 입력 조회, 사용자 필드 Inspector 편집과 씬 직렬화다. 스크립트 디버거와 자동 컴파일·핫 리로드는 후속 개발 범위다. C++의 임의 ECS 컴포넌트와 템플릿 확장은 유지하고 필요한 구현만 `.inl`에 두었다.

## 회귀 테스트

`Engine_csharp`은 실제 .NET 호스팅으로 관리 스크립트를 실행한다. 생성·업데이트·종료 콜백, Transform 변경, 활성 상태 변경, 엔티티 삭제, 예외 격리, 재시작, 씬 수명 종료, 잘못된 스레드 호출, ScriptComponent 저장·로드를 검사한다. `CMake_dotnet_host_discovery`는 배포판 RID와 플랫폼별 호스팅 팩 후보의 선택을 검사한다.

## 사용자 필드와 Inspector

지원하는 필드 형식은 `bool`, `int`, `float`, `string`, `System.Numerics.Vector2/Vector3/Vector4`, `Entity` 참조다. public 인스턴스 필드 또는 `[SerializeField]`가 붙은 비공개 필드를 노출하며 static·readonly 필드와 속성은 편집하지 않는다. 지원하지 않는 노출 필드 형식이나 같은 이름의 숨겨진 필드는 진단 오류를 반환한다.

```csharp
public sealed class Player : EntityScript {
    public float Speed = 4;
    public int Health = 100;
    public Entity? Target;
    [SerializeField] private System.Numerics.Vector3 spawnOffset;
}
```

1. Details의 C# Script에서 스크립트 컴포넌트를 추가하고 `Game.Player`처럼 전체 클래스 이름을 지정한다.
2. 게임 스크립트를 빌드한 다음 **어셈블리에서 필드 읽기**로 `GameScripts.dll`을 선택한다. 이 동작은 타입 생성자를 한 번 호출해 필드 기본값과 형식을 조회하며 `OnCreate`를 실행하지 않는다. 지원 형식이 같은 기존 값은 유지하고 새 필드는 기본값으로 추가한다.
3. 형식별 입력으로 값과 엔티티 참조를 편집한다. 편집은 씬 Undo/Redo, 저장·열기, 계층 복사·붙여넣기에 연결된다.
4. 독립 플레이어나 생성 게임에서 스크립트가 붙을 때 저장한 값을 먼저 적용하고 `OnCreate`를 호출한다.

씬 JSON의 `script.fields`는 필드 이름별 `{type, value}` 레코드를 저장한다. type 값은 bool=0, int=1, float=2, string=3, Vector2=4, Vector3=5, Vector4=6, Entity=7이다. 최대 128개 필드, 이름 256 UTF-8 바이트, 문자열 16 KiB, 전체 필드 데이터 64 KiB를 허용한다. 숫자는 유한값만 허용하고 형식이 바뀐 필드·알 수 없는 필드 이름은 런타임에서 오류를 반환한다.

엔티티 참조는 씬 내부 ID로 저장하고 현재 런타임 세계의 Entity 핸들로 복원한다. 계층 가져오기와 프리팹 인스턴싱에서는 대상 ID로 재매핑하며 가져오기 범위 밖 참조는 0으로 비운다. 씬 참조 원본 갱신에서도 내부 참조를 재매핑하고, ID 차이만으로 사용자 재정의로 판단하지 않는다. 삭제된 대상의 핸들은 `IsValid`가 false를 반환한다.

실행 중 C++에서 authored 필드 값을 바꾸면 다음 업데이트에 해당 스크립트 인스턴스를 종료하고 새 값으로 다시 생성한다. 관리 스크립트가 자신의 런타임 필드를 변경하는 동작은 매 프레임 authored 값으로 덮어쓰지 않는다. 필드 스키마 조회와 편집은 자동 컴파일이나 핫 리로드 기능을 의미하지 않는다. 네이티브/관리 브리지 ABI는 2이며 엔진과 ScriptCore를 함께 빌드한다.

`Engine_csharp`은 실제 호스팅으로 필드 reflection, private 속성, OnCreate 이전 적용, 엔티티 참조, 씬 저장/로드, 가져오기 ID 재매핑과 형식 불일치 거부를 검사한다. `Editor_script_fields`는 실제 Qt 입력으로 정수·실수·벡터·엔티티 참조 편집, Undo/Redo와 저장·다시 열기를 검사한다.

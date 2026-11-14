# LuxUI

LuxEditor가 사용하는 Qt 6 기반 UI 라이브러리다. Qt 패키지 연결과 네이티브 헤더, 애플리케이션 수명, 도킹 패널·명령 생성, 콘솔·프로파일러·에셋 목록을 이 모듈이 소유한다. 에디터 기능 모듈은 `<LuxUI/...>`를 include하고 `LuxUI::LuxUI`를 링크한다.

`Widgets.hpp`의 기본 위젯·값 타입은 Qt 의미 체계를 유지하는 호환 바인딩이다. Qt와 독립된 ABI나 교체 가능한 렌더링 백엔드라고 주장하지 않는다. 기존 에디터의 신호·슬롯·그리기·입력 처리를 유지하면서 Qt 의존성을 한 경계로 모은 것이다. 새 서비스의 동작은 `.cpp`가 소유한다.

- `Application`: 실제 애플리케이션 클래스, 제품 식별 정보와 이벤트 루프를 관리한다.
- `MainWindow`: 실제 메인 창 클래스, 고유 ID의 패널과 명령을 생성한다. 도구 실행 예외는 `CommandFailed` 신호로 전달한다.
- `CommandRegistry`: 실행 가능 조건과 고유 ID를 가진 C++ 도구 등록·호출·해제를 제공한다. 콜백 실행 중 자기 명령을 해제할 수 있다.
- `MessageStore`: 메시지의 시각·수준·분류·순서를 저장하는 스레드 안전한 제한 크기 저장소다. 최근 기본 2,000개를 유지하고, 지운 뒤에도 순서 번호를 재사용하지 않는다.
- `ConsolePanel`: 로그를 다음 이벤트 루프에서 묶어 증분 갱신한다. 수준·검색 조건을 바꾸면 다시 필터링하고, 오래된 로그만 제거한다. 명령 입력과 현재 필터 결과의 UTF-8 저장을 제공한다.
- `FrameHistory`, `ProfilerPanel`: 최근 240개 업데이트·렌더 CPU 구간 샘플, 평균·P95, GPU 측정값·할당 크기·그리기 횟수와 CSV 저장을 제공한다. GPU 측정이 없으면 측정 불가로 표시한다. CPU 구간의 합은 전체 애플리케이션 CPU 사용률이나 GPU 사용률을 뜻하지 않는다.
- `AssetList`: 선택한 파일의 URL을 드래그 데이터로 전달한다.

```cpp
#include <LuxUI/AssetList.hpp>
#include <LuxUI/Widgets.hpp>

int main(int argc, char** argv) {
    LuxUI::Application app(argc, argv);
    app.SetIdentity("LuxEngine", "Tools", "0.1");
    LuxUI::MainWindow window;
    window.AddPanel("content", "콘텐츠", new LuxUI::AssetList(&window),
                    LuxUI::Flags::BottomDockWidgetArea);
    window.show();
    return app.Run();
}
```

위젯·패널·명령 레지스트리·프레임 기록은 UI 스레드에서 사용한다. 작업 스레드는 `MessageStore`로 기록하고 UI 스레드가 이후 순서 번호의 스냅샷을 읽는다. 서비스 객체보다 네이티브 부모 창이 먼저 파괴돼도 콘솔·프로파일러는 해제된 위젯을 다시 접근하지 않는다.

Qt는 공유 라이브러리로 연결한다. 애플리케이션과 메인 창의 메타오브젝트 코드는 LuxUI 타깃의 AUTOMOC가 생성한다. Qt 라이선스와 배포 고지는 기존 `docs/licenses/`가 소유한다.

회귀 테스트는 Qt 없는 데이터·명령 모델의 `LuxUI_framework`, offscreen Qt 패널·창 수명의 `LuxUI_panels`로 구분한다.

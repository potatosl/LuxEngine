# 에디터 씬 감사 결과

씬 편집·저장·실행 경계를 대상으로 확인한 결함과 수정 내용을 정리했다. 아래 표는 구현과 회귀 검증의 대응 관계를 보여 준다.

## 수정 항목과 회귀 검증

`Editor_scene_ui`의 괄호 안 이름은 `LuxEditorSceneUiTests <이름>`으로 개별 실행할 수 있는 테스트 묶음이다. 나머지 이름은 CTest에 등록한 테스트 이름이다.

| 번호 | 확인한 문제와 수정 내용 | 구현 근거 | 회귀 검증 |
| --- | --- | --- | --- |
| 1 | 씬 전환 시 편집 내용·선택·저장 여부와 Undo 대상이 섞이는 문제를 수정했다. 탭마다 고유 ID와 저장 기준 상태를 유지하고, 전역 Undo 명령이 원래 탭을 복원하도록 수정했다. 닫은 탭의 명령은 무효화하고 이력의 개수·메모리 사용량을 제한했다. | [EditorDocument](../Editor/Scene/EditorDocument.cpp), [씬 작업 공간](../Editor/App/EditorWindowScenes.cpp), [SceneHistory](../Editor/Scene/SceneHistory.hpp) | `Editor_scene_documents`, `Editor_scene_ui` (`tabs`, `settings`) |
| 2 | 씬 간 이동을 두 씬의 독립 작업으로 처리하면 한쪽만 되돌릴 수 있는 문제를 수정했다. 자식 계층과 ID 참조를 함께 이동하고, 원본·대상 스냅샷을 단일 Undo 명령으로 복원하도록 수정했다. | [씬 간 이동](../Editor/App/EditorWindowScenes.cpp), [엔진 계층·가져오기](../Engine/Scene/SceneHierarchy.cpp) | `Editor_scene_ui` (`move`), `Engine_scene_management` |
| 3 | 그룹 복사 시 자식이 빠지거나 기존 ID·부모를 참조하는 문제를 수정했다. 부분 계층 복사·붙여넣기에서 ID를 다시 매핑하고, 그룹화·해제 시 월드 배치를 보존하도록 수정했다. | [에디터 복사·붙여넣기](../Editor/App/EditorWindow.cpp), [SceneHierarchy](../Editor/Scene/SceneHierarchy.cpp) | `Editor_scene_ui` (`hierarchy`, `clipboard`), `Editor_sparse_copy` |
| 4 | 저장 실패가 기존 파일·문서 경로·dirty 상태를 훼손하는 문제와 다른 열린 씬을 Save As로 덮는 문제를 수정했다. 누락·순환 참조 때문에 편집 자료까지 열지 못하던 경계도 수정했다. 에디터는 저장된 편집 내용을 경고와 함께 열고 작성용 저장을 허용하며, 런타임의 엄격한 합성 검증은 유지했다. | [EditorDocument](../Editor/Scene/EditorDocument.cpp), [작성용 씬 IO](../Editor/Scene/SceneAuthoringIO.cpp), [원자 저장](../Engine/Scene/Scene.cpp) | `Editor_document`, `Editor_scene_documents`, `Editor_broken_scene_recovery` |
| 5 | 비활성 탭의 자동 저장과 세션 재시작 시 복구를 놓치는 문제를 수정했다. 탭별 프로젝트 소유권으로 자동 저장하고, 비활성 씬의 새 복구본도 한 번에 확인하도록 수정했다. 누락·순환 참조를 수리하는 중에도 자동 저장할 수 있고, 교체 실패 시 이전 복구본을 보존하도록 수정했다. | [세션 복원](../Editor/App/EditorWindow.cpp), [자동 저장](../Editor/App/EditorWindowProjects.cpp), [프로젝트 복구 파일](../Editor/Project/ProjectSettings.cpp) | `Editor_scene_ui` (`session-recovery`), `Editor_scene_documents`, `Editor_recovery_history`, `Editor_session_scene` |
| 6 | 늦게 끝난 비동기 로드가 이미 수정한 탭을 덮거나 프로젝트의 이전 시작 씬·설정을 되살리는 문제를 수정했다. 중복 탭은 재사용하고, 로드 세대·포커스·최신 프로젝트 소유권을 확인하도록 수정했다. | [비동기 씬 로드](../Editor/App/EditorWindowScenes.cpp), [프로젝트 설정 전파](../Editor/App/EditorWindowProjects.cpp) | `Editor_scene_ui` (`async`, `async-startup`), `Editor_scene_documents` |
| 7 | 미리보기 중 씬을 복제하면 실행 중 상태가 원본에 남고 복제본에 원본 스냅샷이 복원되는 문제를 수정했다. 미리보기를 종료해 편집 상태를 복원한 뒤 복제하고, 프로젝트 밖 복제본의 소유권을 분리하도록 수정했다. | [씬 복제](../Editor/App/EditorWindowScenes.cpp), [미리보기 복원](../Editor/App/EditorWindow.cpp) | `Editor_scene_ui` (`duplicate-preview`), `Editor_scene_documents` |
| 8 | 부모의 이동·회전·비균일 배율을 가진 오브젝트의 드래그와 타일맵 칠하기·지우기가 로컬 좌표를 잘못 사용하는 문제를 수정했다. 월드 좌표와 역행렬로 조작 위치를 계산하고 자식의 로컬 축을 보존하도록 수정했다. | [2D 뷰포트](../Editor/Viewport/SceneViewport2D.cpp), [월드 변환](../Engine/Scene/SceneHierarchy.cpp) | `Editor_scene_ui` (`viewport`), `Editor_picking2d`, `Editor_sparse_tilemap` |
| 9 | 임시 월드 행렬의 내부 배열 포인터가 행렬 수명보다 오래 남는 문제를 수정했다. 행렬을 지역 변수로 보관한 뒤 좌표 계산에 사용하도록 수정했다. | [2D 좌표 변환](../Editor/Viewport/SceneViewport2D.cpp) | `Editor_scene_ui` (`viewport`), `Editor_picking2d`; Debug·Release에서 검증했다. |
| 10 | 기본 3D 카메라가 지면 아래에서 바라보던 문제와 전단·반사 변환을 가진 오브젝트의 선택 오차를 수정했다. 기본 pitch를 수정하고 실제 affine 역행렬로 광선을 변환하도록 수정했다. 드래그 중 부모 변환이 특이·비정상 상태가 되어도 조작을 취소하고 편집 트랜잭션을 닫도록 수정했다. | [3D 카메라·교차 검사](../Editor/Viewport/ViewportCamera3D.cpp), [3D 뷰포트](../Editor/Viewport/SceneViewport3D.cpp) | `Editor_scene_ui` (`viewport3d`), `Editor_picking3d` |
| 11 | 스트리밍 종료 후 늦은 로드가 삭제한 루트를 되살리거나 캐시 삭제 후 작업 결과가 캐시를 다시 채우는 문제를 수정했다. 세대와 루트 생존 여부를 확인하고, 언로드 상태·인스턴스 수정·거리 히스테리시스를 보존하도록 수정했다. 변경 없는 업데이트가 엔티티 핸들을 무효화하지 않도록 수정했다. | [SceneManager](../Engine/Scene/SceneManager.cpp), [런타임 스트리밍](../Runtime/ScenePlayer.cpp) | `Engine_scene_loading`, `Engine_scene_management` |
| 12 | 창 종료 시 미리보기·타이머·게임 자식 프로세스의 정리 경로를 보강했다. 빈 콘솔의 안내 문구와 일반 편집의 불필요한 로그를 제거하고, 같은 스트리밍 오류를 재시도마다 반복 출력하던 문제를 수정했다. | [창 수명·콘솔](../Editor/App/EditorWindow.cpp), [게임 프로세스 종료](../Editor/App/EditorWindowPlay.cpp), [스트리밍 로그](../Editor/App/EditorWindowScenes.cpp) | `Editor_ui`, `Editor_ui_1000`; `Editor_scene_ui`의 `tabs`에서 빈 콘솔·편집 로그를, `async-startup`에서 실행·중지 경로를 검증했다. |
| 13 | 개발 환경에서는 열리지만 배포본에는 없는 외부 씬 참조가 빌드·내보내기로 넘어가는 문제를 수정했다. 직접·전이·baseline·suspended 참조와 심볼릭 링크를 검사하고 프로젝트 경계를 확인하도록 수정했다. 검증 한도는 프로젝트 총합이 아닌 씬 그래프마다 적용하도록 수정했다. | [프로젝트 참조 검증](../Editor/Scene/SceneDependencies.cpp), [빌드 전 검증](../Editor/App/EditorWindowBuild.cpp) | `Editor_scene_dependencies`: 누락·외부·숨은 참조·깊이 제한·실패 시 보존·총 132 MiB의 독립 씬 프로젝트를 검증했다. |
| 14 | 시작 씬 설정과 생성 게임의 실제 시작 경로가 어긋나고, 기본 `Scenes/Main.lux` 이외의 씬이 배포에서 빠지는 문제를 수정했다. 프로젝트 메타데이터에 시작 씬을 저장하고, 생성 게임이 manifest를 읽으며 작성한 씬을 `Content/`에 복사하도록 수정했다. | [시작 씬 저장](../Editor/Project/EditorProject.cpp), [게임 템플릿](../Editor/Project/ProjectTemplates.cpp), [게임 빌드 회귀](../Editor/Tests/EditorGameBuildTest.cpp) | `Editor_scene_documents`, `Editor_game_build`: 기본 씬을 삭제하고 별도 경로의 한국어 이름 씬으로 Debug·Release 실행과 ZIP 배포를 검증했다. |
| 15 | 같은 프로젝트를 다시 열거나 백업을 복구할 때 기존 시작 씬 탭과 경로가 충돌해 작업이 실패하는 문제를 수정했다. 별도 문서에서 후보 씬을 불러온 뒤 성공한 경우에만 작업 공간을 교체하도록 수정했다. 실패한 후보가 기존 탭·프로젝트 상태를 바꾸지 않도록 수정했다. | [프로젝트 다시 열기·복구](../Editor/App/EditorWindowProjects.cpp), [프로젝트 상태 회귀](../Editor/Tests/EditorStateTest.cpp) | `Editor_project_reopen`, `Editor_project_restore`; 기존 `Editor_scene_context`, `Editor_recovery_history`, `Editor_broken_scene_recovery`, `Editor_session_scene`도 함께 검증했다. |
| 16 | 속성·저장 대화상자가 열린 동안 비동기 로드가 완료되면 편집 대상 씬이 바뀔 수 있는 문제를 수정했다. 새 탭 채택과 이미 열린 탭 재사용 두 경로 모두 모달 대화상자 중에는 현재 탭을 유지하도록 수정했다. | [비동기 완료의 포커스 보호](../Editor/App/EditorWindowScenes.cpp), [모달 중 비동기 회귀](../Editor/Tests/EditorSceneUiTest.cpp) | `Editor_scene_ui` (`async`): 새 파일 완료와 이미 열린 파일의 늦은 완료에서 대화상자·현재 탭·수정 내용을 보존하는지 검증했다. |
| 17 | `WaitIdle`만으로 present 자원의 파괴 가능 시점을 판단하던 종료 계약의 공백을 수정했다. 지원 환경에서 swapchain maintenance feature를 조회·활성화하고, 슬롯별 present fence를 재사용·재생성·종료 전에 확인하도록 수정했다. 실제 큐에 들어간 WSI 오류와 미호출·메모리 부족을 구분하고, 부분 생성 실패 정리와 미지원 환경의 기존 경로를 유지했다. 최초 한 차례 발생한 `DEVICE_LOST`의 원인으로 확정한 것은 아니다. | [확장·feature 활성화](../Engine/Renderer/Vulkan/VulkanContext.cpp), [present 추적](../Engine/Renderer/RHI/GpuDevice.cpp), [슬롯·종료 정리](../Engine/Renderer/Vulkan/VulkanResources.cpp), [swapchain 재생성](../Engine/Renderer/Vulkan/VulkanSwapchain.cpp) | `Engine_present_completion`: 지원·강제 미지원, 슬롯 재사용, resize, 큐 제출 후 OUT_OF_DATE, 미제출 OOM, device loss, 부분 fence 생성 실패를 검증했다. X11·Wayland의 해당 회귀 직접 실행을 통과했다. |

출력 자원 해제 시점의 근거는 [Khronos Vulkan Guide](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html#_vk_ext_swapchain_maintenance1_extension)와 [present fence 계약](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html)에 있다. 미지원 장치는 기존 Vulkan 호환 경로를 사용한다.

## 확인한 실행 범위

- Debug headless 테스트 40/40을 통과했다. `Editor_ui_1000`을 포함했다. `Engine_audio`도 독립 실행으로 1/1 통과했다.
- Release 전체 빌드를 처음부터 다시 완료하고 headless 40/40을 통과했다. 엔진·샘플·런타임 20개와 에디터 20개를 포함하며, 프로젝트 재열기·복구와 두 비동기 모달 경로도 검증했다.
- 설치한 SDK로 에디터를 독립 빌드하고 에디터 테스트 20/20을 통과했다.
- 설치한 SDK로 생성 게임을 Debug·Release 빌드해 실행하고 ZIP 결과물을 확인한 `Editor_game_build` 1/1을 통과했다. [MinimalGame](../examples/MinimalGame/main.cpp) SDK 소비 예제도 빌드·실행했다.
- X11 GPU 23개를 실행했다. 전체 실행의 22개가 통과했고, 창 복원 자동화 1개는 실제 표시·활성화 요청과 이벤트 동기화 보강 후 10회 연속 및 최종 독립 실행을 통과했다. 새 present 회귀도 단계별 검증을 추가한 최종 실행에서 통과했다.
- Wayland의 최종 선택·계층·런타임·Vulkan·present 회귀 7/7을 통과했다. 수정 후 에디터 GPU 생명주기와 뷰포트 종료 반복도 X11·Wayland 각각 100/100을 통과했고 검증 오류·GPU 할당 증가가 없었다.
- 실행 환경은 Linux, NVIDIA RTX 2060, 드라이버 580.178.04다. Windows·macOS 실기 검증과 CI 결과는 포함하지 않았다.

## 추가로 보존한 GPU 관찰

초기 확대 검사에서 다른 뷰포트 장치를 종료한 뒤 `DEVICE_LOST`가 한 차례 발생했다. 수정 전 동일 조건 재실행과 별도 종료 순서 검사 240회에서는 재현되지 않았고, present 수명 보강 후의 양쪽 백엔드 반복 검사도 통과했다. 최초 오류의 원인은 미확정으로 남겼다. 이를 present 수명 수정의 인과관계가 확정된 크래시로 분류하지 않았으며, 재발 시 단계·장치 상태·카운터를 남기는 진단을 보강했다.

별도로 창 최소화 상태 고착은 GPU를 사용하지 않는 SDL 검사에서도 재현했다. 실제 표시·활성화 요청을 추가한 창 복귀 검사 100회와 `Engine_vulkan` 10회 연속 실행을 통과했다. 렌더링 복귀 단언은 유지했다.

회귀 코드는 [씬 문서 검사](../Editor/Tests/EditorSceneDocumentTest.cpp), [씬 UI 검사](../Editor/Tests/EditorSceneUiTest.cpp), [배포 참조 검사](../Editor/Tests/EditorSceneDependencyTest.cpp), [씬 관리자 검사](../Tests/SceneManagerTest.cpp)에 모았다. CTest 등록은 [에디터 테스트 목록](../Editor/CMakeLists.txt)과 [엔진·런타임 테스트 목록](../Tests/CMakeLists.txt)에서 확인할 수 있다.

# FBX와 기본 모델

FBX 파서는 ufbx 0.23.1을 사용한다. 공식 태그의 커밋 `26a482ae66871d7de36eb722aa060bce95bce274`를 고정했고, 원본 C 소스·헤더·라이선스와 파일 해시를 `Engine/ThirdParty/ufbx/`에 보존했다. MIT 라이선스를 선택했다. CMake는 이 로컬 소스를 컴파일하며 다운로드하지 않는다. 파서 타입과 별도 링크 의존성을 엔진 공개 API에 노출하지 않는다.

참고: [ufbx 공식 저장소](https://github.com/ufbx/ufbx), [공식 변환 문서](https://ufbx.github.io/elements/nodes/).

## 가져오기 범위

`AssetManager::LoadMesh()`·`ValidateMeshFile()`·`ReloadMesh()`에서 OBJ와 FBX를 같은 자산 경로로 다룬다. 확장자는 대소문자를 구분하지 않는다.

| 데이터 | 현재 처리 |
| --- | --- |
| ASCII·바이너리 FBX | ufbx로 읽는다. |
| 여러 메시·메시 인스턴스 | 노드별 geometry-to-world 변환을 적용해 하나의 정적 메시로 합친다. |
| 축·단위·피벗 | 오른손 좌표계·Y-up·미터 단위로 변환한다. 노드와 geometry 변환을 함께 적용한다. |
| 다각형·법선·UV | 삼각형으로 변환하고 UV 경계와 법선을 보존한다. UV의 하단 원점을 엔진 텍스처의 상단 원점으로 정규화한다. 음수 스케일에서는 면 방향을 보정한다. |
| 파일 재질·외부 텍스처 | 자동으로 엔진 재질을 만들지 않는다. Details에서 설정한다. 기본 모델은 함께 제공한 colormap을 사용한다. |
| 원본 노드 계층 | 메시의 정적 위치에 반영한다. 엔티티 부모·자식 구조로 인스턴스화하는 기능은 아직 개발 중이다. |
| 스킨·모프·geometry cache 변형 | 엔진 연결이 아직 개발 중이다. 잘못된 정적 결과를 만들지 않도록 거부한다. |
| 노드 애니메이션 | 기본 자세의 정적 geometry만 가져온다. 클립 가져오기·런타임 재생은 아직 개발 중이다. |

ufbx 자체의 기능 목록이 LuxEngine의 지원 목록은 아니다. 이번 경로는 `RawMeshData`를 만드는 정적 메시 가져오기이며 FBX 전체 씬·리깅·애니메이션 지원 완료를 뜻하지 않는다.

파일은 64 MiB, 파서 임시/결과 메모리는 각각 128 MiB, 노드 깊이는 256, 출력은 정점 100만 개·인덱스 300만 개로 제한한다. 파일 스냅샷·유한 좌표·변환·경계를 검증하고 외부 파일 자동 로딩을 끈다. 실패한 재가져오기는 기존 정상 캐시와 리비전을 교체하지 않는다.

## 기본 도형

[Kenney Prototype Kit](https://kenney.nl/assets/prototype-kit) 1.0의 CC0 원본을 사용한다. Kenney의 [직접 게시 페이지](https://opengameart.org/content/prototype-kit)에서 받은 ZIP 중 다음 FBX 10개와 텍스처·라이선스를 선택했다.

| 모델 | 원본 파일 |
| --- | --- |
| 큐브 | `shape-cube.fbx` |
| 반쪽 큐브 | `shape-cube-half.fbx` |
| 둥근 큐브 | `shape-cube-rounded.fbx` |
| 원기둥 | `shape-cylinder.fbx` |
| 세밀한 원기둥 | `shape-cylinder-detailed.fbx` |
| 반쪽 원기둥 | `shape-cylinder-half.fbx` |
| 육각 기둥 | `shape-hexagon.fbx` |
| 경사면 | `shape-slope.fbx` |
| 삼각 기둥 | `shape-triangular-prism.fbx` |
| 바닥 | `floor-square.fbx` |

구·원뿔·캡슐·토러스는 이 묶음에 포함되지 않는다. 원본 파일과 `Textures/colormap.png`, `License.txt`, 출처·해시를 기록한 `Provenance.json`은 `Engine/Resources/Models/Primitives/KenneyPrototype/`에 있다. FBX와 텍스처 원본 바이트는 수정하지 않았다.

Game Asset Vendoring 도구가 FBX 패키지를 지원하지 않아, 사용자가 승인한 예외로 원본 ZIP과 선택 파일의 출처·CC0·크기·SHA256을 직접 검증했다. 이 기록은 해당 도구의 canonical receipt가 아니다.

## 에디터와 프레임워크

`LuxEngine::BasicModelLibrary`는 모델 목록, 리소스 위치 탐색, 프로젝트 내부 설치 API를 제공한다. 에디터는 이 엔진 API로 기본 모델을 배치한다. 프로젝트에는 `Assets/Models/Primitives/` 아래에 모델·텍스처·라이선스를 함께 복사하고, 다른 내용의 기존 파일을 덮어쓰지 않는다. 동일한 패키지는 재사용한다.

기본 모델 생성과 프로젝트 모델 배치는 일반 씬 편집 이력을 사용한다. Undo/Redo는 엔티티 편집을 복원하며 공유 에셋 파일은 프로젝트에 남겨 재사용한다. 임의의 OBJ·FBX는 Content Browser와 메시 선택 창에서 사용할 수 있다.

빌드 시 기본 모델을 실행 파일 옆 `Resources/Models/`에 준비한다. 설치 SDK는 `share/LuxEngine/models/`와 `bin/Resources/Models/`에 포함한다. SDK의 `lux_copy_runtime()`은 기본 리소스와 프로젝트 FBX 콘텐츠도 게임 실행 폴더로 복사한다. Windows 에디터·플레이어 배포 도구도 모델 리소스를 보존한다.

## 검증

에셋 원본의 무결성은 다음 명령으로 검사한다.

```sh
python3 tools/VerifyBasicModels.py --json
python3 -m unittest discover -s tools/Tests -p test_basic_models.py -v
```

원본 ZIP을 보유했다면 `--source-archive`로 ZIP과 선택 파일의 동일성도 검사할 수 있다. 이 검사는 파일의 무결성을 확인하며 엔진 가져오기나 GPU 렌더링 성공을 대신하지 않는다. 가져오기·캐시·편집·렌더링은 별도 회귀 테스트로 검사한다.

Linux 로컬에서 정적 엔진의 headless 회귀 57개와 에셋 무결성 검사, 공유 엔진의 FBX·기본 모델 회귀 2개를 통과했다. 설치 SDK만 참조하는 별도 소비자에서 기본 리소스 탐색과 프로젝트 FBX 10개 복사·가져오기도 확인했다. Windows 배포 파일 검사는 PowerShell 모의 패키지 회귀로 확인했으며 Windows·macOS 실제 빌드 완료를 의미하지 않는다.

실제 NVIDIA Vulkan 렌더링에서는 10종의 표시·색상 텍스처·밝기·draw call·검증 오류를 검사했다. 최초 검정 샘플 문제는 FBX와 엔진의 UV 원점 차이로 재현했고, 보정 전후 동일한 카메라·조명에서 다시 캡처했다. 저장된 원본 캡처의 비교 도구는 이미지와 로그의 해시·봉인을 검사할 뿐 GPU 실행·완료·타이밍을 대신 증명하지 않는다. 원격 CI 결과 확인은 사용자의 보류 요청을 유지한다.

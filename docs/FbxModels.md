# FBX와 기본 모델

FBX 파서는 ufbx 0.23.1을 사용한다. 공식 태그의 커밋 `26a482ae66871d7de36eb722aa060bce95bce274`를 고정했고, 원본 C 소스·헤더·라이선스와 파일 해시를 `Engine/ThirdParty/ufbx/`에 보존했다. MIT 라이선스를 선택했다. CMake는 이 로컬 소스를 컴파일하며 다운로드하지 않는다. 파서 타입과 별도 링크 의존성을 엔진 공개 API에 노출하지 않는다.

참고: [ufbx 공식 저장소](https://github.com/ufbx/ufbx), [공식 변환 문서](https://ufbx.github.io/elements/nodes/).

## 모델 가져오기와 런타임

`ModelAsset::LoadFbx()`는 FBX 모델 구조를 보존하는 API다. 노드의 부모·자식 관계와 로컬 TRS, 재질별 하위 메시, 텍스처 참조·내장 이미지, 본·가중치·바인드 행렬, 애니메이션 클립을 제공한다. 노드의 geometry 변환은 해당 노드의 로컬 정점에 한 번 적용한다. 모델의 축은 오른손 Y-up, 길이는 미터, 회전은 XYZ 라디안으로 정규화한다.

`ModelAsset::Evaluate()`는 클립 시작점을 기준으로 한 초 단위 시각을 받는다. 노드 변환·스키닝·모프를 CPU에서 평가하고 노드 로컬 정점을 돌려준다. ufbx의 씬 평가 경로로 FBX 회전 순서·피벗·레이어를 처리한다. 결과를 재질별 메시로 제출하고, 동일한 토폴로지의 애니메이션 정점은 기존 GPU 버퍼를 갱신한다. 인스턴스마다 메시 캐시를 구분해 서로 다른 재생 시각이 덮어쓰이지 않는다.

| 데이터 | 처리 |
| --- | --- |
| ASCII·바이너리 FBX | 고정한 ufbx 파서로 읽는다. |
| 계층·여러 메시·인스턴스 | 노드와 재질별 메시로 보존해 씬 엔티티로 배치한다. |
| 축·단위·피벗·geometry 변환 | Y-up·미터·노드 로컬 정점으로 정규화한다. |
| 다각형·법선·UV | 삼각형 분할, 법선 생성, UV 경계 보존과 텍스처 원점 보정을 적용한다. |
| 재질·텍스처 | 기본 색상·금속성·거칠기와 엔진이 지원하는 텍스처 슬롯으로 변환한다. 내장 이미지는 프로젝트에 추출한다. |
| 스켈레톤·스킨 | 본·가중치·바인드 행렬을 보존하고 CPU 변형 결과를 렌더링한다. |
| 모프 | ufbx의 평가 결과를 정점에 적용한다. |
| 애니메이션 | 클립 선택, 반복·속도·재생 시각과 노드 변환을 씬 데이터에 저장한다. |
| 재가져오기 | 변경된 파일을 다시 읽고 모델 계층을 갱신한다. 실패하면 정상 씬 상태를 유지한다. |
| 외부 정점 캐시·DCC 전용 셰이더·제약 조건 | 전용 런타임은 제공하지 않는다. 외부 파일 자동 로딩은 끄고 경고를 제공한다. 필요한 결과는 내보내기 도구에서 애니메이션으로 Bake한다. |

기존 `AssetManager::LoadMesh()`·`ValidateMeshFile()`·`ReloadMesh()`의 OBJ/FBX 경로는 정적 병합 메시 API로 유지한다. 이 경로에서는 노드 변환을 월드 정점에 적용하며 스킨·모프 FBX는 거부한다. 구조·애니메이션 모델에는 `ModelAsset`과 `MeshRendererComponent::modelSubmesh`, `ModelAnimationComponent`를 사용한다. 임의의 DCC 기능이나 모든 FBX 파일의 호환성을 보장하는 의미의 “완벽 지원”을 주장하지 않는다.

파일은 64 MiB, 파서 임시/결과 메모리는 각각 128 MiB, 노드 깊이는 256, 출력은 정점 100만 개·인덱스 300만 개로 제한한다. 파일 스냅샷·유한 좌표·변환·경계를 검증하고 외부 파일 자동 로딩을 끈다. 실패한 정적 메시 재가져오기는 기존 정상 캐시와 리비전을 교체하지 않는다. 구조 모델은 경로·크기·수정 시간으로 캐시를 갱신한다.

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

기존 정적 FBX 도입 시 Linux 로컬에서 headless 회귀 57개와 에셋 무결성 검사, 공유 엔진의 FBX·기본 모델 회귀 2개를 통과했다. 설치 SDK만 참조하는 별도 소비자에서 기본 리소스 탐색과 프로젝트 FBX 10개 복사·가져오기도 확인했다. Windows 배포 파일 검사는 PowerShell 모의 패키지 회귀로 확인했으며 Windows·macOS 실제 빌드 완료를 의미하지 않는다.

실제 NVIDIA Vulkan 렌더링에서는 10종의 표시·색상 텍스처·밝기·draw call·검증 오류를 검사했다. 최초 검정 샘플 문제는 FBX와 엔진의 UV 원점 차이로 재현했고, 보정 전후 동일한 카메라·조명에서 다시 캡처했다. 저장된 원본 캡처의 비교 도구는 이미지와 로그의 해시·봉인을 검사할 뿐 GPU 실행·완료·타이밍을 대신 증명하지 않는다. 원격 CI 결과 확인은 사용자의 보류 요청을 유지한다.

이번 구조화 FBX 확장은 자체 작성한 ASCII fixture로 계층·단위 변환·다중 재질·내장 텍스처·스킨·모프·클립·잘못된 입력과 씬 왕복을 검사했다. 에디터 회귀는 계층 배치·재가져오기·ID/사용자 자식 유지·Undo/Redo·현재 포즈의 경계와 삼각형 선택·독립 애니메이션 작업공간을 검사한다. 음수 월드 스케일은 CW/CCW 파이프라인을 선택하고 양면 재질의 뒷면 조명 법선을 보정했다.

Linux 헤드리스 회귀 82개와 수정 후 에디터 관련 42개 재검사를 통과했다. NVIDIA Vulkan GPU 회귀 5개는 기본 모델 10종, CPU 스키닝 인스턴스 분리·정점 갱신, 한 씬 내 4096개 캐시 항목을 넘는 메시 수명, 미러/양면 재질, Game View 주 카메라, 일반/HiDPI 선택을 확인했다. 설치한 SDK만 링크한 외부 소비자에서 Jolt·ModelAsset·모델 씬 직렬화를 검증했다. Windows·macOS 실행이나 원격 CI 검증 결과는 포함하지 않는다.

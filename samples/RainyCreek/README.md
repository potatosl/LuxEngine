# Rainy Creek

Atanara Engine의 Rainy Creek 장면을 LuxEngine의 **현재 공개 API**로 새로 구현한 3D 샘플이다. 구불구불한 개울, 숲 바닥, 이끼 바위 42개, 양치식물 100개, 소나무 18개, 쓰러진 나무 3개와 따뜻한 가로등 2개를 배치했다. 실행하면 바로 장면이 나온다.

원본 엔진의 C++·셰이더·렌더 패스·에셋 준비 스크립트는 반입하지 않았다. 사용자가 재사용을 허용한 Poly Haven의 CC0 데이터만 검증하고, 표준 glTF/JPEG/HDR에서 OBJ/PNG/HDR로 독립 변환했다. 원본 엔진이나 개발자의 로컬 경로는 실행·빌드에 필요하지 않다.

## 실행

기존 엔진 구성에서 다음 타깃을 빌드한다. CMake는 에셋을 다운로드하지 않는다.

```sh
cmake --build out --target SampleRainyCreek --parallel 10
./out/bin/SampleRainyCreek
```

멀티 구성 생성기는 `--config Debug`를 추가하고 `out/bin/Debug/`의 실행 파일을 사용한다. SDL과 The Forge를 사용하는 기존 엔진의 Windows/D3D12, macOS/Metal, Linux/Vulkan 경로를 그대로 사용한다. 플랫폼별 실행 여부는 해당 환경에서 별도로 검증해야 한다.

```sh
./out/bin/SampleRainyCreek --validate-assets
./out/bin/SampleRainyCreek --smoke-test
./out/bin/SampleRainyCreek --time 2.5 --frames 48 --capture rainy-creek.png
./out/bin/SampleRainyCreek --preset dusk
```

`--assets PATH`로 다른 배포 위치를 지정할 수 있다. 모든 기본 에셋은 실행 파일 옆 `assets/rainy-creek/`으로 복사한다. 화면 UI와 폰트 의존성은 없다. `--help`는 창이나 GPU를 초기화하지 않는다.

## 조작

| 입력 | 동작 |
| --- | --- |
| 왼쪽 드래그 / 가운데 드래그 | 궤도 회전 / 화면 이동 |
| 마우스 휠 | 확대·축소, 5~24m 제한 |
| Space | 시뮬레이션 일시정지·계속 |
| R / Esc | 시점과 설정 초기화, 시뮬레이션 시간은 유지 |
| L / P / S | 밤·황혼·낮 조명 전환 / 가로등 / 그림자 |
| W / H / G | 비 / 수면 / 바람 |
| J | 거리별 LOD |
| + / - | 노출 조정 |
| 창 닫기 | 종료 |

일시정지 중에도 카메라는 조작할 수 있다. 실행 즉시 장면을 표시하며, 화면 위 패널·버튼·텍스트는 없다.

## 현재 API로 표현한 범위

PBR·HDR 환경광·하늘·방향광·점광원·그림자·톤 매핑을 연결했다. 식생은 원본 albedo와 알파를 합친 이진 컷아웃 텍스처로 표시한다. 모델을 재사용하고 거리에 따라 소나무와 통나무의 LOD를 전환한다. 모델·비·수면은 같은 3D 패스의 깊이를 사용하므로 앞의 지형이나 물체가 뒤의 빗줄기를 가린다.

현재 API에 없는 기능은 `Private/CreekRenderer.cpp`와 `Private/CreekGeometry.cpp`의 `TODO`에 구현 요구를 남겼다. 원본과 같은 렌더링 결과를 완료했다고 표시하지 않는다.

| 필요한 엔진 API | 현재 샘플 구현 |
| --- | --- |
| 투명 패스·물 전용 반사/굴절·수심 | 저거칠기 불투명 수면과 IBL 하이라이트 |
| 깊이 검사를 유지하는 알파 블렌딩 | 가느다란 불투명 3D 빗줄기 |
| 정점 버퍼 부분 갱신 | 초기화 때 만든 물결 12단계·비 24단계 메시 선택 |
| 재질 normal/roughness/AO 슬롯 | albedo 텍스처와 상수 거칠기 |
| alpha cutoff·양면 식생 설정 | alpha 0/255로 변환한 텍스처, 현재 엔진의 컷아웃 |
| specular/IOR·Repeat 샘플러·밉맵 | 현재 재질 기본값과 Clamp 샘플러, 일부 bark UV 늘어짐·먼 침엽의 계단 현상 |
| 식생 정점 바람 | 뿌리를 기준으로 작은 인스턴스 회전 |
| emissive·bloom | 가로등 재질과 실제 PointLight |
| 거리/높이 안개·SSAO·SSR·TAA | 미구현, 동작하지 않는 효과 버튼은 제공하지 않음 |

밤 조명은 황혼 HDR의 환경광 강도를 낮춰 사용한다. 낮 조명은 흐린 낮 HDR을 사용한다. 실제 야간 촬영 HDR이나 원본의 별도 대기 효과를 제공하는 것은 아니다.

## 구조와 검증

`Public/RainyCreek/`는 선언, `Private/`는 구현, `Tests/`는 회귀 테스트다. 엔진 코드는 변경하지 않는다.

```sh
ctest --test-dir out -R 'RainyCreek|SampleRainyCreek' --output-on-failure
python3 tools/PrepareRainyCreekAssets.py --verify
```

카메라 입력의 유한값·각도/거리 제한·프레임 분할 독립성, 지형/물결/비의 인덱스·법선·주기, 일시정지와 초기화 계약, 에셋 경로·파싱·이미지를 검사한다. GPU 스모크는 카메라·효과 토글·조명 전환·창 크기 변경을 실제로 그리며 GPU 검증 오류를 확인한다. `--time`을 사용하면 캡처의 물결·빗줄기·바람 시간을 고정할 수 있다.

## 에셋 출처와 재생성

출처·저자·CC0 라이선스는 `Assets/LICENSE.txt`, 입력/출력 SHA-256·변환 설정·도구 버전은 `Assets/Provenance.json`, 입력 허용 목록은 `tools/RainyCreekSources.json`에 기록했다. 별도 엔진 전용 `.gmesh/.gtex`는 사용하지 않는다. 이 기록은 독립 검증 기록이며 game-dev 정규 패키지 영수증이 아니다.

일반 빌드에는 변환 도구가 필요 없다. 에셋을 재생성하는 개발 작업에만 NumPy·Pillow·meshoptimizer·OpenCV가 필요하다. 준비 도구는 다운로드를 하지 않으며, 입력 목록에 없는 파일과 심볼릭 링크를 거부하고 새 출력 디렉터리만 만든다.

```sh
python3 tools/PrepareRainyCreekAssets.py --help
```

OBJ의 UV와 PNG는 glTF의 왼쪽 위 원점을 보존한다. 선택한 모델의 재질을 분리하고, 표면 속성 오차를 제한해 메시를 단순화했다. 원본의 고해상도 텍스처·일부 표면 맵·고유한 멀티패스 효과는 포함하지 않는다.

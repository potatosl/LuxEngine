# 무무 · Mumu

사용자가 제공한 둥근 캐릭터와 무심한 표정을 바탕으로 만든 게임용 3D 모델입니다. 모찌처럼 보송하고 부드러운 밝은 몸체, 짧은 두 발, 반쯤 감긴 삼각형 눈과 삼각형 입으로 구성했습니다. 얼굴 선은 실제 3D 메시입니다.

## 파일 선택

| 파일 또는 폴더 | 용도 |
| --- | --- |
| `Mumu.blend` | Blender 5.3 원본. 3단계 LOD, 재질, 뼈대, 3개 액션과 촬영용 조명·카메라 포함 |
| `Mumu.glb` | 기본 모델. 4개 뼈, 스킨, Idle·Walk·Hop 애니메이션 포함 |
| `Mumu_LOD1.glb` | 3,528삼각형의 정적 경량 모델 |
| `Mumu_LOD2.glb` | 1,840삼각형의 정적 최경량 모델 |
| `luxengine/lod0` | 현재 LuxEngine에서 사용할 기본 부위별 OBJ |
| `luxengine/lod1`, `luxengine/lod2` | 현재 LuxEngine에서 사용할 경량 부위별 OBJ |
| `exchange/Mumu_LOD*.obj` | 다른 모델링 도구용 조립 상태 OBJ와 공용 MTL |
| `Mumu.asset.json` | 좌표계, 피벗, 재질색, LOD 통계, 애니메이션 길이와 충돌체 제안 |
| `textures` | 모찌 표면의 1,024px 노멀·거칠기 텍스처 원본 |
| `previews` | 실제 모델 렌더와 LuxEngine 실행 화면 (`Tools/BuildMumu.py`로 다시 생성하며 저장소에는 포함하지 않음) |

LOD1·LOD2 GLB에는 뼈대와 애니메이션이 없습니다. 애니메이션이 필요하면 `Mumu.glb`를 사용합니다. OBJ 형식 자체에는 애니메이션이 저장되지 않습니다. 외부 텍스처나 추가 플러그인은 필요하지 않습니다.

## 모찌 재질

몸과 양발에 `Mumu_Mochi` 재질을 적용했습니다. 높은 표면 거칠기(평균 0.84), 낮은 반사 강도, 고운 가루처럼 보이는 미세 노멀을 사용합니다. Blender 원본에는 약한 표면 아래 산란(SSS 0.12, Scale 0.025m)을 더해 빛이 부드럽게 퍼지도록 했습니다. 검은 얼굴 선의 광택도 줄였습니다.

노멀과 거칠기 이미지는 `.blend`와 GLB에 내장돼 있습니다. `textures` 폴더는 다른 도구에서 재질을 직접 구성할 때 사용할 원본입니다. GLB는 노멀·거칠기·IOR·반사 강도를 보존하며, Blender의 SSS는 이 GLB 내보내기 경로에서 보존되지 않습니다.

현재 LuxEngine의 OBJ 렌더 경로는 이 PBR 텍스처와 SSS를 처리하지 않습니다. 모찌 재질은 Blender의 Material Preview 또는 Rendered 보기와 PBR을 지원하는 GLB 뷰어·엔진에서 확인합니다. `previews/Mumu_LuxEngine.png`와 기존 포터블 예제는 엔진의 기본 단색 재질을 보여줍니다.

## 모델 규격

| 단계 | 정점 수 | 삼각형 수 | 권장 사용 |
| --- | ---: | ---: | --- |
| LOD0 | 3,696 | 7,360 | 플레이어, 가까운 NPC |
| LOD1 | 1,780 | 3,528 | 중거리 NPC |
| LOD2 | 936 | 1,840 | 멀리 있는 NPC, 다수 배치 |

정점 수는 원본 메시의 위치 정점 합입니다. GLB는 UV 경계와 노멀 분리에 따라 실제 렌더 정점 수가 다를 수 있습니다. LOD는 제공 파일이며 거리에 따른 자동 전환은 게임에서 구현합니다.

단위는 미터이며 너비 약 2.0m, 전체 높이 1.73m, 몸체 깊이 약 1.28m입니다. 캐릭터를 작게 쓰려면 루트에 균일 배율을 적용합니다. GLB와 OBJ는 **+Y 위, +Z 정면**, 원점은 양발 사이의 지면입니다. Blender 원본은 Blender 기본 좌표계인 +Z 위, −Y 정면입니다.

`luxengine` 폴더의 `body.obj`와 `face.obj`는 캐릭터 루트 좌표를 사용합니다. `foot_l.obj`와 `foot_r.obj`는 발 중심을 기준으로 한 로컬 정점입니다. 기본 발 피벗은 각각 `(-0.53, 0.10, 0)`과 `(0.53, 0.10, 0)`입니다. `exchange`의 조립 OBJ에는 이 피벗이 이미 적용돼 있습니다.

흰 부위의 선형 RGBA는 `(0.82, 0.81, 0.76, 1)`, 얼굴은 `(0.008, 0.009, 0.012, 1)`입니다. 미터당 픽셀 같은 텍스처 설정이나 알파 블렌딩은 필요하지 않습니다.

## 애니메이션

| 클립 | 길이 | 동작 |
| --- | ---: | --- |
| `Idle` | 2.0초 | 작은 호흡과 몸체 흔들림. 반복 |
| `Walk` | 0.8초 | 좌우 발 교대, 몸체 바운스. 제자리 반복 |
| `Hop` | 1.2초 | 준비 자세, 약 0.48m 점프, 착지 |

30fps로 작성했습니다. Root·Body·Foot.L·Foot.R의 4개 뼈를 사용하며, 각 정점은 하나의 뼈에 100% 연결됩니다. 걷기는 제자리 동작이므로 이동 속도는 게임 코드에서 제어합니다. Hop에는 루트의 수직 이동이 들어 있으므로 게임의 점프 높이와 중복 적용하지 않도록 설정합니다. Hop 재생 시 반복을 끄는 설정은 사용하는 엔진에서 지정합니다.

## LuxEngine에서 실행

OBJ는 위치·삼각형과 스무딩 그룹(`s 1`)만 담고, 엔진이 이를 바탕으로 부드러운 정점 법선을 생성합니다. GLB·스켈레탈 애니메이션·OBJ의 MTL과 노멀·거칠기 텍스처 가져오기는 지원하지 않습니다. 에디터의 메시 컴포넌트에는 사용자 모델 경로가 없어 파일을 끌어 넣어 배치하는 방식은 아직 사용할 수 없습니다.

함께 제공한 `examples/MumuPreview`는 실제 엔진의 `AssetManager::LoadMesh`, `Mesh3D::Upload`, `Renderer3D::DrawMesh`로 부위별 OBJ를 그립니다. 부위별 단색 재질과 변환 행렬을 적용하고, 대기·걷기·점프를 C++ 절차 애니메이션으로 재생합니다. 모델은 초기화 때 GPU에 올리고 버퍼를 재사용합니다.

저장소 루트의 PowerShell에서 실행합니다.

```powershell
cmake -S examples/MumuPreview -B build/mumu-preview -G "Visual Studio 18 2026" -A x64
cmake --build build/mumu-preview --config Release
& .\build\mumu-preview\Release\MumuPreview.exe
```

기본 SDK는 저장소의 `dist/LuxEngine-SDK-0.1.0-x64`를 사용합니다. 다른 SDK는 `-DLuxEngine_DIR=SDK/lib/cmake/LuxEngine`로 지정합니다. SDK는 현재 엔진 소스로 설치한 것이어야 합니다. 오래된 SDK면 CMake 구성 단계에서 재설치 방법을 안내하며 중단합니다. 별도로 제공하는 `dist/MumuPreview-Portable.zip`은 압축을 풀고 `MumuPreview.exe`를 실행하면 됩니다.

조작은 `1` 대기, `2` 걷기, `3` 점프, `A/D` 회전, `W/S` 확대·축소, `Tab` LOD 변경, `Space` 일시정지, `R` 초기화, `Esc` 종료입니다. 엔진 렌더는 부드러운 정점 법선을 쓰지만 노멀·거칠기 텍스처와 SSS가 없어 Blender 미리보기와 표면 질감이 다릅니다.

## 재생성과 검증

전역 설치 없이 기존 Blender 실행 파일로 모델을 다시 생성할 수 있습니다.

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.3\blender.exe' --background --factory-startup --python-exit-code 1 --python Tools/BuildMumu.py
```

`-- --skip-render`를 덧붙이면 렌더 없이 모델만 생성합니다. `-- --animate-preview`는 정지 이미지와 걷기 미리보기용 24프레임 PNG를 `build/mumu-frames`에 만듭니다. 재생성은 이 캐릭터의 출력 파일을 덮어씁니다.

제공 파일은 GLB의 헤더·버퍼·접근자·인덱스·노멀·스킨 가중치와 애니메이션 채널을 검사했고, OBJ 12개의 면적 0인 삼각형·열린 경계·비다양체 모서리·면 방향·스무딩 그룹과 `Mumu.asset.json`의 부품별 정점·삼각형 수와 경계를 확인했습니다. 반복 애니메이션의 첫·마지막 자세 일치와 모든 부품의 강체 스킨도 검사합니다. GLB 애니메이션의 실제 스키닝 결과를 120Hz로 평가해 발바닥의 지면 관통도 검사했습니다. `Tools/ValidateMumu.py`는 별도 패키지 설치 없이 Python 표준 라이브러리로 이 검증을 재현합니다. LuxEngine 예제는 Release 빌드, 에셋 검사와 실제 Vulkan 렌더를 실행합니다.

```powershell
python Tools/ValidateMumu.py
& .\build\mumu-preview\Release\MumuPreview.exe --validate-assets
& .\build\mumu-preview\Release\MumuPreview.exe --smoke-test
& .\build\mumu-preview\Release\MumuPreview.exe --capture build/mumu-preview/mumu.png --frames 60
```

충돌체는 게임의 이동 방식에 맞춰 별도로 생성합니다. 메타데이터의 캡슐 값은 제안일 뿐 실제 물리 객체가 포함돼 있지는 않습니다.

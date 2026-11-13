# Mumu LuxEngine 미리보기

저장소 루트의 PowerShell에서 빌드합니다. 기본 SDK 경로는 `dist/LuxEngine-SDK-0.1.0-x64`입니다. 다른 SDK는 `-DLuxEngine_DIR=SDK/lib/cmake/LuxEngine`로 지정합니다. SDK는 현재 엔진 소스로 설치한 것이어야 합니다. 오래된 SDK면 CMake 구성 단계에서 재설치 방법을 안내하며 중단합니다.

```powershell
cmake -S examples/MumuPreview -B build/mumu-preview -G "Visual Studio 18 2026" -A x64
cmake --build build/mumu-preview --config Release
& ./build/mumu-preview/Release/MumuPreview.exe --validate-assets
& ./build/mumu-preview/Release/MumuPreview.exe --smoke-test
& ./build/mumu-preview/Release/MumuPreview.exe
```

`1` 대기, `2` 걷기, `3` 점프, `A/D` 카메라 회전, `W/S` 확대·축소, `Tab` LOD, `Space` 일시정지, `R` 초기화, `Esc` 종료입니다. `--capture build/mumu-preview/mumu.png --frames 60`으로 GPU 렌더링 결과를 저장합니다. PNG와 PPM을 지원합니다. `--assets`는 `lod0`·`lod1`·`lod2`가 있는 디렉터리를 받습니다. 실행 파일 옆에 OBJ, 셰이더, 런타임 DLL을 복사하므로 이 디렉터리를 함께 배포합니다.

모든 부품을 초기화 때 GPU에 한 번 업로드하고 프레임마다 버퍼와 변환 행렬을 재사용합니다. 절차적 변환 애니메이션은 예제 코드에서 실행되며 GLB 스켈레탈 클립 가져오기는 사용하지 않습니다. OBJ의 스무딩 그룹(`s 1`)으로 엔진이 생성한 정점 법선을 사용하므로 표면이 부드럽게 조명되며, 재질은 단색입니다.

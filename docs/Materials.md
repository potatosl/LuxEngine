# 3D 재질과 텍스처

`Material3D`의 재질 모드, 텍스처와 샘플링 설정은 씬 JSON에 저장된다. Details에서 편집하고 Undo/Redo, 복사·붙여넣기, 독립 플레이어와 생성 게임 배포에 같은 값을 사용한다. 기존 FBX의 재질을 자동으로 이 형식으로 변환하는 기능은 별도 범위다.

## 재질 모드

| 모드 | 깊이와 합성 |
| --- | --- |
| `MaterialAlphaMode::Opaque` | 텍스처와 기본색의 알파를 불투명으로 처리하고 깊이를 기록한다. |
| `MaterialAlphaMode::Mask` | 텍스처 알파와 기본색 알파의 곱이 `alphaCutoff`보다 작으면 픽셀을 버린다. 통과한 픽셀은 불투명하게 그리고 깊이를 기록한다. 그림자 패스에도 같은 컷오프를 사용한다. |
| `MaterialAlphaMode::Blend` | 불투명·컷아웃 뒤에 그리고 깊이는 검사하되 기록하지 않는다. 알파를 사용해 합성하며 방향광 그림자 패스에서는 제외한다. |

`doubleSided`를 끄면 뒷면을 컬링한다. 양면 표시에서 법선 방향은 메시의 원래 법선을 따른다. 음수 스케일과 메시의 면 방향은 표시 결과에 영향을 준다.

새 필드가 없는 씬과 기본 `Material3D`는 `Mask`, 컷오프 `0.001`, 양면 표시를 사용한다. 이는 기존 불투명 재질과 알파 0/1 컷아웃 텍스처의 표시를 유지하기 위한 기본값이다. 완전히 불투명한 표면은 `Opaque`를 명시한다. 구형 씬 파일을 읽는 경로는 유지하며 새 저장에는 추가 필드를 기록한다.

기존 렌더러는 부분 알파를 깊이에 기록하면서 RGB를 블렌딩하지 않았고, Qt 합성에서만 알파가 적용되는 불일치가 있었다. **기존 부분 알파 텍스처·기본색을 반투명으로 사용하려면 `Blend`를 명시해야 한다.** 이를 모든 기존 씬에 자동 적용하면 가림과 그리기 순서까지 바뀌므로 완전한 외관 호환을 보장하는 자동 변환은 수행하지 않는다.

새 렌더링의 투명한 프레임 버퍼 읽기는 premultiplied RGB를 포함한다. Qt 3D 뷰포트는 이를 straight `Format_RGBA8888`로 변환한 뒤 반환하므로 이미지의 색 조회와 QPainter 합성에서 알파가 두 번 적용되지 않는다. 낮은 수준의 런타임 캡처와 비교할 때는 같은 알파 표현으로 해석해야 한다.

반투명 오브젝트는 카메라에서 **모델 원점까지의 거리**를 기준으로 먼 것부터 정렬한다. 같은 거리는 제출 순서를 유지한다. 교차하는 반투명 메시, 하나의 메시 내부에서 겹치는 삼각형과 큰 메시의 일부가 앞뒤로 교차하는 경우는 정확한 정렬을 보장하지 않는다. 반투명 모드는 물 전용 반사·굴절·수심 처리를 제공하지 않는다.

## PBR 텍스처 슬롯

| 필드 | 색 공간·채널 | 적용 |
| --- | --- | --- |
| `baseColorTexture` | sRGB RGB, 선형 알파 | `albedoColor`와 곱한다. |
| `normalTexture` | 선형 RGB | 접선 공간의 +Y 노멀 맵. `normalScale`이 XY 강도를 조절한다. |
| `metallicTexture` | 선형 R | `metallic` 상수와 곱한다. |
| `roughnessTexture` | 선형 R | `roughness` 상수와 곱하고 셰이더의 유효 거칠기 범위로 제한한다. |
| `occlusionTexture` | 선형 R | 환경광에 적용한다. `occlusionStrength` 0은 효과 없음, 1은 맵 값 전체 적용이다. 직접광에는 곱하지 않는다. |

금속성·거칠기·AO의 PBR 반응은 `Render3DMode::Pbr`에서 사용한다. 값이 없는 슬롯은 기존 상수 기반 렌더링을 유지한다. 슬롯은 각각 독립 이미지이며 glTF ORM처럼 여러 값을 묶은 텍스처의 채널을 자동으로 해석하지 않는다. PNG, JPEG, BMP, TGA 등 기존 기본색 이미지 로더가 허용하는 파일을 사용한다.

노멀 맵은 정점의 법선과 화면 공간 위치·UV 미분으로 접선 프레임을 계산한다. 별도의 정점 접선 속성을 요구하지 않는다. UV나 접선이 퇴화한 픽셀은 메시 법선을 유지한다. UV가 없는 파일 모델에 텍스처를 요구하면 기존 자산 오류 경로를 사용하며, 실패한 재가져오기로 정상 캐시를 교체하지 않는다. 불러오지 못한 개별 텍스처는 오류 패널에 원인을 전달하고 이전에 정상으로 올린 버전이 있으면 유지한다.

```cpp
LuxEngine::Material3D material;
material.alphaMode = LuxEngine::MaterialAlphaMode::Opaque;
material.albedoColor = {.8f, .8f, .8f, 1.f};
material.baseColorTexture = "Assets/stone-color.png";
material.normalTexture = "Assets/stone-normal.png";
material.normalScale = 1.f;
material.metallic = 1.f;
material.metallicTexture = "Assets/stone-metallic.png";
material.roughness = 1.f;
material.roughnessTexture = "Assets/stone-roughness.png";
material.occlusionTexture = "Assets/stone-ao.png";
material.occlusionStrength = 1.f;
```

상수가 맵과 곱해지므로 금속성 맵을 그대로 사용하려면 `metallic = 1`로 설정한다. `metallic = 0`이면 맵이 있어도 비금속이다.

## 밉맵과 샘플링

3D 재질의 `textureSampling` 기본값은 밉맵 사용, `TextureAddressMode::Repeat`, 이방성 필터링 사용이다. Details에서 밉맵·Clamp/Repeat·이방성을 바꿀 수 있다. 이방성 설정은 최대 8배를 사용하며 Vulkan 장치가 이를 지원하지 않으면 일반 선형 필터링으로 동작한다. Clamp와 Repeat에 각각 일반 삼선형 및 이방성 샘플러를 사용한다. 명시적으로 전달한 Nearest 텍스처는 점 샘플링을 유지한다.

- **Clamp**는 이미지 경계의 텍셀을 늘려 샘플링한다.
- **Repeat**는 0~1 범위 밖의 UV를 반복한다.
- **밉맵**은 화면에서 작게 보이는 표면에 낮은 해상도의 레벨을 사용한다. 전체 체인은 마지막 1×1까지 만든다.
- **이방성**은 비스듬히 보이는 표면의 샘플링에 사용한다.

2D와 픽셀 아트의 기존 호출은 그대로 유지한다. `GpuTexture::Initialize`의 새 `TextureSampling` 인자를 생략하면 밉맵이 없는 기존 텍스처를 만들며, `TextureFilter::Nearest`도 유지한다. 3D의 주소 모드와 이방성은 재질의 그리기 설정이다.

```cpp
LuxEngine::TextureSampling sampling;
sampling.mipmaps = true;
sampling.addressMode = LuxEngine::TextureAddressMode::Repeat;
sampling.anisotropic = true;
material.textureSampling = sampling;
```

밉 생성은 CPU에서 수행한다. sRGB 색상은 선형 공간으로 변환해 알파를 고려하여 평균한 뒤 sRGB로 되돌린다. 금속성·거칠기·AO는 선형 데이터로 평균한다. 노멀 맵은 벡터를 평균하고 정규화한다. 홀수 크기와 1×N·N×1도 마지막 텍셀이 사라지지 않도록 영역 가중치로 처리한다. 노멀 맵을 sRGB로 지정하는 입력은 거부한다.

`BuildTextureMipChain`은 최대 8192×8192 입력과 256 MiB 이하의 전체 밉 데이터를 허용한다. 잘못된 입력이나 실패는 기존 출력 체인을 보존한다. 밉이 없는 GPU 텍스처는 기존 16384 축 크기 상한을 유지한다. 파일 이미지 로더의 기존 크기·파일 크기 제한도 적용된다.

## 캐시와 수명

3D 텍스처 캐시는 경로뿐 아니라 색 공간·밉맵·주소 모드·이방성·노멀 데이터 여부로 구분한다. 같은 파일을 기본색과 데이터 맵에 동시에 써도 서로 다른 색 공간으로 처리된다. 캐시의 256 MiB 예산에는 전체 밉 체인과 기존 단일 레벨 1차원 텍스처의 패딩 바이트를 포함한다. 잘못된 파일의 재가져오기는 자산의 기존 리비전을 유지하고, GPU 업로드 실패는 기존 정상 GPU 텍스처를 유지한다. 같은 리비전의 업로드 실패를 매 프레임 반복하지 않으며 새로 재가져온 리비전에서 다시 시도한다.

`GpuTexture::GetMipCount()`와 `GetStorageBytes()`로 업로드 레벨과 데이터 크기를 확인할 수 있다. `GpuDevice::GetMemoryStats().allocationBytes`에도 사용자 텍스처의 전체 밉 데이터가 포함된다. 이 값은 엔진이 추적하는 데이터 크기이며 드라이버별 힙 정렬·메타데이터까지 측정하는 값은 아니다. 실제 해제 대기 중인 자원은 GPU가 사용을 마칠 때까지 집계된다.

머티리얼이 한 번 그려지는 동안 필요한 다섯 텍스처를 유지해, 다른 맵을 로딩하며 캐시를 축출해도 현재 그리기의 핸들이 무효화되지 않는다. 셰이더의 텍스처 디스크립터는 프레임별로 분리하고 D3D12의 개수 한도를 넘지 않는 은행으로 나눈다. 샘플러는 씬 디스크립터에서 공유한다.

## 검증

- `Engine_texture_mips`: 홀수·1차원·1×1 크기, 전체 체인 크기, sRGB/선형 평균, 투명 가장자리, 노멀 정규화, 잘못된 입력과 실패 원자성.
- `Forge_material_textures`: 실제 GPU의 1×1·1×9·13×1·7×5 밉 업로드와 그리기, 금속성·거칠기·노멀·AO의 픽셀 반응, Clamp/Repeat 및 이방성 샘플링.
- `Forge_materials`: 불투명·컷아웃·반투명 재질, 삽입 순서와 깊이·알파 합성.

```sh
ctest --test-dir build -R 'Engine_texture_mips|Forge_materials|Forge_material_textures' --output-on-failure
```

GPU 검사는 실제 그래픽 장치와 표시 세션이 필요하다. 플랫폼별 빌드 및 실제 GPU 통과 여부는 해당 실행 결과로 확인한다. 이 문서의 기능 목록이 모든 장치에서 검증 완료됐다는 뜻은 아니다. 텍스처 아틀라스의 밉 경계 확장·알파 커버리지 보존, 압축 텍스처, 프리컴퓨트된 밉 가져오기와 패킹 채널 선택은 후속 범위다.

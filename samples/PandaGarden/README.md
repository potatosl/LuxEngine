# 판다의 대나무 정원

사용자가 제공한 `Assets/panda.png`와 `Assets/bamboo.png` 원본 이미지를 유지하는 LuxEngine 게임 샘플입니다. 판다의 검은 바탕은 로딩할 때 연결된 외곽 영역에 한해 투명 처리합니다.

나머지 정원 배경 자산은 [Kenney Tiny Town](https://kenney.nl/assets/tiny-town)의 CC0 원본 타일을 재사용합니다. 게임에 사용하는 타일과 출처·라이선스 자료는 `Assets/BackgroundTinyTown/`에 보관합니다. 저장소에 보관한 원본 패키지의 [출처·파일 해시 목록](../Assets/TinyTown/source-manifest.json)과 [라이선스](../Assets/TinyTown/License.txt)도 확인할 수 있습니다.

1280×800 고정 창 전체를 정원으로 사용합니다. 실행하면 바로 시작하고 대나무를 계속 수집하며 움직이는 버섯을 피합니다. 제한 시간이나 수집 개수에 따른 종료는 없습니다. 게임 화면에는 글씨, 정보 바, 시작·일시정지·결과 메뉴와 좌우 장식 테두리를 표시하지 않습니다.

- WASD / 방향키: 이동
- 왼쪽 Shift: 달리기
- P / Esc: 일시정지·계속, 별도 메뉴는 표시하지 않음
- R: 즉시 다시 시작
- 창 닫기: 종료

빌드 타깃은 `SamplePandaGarden`이며 `LuxEngine::Engine`에 직접 연결합니다. 공통 샘플 UI·폰트 라이브러리는 사용하지 않습니다. 배포할 때 실행 파일 옆의 `panda-assets/`, `shaders/`와 런타임 DLL을 함께 유지합니다. 자동 종료나 OS 전체화면 전환은 없습니다.

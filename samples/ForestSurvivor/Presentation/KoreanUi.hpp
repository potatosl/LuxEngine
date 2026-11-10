#pragma once

#include <array>
#include <string_view>

namespace ForestGame::UiText {
inline constexpr std::string_view Original = "LuxEngine 오리지널";
inline constexpr std::string_view Brand = "MOSSBOUND";
inline constexpr std::string_view Subtitle = "마지막 숲의 수호자";
inline constexpr std::string_view Genre = "숲 생존 모험";
inline constexpr std::string_view Story =
    "숲의 정수를 모아 성장하고, 밀려오는 적에게서 숲을 지켜 주세요.";
inline constexpr std::string_view Play = "시작하기";
inline constexpr std::string_view Quit = "종료";
inline constexpr std::string_view ClickPlay = "시작하기 버튼을 눌러 주세요";
inline constexpr std::string_view Move = "WASD / 방향키: 이동";
inline constexpr std::string_view Controls = "Space: 돌진   P / Esc: 일시정지";
inline constexpr std::string_view Adventure = "5분간의 숲 생존";
inline constexpr std::string_view Tagline = "작은 숲에서 시작하는 수호자의 여정";
inline constexpr std::string_view Features = "자동 공격 / 성장 / 보스전";
inline constexpr std::string_view Art = "CC0 그래픽: Kenney";
inline constexpr std::string_view Typeface = "글꼴: 둥근모꼴";
inline constexpr std::string_view Survive = "생존 목표 ";
inline constexpr std::string_view Level = "등급 ";
inline constexpr std::string_view Slain = "처치 ";
inline constexpr std::string_view Pause = "일시정지";
inline constexpr std::string_view Keeper = "수호자";
inline constexpr std::string_view Essence = "정수 ";
inline constexpr std::string_view DashReady = "돌진 준비 완료";
inline constexpr std::string_view DashCharging = "돌진 충전 중";
inline constexpr std::string_view Boss = "가시 왕";
inline constexpr std::string_view AutoAttack = "활은 가까운 적을 자동으로 공격합니다";
inline constexpr std::string_view Gift = "숲의 선물";
inline constexpr std::string_view Choose = "강화 하나를 선택해 주세요";
inline constexpr std::string_view Enter = "Enter로 선택";
inline constexpr std::string_view Click = "클릭하여 선택";
inline constexpr std::string_view Wait = "강화를 고르는 동안 게임은 멈춥니다";
inline constexpr std::string_view Paused = "잠시 쉬어가세요";
inline constexpr std::string_view Victory = "숲을 지켰습니다!";
inline constexpr std::string_view Defeat = "여정이 끝났습니다";
inline constexpr std::string_view PausedDetail = "게임이 일시정지 상태입니다";
inline constexpr std::string_view VictoryDetail = "가시 왕을 물리치고 숲의 평화를 되찾았습니다";
inline constexpr std::string_view DefeatDetail = "모은 경험으로 다시 도전해 보세요";
inline constexpr std::string_view Resume = "계속하기";
inline constexpr std::string_view Restart = "다시 시작";
inline constexpr auto All = std::to_array<std::string_view>(
    {Original,     Brand,        Subtitle, Genre,      Story,   Play,         Quit,
     ClickPlay,    Move,         Controls, Adventure,  Tagline, Features,     Art,
     Typeface,     Survive,      Level,    Slain,      Pause,   Keeper,       Essence,
     DashReady,    DashCharging, Boss,     AutoAttack, Gift,    Choose,       Enter,
     Click,        Wait,         Paused,   Victory,    Defeat,  PausedDetail, VictoryDetail,
     DefeatDetail, Resume,       Restart});
} // namespace ForestGame::UiText

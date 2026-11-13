#pragma once

namespace LuxEngine {
template <class Entry> int RunEngineMain(Entry&& entry, int argc, char** argv) {
    Log::ConfigureUtf8Output();
    try {
        return std::invoke(std::forward<Entry>(entry), argc, argv);
    } catch (const std::exception& error) {
        Log::Error(std::string("응용 프로그램 실행 중 오류가 발생했습니다: ") + error.what());
    } catch (...) {
        Log::Error("응용 프로그램 실행 중 알 수 없는 오류가 발생했습니다.");
    }
    return 1;
}
} // namespace LuxEngine

#define LUX_ENGINE_MAIN(Entry)                                                                     \
    int main(int argc, char** argv) {                                                              \
        return ::LuxEngine::RunEngineMain(Entry, argc, argv);                                      \
    }

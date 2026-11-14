#include <LuxEditor/Build/BuildOutput.hpp>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        const LuxUI::ByteArray utf8 = LUX_UI_TEXT("빌드 오류: 파일을 찾을 수 없습니다.").toUtf8();
        if (LuxEditor::DecodeBuildOutput(utf8) != LuxUI::String::fromUtf8(utf8))
            throw std::runtime_error("UTF-8 build output was corrupted");
        const LuxUI::ByteArray bytes("A\0B", 3);
        if (LuxEditor::DecodeBuildOutput(bytes).size() != 3)
            throw std::runtime_error("Build output ignored its explicit byte length");
        const auto environment = LuxEditor::Utf8BuildEnvironment();
        if (environment.value(LUX_UI_TEXT("PYTHONUTF8")) != LUX_UI_TEXT("1") ||
            environment.value(LUX_UI_TEXT("DOTNET_CLI_FORCE_UTF8_ENCODING")) != LUX_UI_TEXT("true"))
            throw std::runtime_error("Build process UTF-8 environment was not configured");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

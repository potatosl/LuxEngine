#include <LuxEditor/Build/BuildOutput.hpp>
#include <LuxUI/Widgets.hpp>

namespace LuxEditor {
LuxUI::String DecodeBuildOutput(LuxUI::ByteArrayView bytes) {
    LuxUI::StringDecoder decoder(LuxUI::StringDecoder::Utf8,
                                 LuxUI::StringConverter::Flag::Stateless);
    const LuxUI::String text = decoder(bytes);
    return decoder.hasError() ? LuxUI::String::fromLocal8Bit(bytes) : text;
}

LuxUI::ProcessEnvironment Utf8BuildEnvironment() {
    auto environment = LuxUI::ProcessEnvironment::systemEnvironment();
    environment.insert(LUX_UI_TEXT("PYTHONUTF8"), LUX_UI_TEXT("1"));
    environment.insert(LUX_UI_TEXT("PYTHONIOENCODING"), LUX_UI_TEXT("utf-8"));
    environment.insert(LUX_UI_TEXT("DOTNET_CLI_FORCE_UTF8_ENCODING"), LUX_UI_TEXT("true"));
    return environment;
}
} // namespace LuxEditor

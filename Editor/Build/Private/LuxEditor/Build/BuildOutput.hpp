#pragma once
#include <LuxUI/Widgets.hpp>

namespace LuxEditor {
LuxUI::String DecodeBuildOutput(LuxUI::ByteArrayView bytes);

LuxUI::ProcessEnvironment Utf8BuildEnvironment();
} // namespace LuxEditor

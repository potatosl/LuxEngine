#pragma once

#include <QByteArrayView>
#include <QProcessEnvironment>
#include <QString>

namespace LuxEditor {
QString DecodeBuildOutput(QByteArrayView bytes);

QProcessEnvironment Utf8BuildEnvironment();
} // namespace LuxEditor

#include <LuxEditor/Build/BuildOutput.hpp>
#include <QStringDecoder>

namespace LuxEditor {
QString DecodeBuildOutput(QByteArrayView bytes) {
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString text = decoder(bytes);
    return decoder.hasError() ? QString::fromLocal8Bit(bytes) : text;
}

QProcessEnvironment Utf8BuildEnvironment() {
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    environment.insert(QStringLiteral("DOTNET_CLI_FORCE_UTF8_ENCODING"), QStringLiteral("true"));
    return environment;
}
} // namespace LuxEditor

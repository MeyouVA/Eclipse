// Eclipse - Script and External effects: a picture handed to JavaScript, or to another program.
#include "render/effects/Effects.h"

#include <QColor>
#include <QDir>
#include <QImage>
#include <QJSEngine>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QTemporaryDir>
#include <QTransform>

#include <algorithm>
#include <cmath>

#include <QObject>

namespace {

// What a script calls `image`.
class ScriptImage : public QObject {
    Q_OBJECT
    Q_PROPERTY(int width READ width)
    Q_PROPERTY(int height READ height)

public:
    ScriptImage(QImage& image, double unit) : m_image(image), m_unit(unit) {}
    int width() const { return m_image.width(); }
    int height() const { return m_image.height(); }
    Q_INVOKABLE QString pixel(int x, int y) const
    {
        return m_image.valid(x, y) ? m_image.pixelColor(x, y).name(QColor::HexArgb) : QString();
    }
    Q_INVOKABLE void setPixel(int x, int y, QString const& colour)
    {
        if (m_image.valid(x, y))
            m_image.setPixelColor(x, y, QColor(colour));
    }
    Q_INVOKABLE void blur(double radius) { Effects::blur(m_image, radius * m_unit); }
    Q_INVOKABLE void tint(QString const& colour, double amount) { Effects::tint(m_image, QColor(colour), amount); }
    Q_INVOKABLE void transparency(double amount) { Effects::transparency(m_image, amount); }
    Q_INVOKABLE void negate(double amount) { Effects::negate(m_image, amount); }
    Q_INVOKABLE void blackWhite(double amount) { Effects::blackWhite(m_image, amount); }
    Q_INVOKABLE void hueSaturation(double hue, double saturation, double lightness)
    {
        Effects::hueSaturation(m_image, hue, saturation, lightness, 1.0);
    }
    Q_INVOKABLE void brightnessContrast(double brightness, double contrast) { Effects::brightnessContrast(m_image, brightness, contrast, 1.0); }
    Q_INVOKABLE void pixelate(double size) { Effects::pixelate(m_image, size * m_unit); }
    Q_INVOKABLE void median(double radius) { Effects::median(m_image, radius * m_unit); }
    Q_INVOKABLE void bloom(double radius) { Effects::bloom(m_image, radius * m_unit); }
    Q_INVOKABLE void grain(double amount) { Effects::grain(m_image, amount, 1); }
    Q_INVOKABLE void directionalBlur(double length, double degrees) { Effects::directionalBlur(m_image, length * m_unit, degrees); }
    Q_INVOKABLE void colourScale(QString const& colour, double amount) { Effects::colourScale(m_image, QColor(colour), amount); }

private:
    QImage& m_image;
    double m_unit;
};

}

QString Effects::runScript(QImage& image, QString const& script, int frame, double amount, double unit)
{
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QJSEngine engine;
    engine.setInterrupted(false);
    ScriptImage picture(image, unit);
    engine.globalObject().setProperty(QStringLiteral("image"), engine.newQObject(&picture));
    QJSEngine::setObjectOwnership(&picture, QJSEngine::CppOwnership);
    engine.globalObject().setProperty(QStringLiteral("frame"), frame + 1);
    engine.globalObject().setProperty(QStringLiteral("amount"), amount);
    QJSValue const result = engine.evaluate(script);
    if (result.isError())
        return QObject::tr("Line %1: %2").arg(result.property(QStringLiteral("lineNumber")).toInt()).arg(result.toString());
    return QString();
}

QString Effects::runExternal(QImage& image, QString const& command, int frame, int seconds)
{
    QStringList words = QProcess::splitCommand(command);
    if (words.isEmpty())
        return QObject::tr("No command line.");
    QTemporaryDir dir;
    if (!dir.isValid())
        return QObject::tr("No room for the pictures.");
    QString const in = QDir::toNativeSeparators(dir.filePath(QStringLiteral("in.png")));
    QString const out = QDir::toNativeSeparators(dir.filePath(QStringLiteral("out.png")));
    double const dpr = image.devicePixelRatio();
    if (!image.save(in, "PNG"))
        return QObject::tr("The picture could not be written.");
    for (QString& w : words)
        w.replace(QStringLiteral("{in}"), in).replace(QStringLiteral("{out}"), out).replace(QStringLiteral("{frame}"), QString::number(frame + 1));
    QProcess process;
    process.setWorkingDirectory(dir.path());
    process.start(words.takeFirst(), words);
    if (!process.waitForStarted(10000))
        return QObject::tr("The program could not be started.");
    if (!process.waitForFinished(std::max(1, seconds) * 1000)) {
        process.kill();
        process.waitForFinished(2000);
        return QObject::tr("The program took too long.");
    }
    QImage made(out);
    if (made.isNull())
        return QObject::tr("The program made no picture at {out}.");
    made = made.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    made.setDevicePixelRatio(dpr);
    image = made;
    return QString();
}

#include "EffectsScript.moc"

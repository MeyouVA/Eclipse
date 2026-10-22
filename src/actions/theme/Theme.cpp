#include "actions/theme/Theme.h"

#include <QApplication>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QSvgRenderer>

namespace Theme {

static bool readText(QString const& path, QString& out, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot read %1: %2").arg(path, file.errorString());
        return false;
    }
    out = QString::fromUtf8(file.readAll());
    return true;
}

bool apply(QApplication& app, QString const& resources, QString const& theme, QString* error)
{
    QString sheet, vars;
    if (!readText(resources + QStringLiteral("/stylesheets/Master.qss"), sheet, error)
        || !readText(resources + QStringLiteral("/stylesheets/") + theme + QStringLiteral(".vars"), vars, error))
        return false;

    QHash<QString, QString> values;
    for (QString const& line : vars.split(QLatin1Char('\n'))) {
        int const eq = int(line.indexOf(QLatin1Char('=')));
        if (eq > 0 && !line.trimmed().startsWith(QLatin1Char('#')))
            values.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    static QRegularExpression const variable(QStringLiteral("\\$\\[(\\w+)\\]"));
    QString out;
    int last = 0;
    for (QRegularExpressionMatchIterator it = variable.globalMatch(sheet); it.hasNext();) {
        QRegularExpressionMatch const m = it.next();
        out += sheet.mid(last, m.capturedStart() - last);
        out += values.value(m.captured(1), QStringLiteral("magenta"));  // a missing name shows
        last = int(m.capturedEnd());
    }
    out += sheet.mid(last);

    // The palette too: widgets that paint themselves ask it, not the stylesheet.
    auto color = [&values](char const* name) { return QColor(values.value(QLatin1String(name), QStringLiteral("#808080"))); };
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette palette;
    palette.setColor(QPalette::Window, color("window"));
    palette.setColor(QPalette::WindowText, color("text"));
    palette.setColor(QPalette::Base, color("base"));
    palette.setColor(QPalette::AlternateBase, color("panel"));
    palette.setColor(QPalette::Text, color("text"));
    palette.setColor(QPalette::Button, color("raised"));
    palette.setColor(QPalette::ButtonText, color("text"));
    palette.setColor(QPalette::Highlight, color("accent"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, color("raised"));
    palette.setColor(QPalette::ToolTipText, color("text"));
    for (QPalette::ColorRole role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText})
        palette.setColor(QPalette::Disabled, role, color("textDim"));
    app.setPalette(palette);
    app.setStyleSheet(out);
    return true;
}

QIcon icon(QString const& resources, QString const& name)
{
    QString const path = resources + QStringLiteral("/icons/") + name + QStringLiteral(".svg");
    if (!QFile::exists(path))
        return QIcon();
    QSvgRenderer renderer(path);
    if (!renderer.isValid())
        return QIcon();
    QIcon icon;
    for (int size : {20, 40}) {
        QPixmap normal(size, size);
        normal.fill(Qt::transparent);
        QPainter painter(&normal);
        renderer.render(&painter, QRectF(0, 0, size, size));
        painter.end();
        // Greyed out: the same drawing, faint.
        QPixmap disabled(size, size);
        disabled.fill(Qt::transparent);
        QPainter faint(&disabled);
        faint.setOpacity(0.3);
        faint.drawPixmap(0, 0, normal);
        faint.end();
        icon.addPixmap(normal, QIcon::Normal);
        icon.addPixmap(normal, QIcon::Active);
        icon.addPixmap(normal, QIcon::Selected);
        icon.addPixmap(disabled, QIcon::Disabled);
    }
    return icon;
}

}

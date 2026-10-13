// Eclipse - the Camera view: the Text tool.
#include "ui/canvas/Canvas.h"

#include <QLineEdit>
#include <QPainterPath>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

// ---- text ---------------------------------------------------------------------------------

void Canvas::beginText(QPointF widgetPos, QPointF layerPos, QString const& words)
{
    m_textAt = layerPos;
    m_textEdit = -1;
    m_textEntry = new QLineEdit(this);
    m_textEntry->setObjectName(QStringLiteral("TextEntry"));
    // As big on the screen as the words will be.
    QFont font = m_textFont;
    font.setPixelSize(std::clamp(int(m_textFont.pixelSize() * m_zoom + 0.5), 8, 200));
    m_textEntry->setFont(font);
    m_textEntry->setFrame(false);
    m_textEntry->setStyleSheet(QStringLiteral("QLineEdit { background: rgba(255, 255, 255, 215); color: black; border: 1px solid #2a9bb0; }"));
    int const height = m_textEntry->sizeHint().height();
    QFontMetrics const metrics(font);
    // The line of the letters is where the click was.
    m_textEntry->setGeometry(int(widgetPos.x()) - 2, int(widgetPos.y()) - metrics.ascent() - (height - metrics.height()) / 2,
                             std::max(160, width() - int(widgetPos.x()) - 12), height);
    connect(m_textEntry, &QLineEdit::returnPressed, this, [this] { endText(true); });
    // The entry is one line: where a new line of words begins there is a mark.
    QString shown = words;
    shown.replace(QLatin1Char('\n'), QChar(kLineBreak));
    m_textEntry->setText(shown);
    m_textEntry->installEventFilter(this);
    m_textEntry->show();
    m_textEntry->setFocus();
}

void Canvas::endText(bool keep)
{
    if (!m_textEntry)
        return;
    QLineEdit* const entry = m_textEntry;
    m_textEntry = nullptr;
    QString text = entry->text();
    text.replace(QChar(kLineBreak), QLatin1Char('\n'));
    entry->hide();
    entry->setObjectName(QString());  // it is on its way out: not the entry to look for any more
    entry->deleteLater();
    setFocus();
    if (keep)
        addText(m_textAt, text);
    m_textEdit = -1;
}

void Canvas::addText(QPointF layerPos, QString const& text)
{
    Scene const& scene = m_document->scene();
    if (text.trimmed().isEmpty() || m_layer < 0 || m_layer >= scene.layers.size() || !scene.layers[m_layer].holdsDrawings()
        || scene.layers[m_layer].locked)
        return;
    // The outlines of the letters: one painted shape for all of them, with the holes of
    // the letters left out by the outlines inside outlines.
    QPainterPath path;
    // The letters as far apart as asked (the font kept with the words is the plain one).
    QFont spaced = m_textFont;
    spaced.setLetterSpacing(QFont::PercentageSpacing, m_textKerning);
    QFontMetricsF const metrics(spaced);
    QStringList const lines = text.split(QLatin1Char('\n'));
    double const spacing = m_textSpacing / 100.0;
    double widest = 0.0;
    for (QString const& line : lines)
        widest = std::max(widest, metrics.horizontalAdvance(line));
    for (int i = 0; i < lines.size(); ++i) {
        // Each line starts at the place, is centred on it, or ends at it.
        double const wide = metrics.horizontalAdvance(lines[i]);
        double const shift = m_textAlign == 1 ? -wide / 2.0 : m_textAlign == 2 ? -wide : 0.0;
        QFont set = spaced;
        // Justified: every line but the last is spread, between its words, to the width of the widest.
        int const gaps = int(lines[i].trimmed().count(QChar(0x20)));
        if (m_textAlign == 3 && i + 1 < lines.size() && gaps > 0 && widest > wide)
            set.setWordSpacing((widest - wide) / gaps);
        path.addText(layerPos + QPointF(shift, i * metrics.lineSpacing() * spacing), set, lines[i]);
    }
    Fill letters;
    for (QPolygonF const& outline : path.toSubpathPolygons())
        if (outline.size() >= 3)
            letters.outlines.push_back(outline);
    if (letters.outlines.isEmpty())
        return;
    letters.swatch = m_swatch;
    letters.color = scene.resolve(m_swatch, Qt::black);
    letters.text = text;
    letters.textFont = m_textFont.toString();
    letters.textAt = layerPos;
    letters.textAlign = m_textAlign;
    letters.textSpacing = spacing;
    letters.textKerning = m_textKerning;
    Drawing edited = currentDrawing() ? *currentDrawing() : Drawing();
    if (m_textEdit >= 0 && m_textEdit < edited.fills.size() && !edited.fills[m_textEdit].text.isEmpty()) {
        // Words taken up again: the new ones stand in their place, in their colour.
        Fill const& old = edited.fills[m_textEdit];
        if (old.text == text && old.textFont == letters.textFont && old.textAlign == letters.textAlign && old.textSpacing == letters.textSpacing
            && old.textKerning == letters.textKerning)
            return;     // nothing was changed
        letters.swatch = old.swatch;
        letters.color = old.color;
        letters.group = old.group;
        edited.fills[m_textEdit] = letters;
        commitDrawing(edited, tr("Edit Text"));
        return;
    }
    edited.fills.push_back(letters);
    commitDrawing(edited, tr("Text"));
}

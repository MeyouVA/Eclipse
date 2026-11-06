// Eclipse - a label that shows as much of its text as its width allows (cut short with an ellipsis, whole in its tooltip), and never
// asks for more room than a little: the Camera view's strip uses it for messages, so a long one does not widen the view.
#pragma once

#include <QLabel>
#include <QResizeEvent>

#include <algorithm>

class ElidedLabel : public QLabel {
public:
    using QLabel::QLabel;
    // The whole text: shown cut to the label's width, whole in the tooltip when it does not fit.
    void setFullText(QString const& text)
    {
        m_full = text;
        updateGeometry();
        refit();
    }
    QString fullText() const { return m_full; }
    QSize minimumSizeHint() const override
    {
        // (Room for a short message - the tool in hand - at the least, so that the strip keeps its width as messages come and go.)
        return QSize(160, QLabel::minimumSizeHint().height());
    }
    QSize sizeHint() const override
    {
        // (As wide as the text, but no wider than this: the strip shares out the rest.)
        return QSize(std::min(fontMetrics().horizontalAdvance(m_full) + 8, 360), QLabel::sizeHint().height());
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        refit();
    }

private:
    void refit()
    {
        QString const shown = fontMetrics().elidedText(m_full, Qt::ElideRight, std::max(0, width() - 4));
        if (text() != shown)
            QLabel::setText(shown);
        setToolTip(shown == m_full ? QString() : m_full);
    }
    QString m_full;
};

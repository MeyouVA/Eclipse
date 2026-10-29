// Eclipse - the Node View's backdrops: coloured panels with a title and a note, behind the nodes they gather.
#include "ui/views/NodeView.h"
#include "actions/theme/InterfaceColours.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"
#include "ui/panels/ColourEditor.h"

QVector<Backdrop> const& NodeView::backdrops() const
{
    return graphMode() ? m_document->scene().graph.backdrops : m_document->scene().backdrops;
}

QRectF NodeView::backdropRect(int index) const
{
    QVector<Backdrop> const& all = backdrops();
    if (index < 0 || index >= all.size() || all[index].group != (graphMode() ? m_graphGroup : m_groupId))
        return QRectF();
    QRectF r = all[index].rect;
    // One being moved or made larger is drawn where the pointer has it.
    if (m_drag == DragBackdrop && m_backdropDrag == index) {
        QPointF const by = (m_dragNow - m_pressPos) / m_zoom;
        if (m_backdropResize) {
            r.setWidth(std::max(60.0, r.width() + by.x()));
            r.setHeight(std::max(40.0, r.height() + by.y()));
        } else {
            r.translate(by);
        }
    }
    return QRectF(toWidget(r.topLeft()), r.size() * m_zoom);
}

int NodeView::backdropAt(QPointF widgetPos) const
{
    // The one on top (the last drawn) first.
    for (int i = int(backdrops().size()) - 1; i >= 0; --i)
        if (backdropRect(i).contains(widgetPos))
            return i;
    return -1;
}

void NodeView::pickBackdrop(int index, bool add)
{
    if (!add) {
        // (A backdrop picked alone: the nodes are let go of.)
        m_backdropPicked.clear();
        m_layerPicked.clear();
        m_graphPicked.clear();
        m_graphNode = 0;
        m_cable = m_graphCable = -1;
    }
    if (m_backdropPicked.contains(index))
        m_backdropPicked.removeAll(index);
    else if (index >= 0 && index < backdrops().size())
        m_backdropPicked << index;
    update();
}

void NodeView::setBackdrops(QVector<Backdrop> const& backdrops, QString const& text)
{
    if (graphMode()) {
        NodeGraph g = m_document->scene().graph;
        if (g.backdrops == backdrops)
            return;
        g.backdrops = backdrops;
        m_document->setGraph(g, text);
    } else {
        if (m_document->scene().backdrops == backdrops)
            return;
        m_document->editScene(text, [&backdrops](Scene& s) { s.backdrops = backdrops; });
    }
}

QVector<QColor> NodeView::backdropColours() const
{
    QVector<QColor> out;
    int const count = std::clamp(m_options.backdropColours, 1, 17);
    for (int c = 0; c < count; ++c)
        out << InterfaceColours::get(QStringLiteral("backdrop%1").arg(c).toLatin1().constData());
    return out;
}

int NodeView::addBackdrop(QString const& title)
{
    // Round the nodes picked (with room for its title), or in the middle of the view.
    QRectF around;
    for (int key : pickedKeys())
        if (QRectF const r = nodeRectOf(key); !r.isNull())
            around = around.united(QRectF(toGraph(r.topLeft()), toGraph(r.bottomRight())));
    Backdrop b;
    b.group = graphMode() ? m_graphGroup : m_groupId;
    b.title = title.isEmpty() ? tr("Backdrop") : title;
    b.titleSize = std::clamp(m_options.backdropTitleSize, 6, 72);
    b.color = backdropColours().value(int(backdrops().size()) % std::max<int>(1, int(backdropColours().size())), b.color);
    if (around.isNull())
        b.rect = QRectF(toGraph(QPointF(width() / 2.0, height() / 2.0)) - QPointF(150.0, 100.0), QSizeF(300.0, 200.0));
    else
        b.rect = around.adjusted(-30.0, -30.0 - (b.titleSize + 16.0), 30.0, 30.0);
    QVector<Backdrop> all = backdrops();
    all.push_back(b);
    setBackdrops(all, tr("Add Backdrop"));
    int const index = int(backdrops().size()) - 1;
    pickBackdrop(index, false);
    return index;
}

bool NodeView::editBackdrop(int index)
{
    if (index < 0 || index >= backdrops().size())
        return false;
    Backdrop b = backdrops()[index];
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("BackdropDialog"));
    dialog.setWindowTitle(tr("Backdrop Properties"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QLineEdit* const title = new QLineEdit(b.title, &dialog);
    title->setObjectName(QStringLiteral("BackdropTitle"));
    form->addRow(tr("Title"), title);
    QSpinBox* const size = new QSpinBox(&dialog);
    size->setObjectName(QStringLiteral("BackdropTitleSize"));
    size->setRange(6, 72);
    size->setValue(b.titleSize);
    form->addRow(tr("Title size"), size);
    QPlainTextEdit* const text = new QPlainTextEdit(b.text, &dialog);
    text->setObjectName(QStringLiteral("BackdropText"));
    text->setMinimumSize(320, 90);
    form->addRow(tr("Description"), text);
    QComboBox* const colour = new QComboBox(&dialog);
    colour->setObjectName(QStringLiteral("BackdropColour"));
    QVector<QColor> const offered = backdropColours();
    for (int c = 0; c < offered.size(); ++c) {
        QPixmap swatch(14, 14);
        swatch.fill(offered[c]);
        colour->addItem(QIcon(swatch), tr("Colour %1").arg(c + 1), offered[c]);
    }
    colour->addItem(tr("Own colour"), b.color);
    int const listed = colour->findData(b.color);
    colour->setCurrentIndex(listed >= 0 ? listed : colour->count() - 1);
    QPushButton* const own = new QPushButton(tr("Choose..."), &dialog);
    own->setObjectName(QStringLiteral("BackdropOwnColour"));
    connect(own, &QPushButton::clicked, &dialog, [&] {
        QColor const picked = ColourEditor::getColour(b.color, &dialog, tr("Backdrop Colour"));
        if (picked.isValid()) {
            colour->setItemData(colour->count() - 1, picked);
            colour->setCurrentIndex(colour->count() - 1);
        }
    });
    QHBoxLayout* const colourRow = new QHBoxLayout;
    colourRow->addWidget(colour, 1);
    colourRow->addWidget(own);
    form->addRow(tr("Colour"), colourRow);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    b.title = title->text();
    b.titleSize = size->value();
    b.text = text->toPlainText();
    b.color = colour->currentData().value<QColor>();
    QVector<Backdrop> all = backdrops();
    if (index >= all.size())
        return false;
    all[index] = b;
    setBackdrops(all, tr("Edit Backdrop"));
    return true;
}

void NodeView::paintBackdrops(QPainter& p)
{
    QVector<Backdrop> const& all = backdrops();
    for (int i = 0; i < all.size(); ++i) {
        QRectF const r = backdropRect(i);
        if (r.isNull())
            continue;
        Backdrop const& b = all[i];
        bool const picked = m_backdropPicked.contains(i) && !m_printing;
        QColor fill = b.color;
        fill.setAlpha(std::clamp(picked ? m_options.backdropSelected : m_options.backdropUnselected, 0, 255));
        p.save();
        p.setPen(picked ? QPen(InterfaceColours::get("nodeSelected"), 1.6) : Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(r, 6 * m_zoom, 6 * m_zoom);
        p.setPen(QPen(InterfaceColours::get("backdropInnerStroke"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(3 * m_zoom, 3 * m_zoom, -3 * m_zoom, -3 * m_zoom), 4 * m_zoom, 4 * m_zoom);
        // Its title along the top, its note under it.
        QFont font = this->font();
        font.setPointSizeF(std::max(4.0, b.titleSize * 0.75 * m_zoom));
        font.setBold(true);
        p.setFont(font);
        p.setPen(QColor(250, 250, 250));
        double const titleHeight = (b.titleSize + 12.0) * m_zoom;
        QRectF const titleRect(r.left() + 10 * m_zoom, r.top() + 4 * m_zoom, r.width() - 20 * m_zoom, titleHeight);
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(b.title, Qt::ElideRight, int(titleRect.width())));
        if (!b.text.isEmpty()) {
            font.setBold(false);
            font.setPointSizeF(std::max(4.0, this->font().pointSizeF() * m_zoom));
            p.setFont(font);
            p.setPen(QColor(235, 235, 235));
            p.drawText(QRectF(titleRect.left(), titleRect.bottom(), titleRect.width(), r.bottom() - titleRect.bottom() - 6 * m_zoom),
                       Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, b.text);
        }
        if (!m_printing) {
            // The grip at its bottom right corner: dragged, the backdrop grows or shrinks.
            QPainterPath grip;
            grip.moveTo(r.bottomRight());
            grip.lineTo(r.bottomRight() + QPointF(-12, 0));
            grip.lineTo(r.bottomRight() + QPointF(0, -12));
            grip.closeSubpath();
            p.fillPath(grip, InterfaceColours::get("backdropResize"));
        }
        p.restore();
    }
}

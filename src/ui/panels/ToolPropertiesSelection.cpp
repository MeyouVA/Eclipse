// Eclipse - Tool Properties: what the Select tool has selected - the commands that turn it over
// and round, and where it is and how big, as numbers that can be typed.
#include "ui/panels/ToolProperties.h"

#include <algorithm>

#include <QAction>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "actions/manager/ActionManager.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include <QFormLayout>

void ToolProperties::addSelectionRows()
{
    QWidget* const selection = new QWidget(this);
    selection->setObjectName(QStringLiteral("SelectionOptions"));
    QDoubleSpinBox* selBoxes[4] = {};
    QCheckBox* const selLock = new QCheckBox(tr("Lock"), selection);
    {
        QVBoxLayout* const column = new QVBoxLayout(selection);
        column->setContentsMargins(0, 4, 0, 4);
        // Round a shape drawn freehand, or in a rectangle.
        QHBoxLayout* const modes = new QHBoxLayout;
        modes->setSpacing(2);
        QToolButton* const lasso = new QToolButton(selection);
        lasso->setObjectName(QStringLiteral("SelLasso"));
        lasso->setText(tr("Lasso"));
        QToolButton* const marquee = new QToolButton(selection);
        marquee->setObjectName(QStringLiteral("SelMarquee"));
        marquee->setText(tr("Marquee"));
        for (QToolButton* b : {lasso, marquee}) {
            b->setCheckable(true);
            b->setAutoExclusive(true);
            modes->addWidget(b);
        }
        modes->addStretch(1);
        (m_canvas->lasso() ? lasso : marquee)->setChecked(true);
        connect(lasso, &QToolButton::toggled, m_canvas, &Canvas::setLasso);
        connect(m_canvas, &Canvas::lassoChanged, this, [lasso, marquee](bool on) { (on ? lasso : marquee)->setChecked(true); });
        column->addWidget(new QLabel(tr("Selection"), selection));
        column->addLayout(modes);
        column->addWidget(new QLabel(tr("Operations"), selection));
        QHBoxLayout* const operations = new QHBoxLayout;
        operations->setSpacing(2);
        for (char const* id : {"FlipHorizontal", "FlipVertical", "Rotate90CW", "Rotate90CCW", "Rotate180", "Flatten", "Smooth", "ReducePoints"}) {
            if (QAction* const a = m_actions->action(QLatin1String(id))) {
                QToolButton* const b = new QToolButton(selection);
                b->setObjectName(QStringLiteral("Op") + QLatin1String(id));
                b->setDefaultAction(a);
                b->setAutoRaise(true);
                b->setIconSize(QSize(20, 20));
                operations->addWidget(b);
            }
        }
        operations->addStretch(1);
        column->addLayout(operations);
        QGridLayout* const grid = new QGridLayout;
        char const* const names[4] = {"SelX", "SelY", "SelWidth", "SelHeight"};
        QString const labels[4] = {tr("X"), tr("Y"), tr("Width"), tr("Height")};
        for (int i = 0; i < 4; ++i) {
            QDoubleSpinBox* const box = new QDoubleSpinBox(selection);
            box->setObjectName(QLatin1String(names[i]));
            box->setDecimals(2);
            box->setRange(i < 2 ? -100000.0 : 0.01, 100000.0);
            box->setKeyboardTracking(false);    // a number is taken when it is done, not at each key
            grid->addWidget(new QLabel(labels[i], selection), i % 2, (i / 2) * 2);
            grid->addWidget(box, i % 2, (i / 2) * 2 + 1);
            selBoxes[i] = box;
        }
        {
            // Turns what is selected by so many degrees, then stands at nought again.
            QDoubleSpinBox* const angle = new QDoubleSpinBox(selection);
            angle->setObjectName(QStringLiteral("SelAngle"));
            angle->setDecimals(1);
            angle->setRange(-360.0, 360.0);
            angle->setSuffix(QStringLiteral(" deg"));
            angle->setKeyboardTracking(false);
            angle->setToolTip(tr("Type an angle to turn what is selected by it, clockwise"));
            grid->addWidget(new QLabel(tr("Angle"), selection), 2, 0);
            grid->addWidget(angle, 2, 1);
            connect(angle, &QDoubleSpinBox::valueChanged, this, [this, angle](double degrees) {
                if (degrees == 0.0)
                    return;
                m_canvas->rotateSelection(degrees);
                QSignalBlocker const block(angle);
                angle->setValue(0.0);
            });
        }
        selLock->setObjectName(QStringLiteral("SelLock"));
        selLock->setToolTip(tr("Keep the proportions when the width or the height is changed"));
        grid->addWidget(selLock, 2, 3);
        column->addLayout(grid);
    }
    m_form->addRow(selection);
    m_selection = selection;
    QDoubleSpinBox* const selX = selBoxes[0];
    QDoubleSpinBox* const selY = selBoxes[1];
    QDoubleSpinBox* const selW = selBoxes[2];
    QDoubleSpinBox* const selH = selBoxes[3];
    // The numbers follow what is selected (its middle, its width and height) ...
    auto showSelection = [this, selX, selY, selW, selH] {
        QRectF const box = m_canvas->selectionRect();
        bool const any = m_canvas->selecting() && !box.isNull();
        QDoubleSpinBox* const boxes[4] = {selX, selY, selW, selH};
        double const values[4] = {box.center().x(), box.center().y(), box.width(), box.height()};
        for (int i = 0; i < 4; ++i) {
            QSignalBlocker const block(boxes[i]);
            boxes[i]->setEnabled(any);
            boxes[i]->setValue(any ? values[i] : 0.0);
        }
    };
    // ... and what is selected follows a number typed.
    auto applySelection = [this, selX, selY, selW, selH, selLock, showSelection](int changed) {
        QRectF const now = m_canvas->selectionRect();
        if (now.isNull())
            return;
        double width = std::max(selW->value(), 0.01), height = std::max(selH->value(), 0.01);
        if (selLock->isChecked() && now.width() > 1e-9 && now.height() > 1e-9) {
            if (changed == 2)
                height = now.height() * width / now.width();
            else if (changed == 3)
                width = now.width() * height / now.height();
        }
        QRectF box(0, 0, now.width() > 1e-9 ? width : now.width(), now.height() > 1e-9 ? height : now.height());
        box.moveCenter(QPointF(selX->value(), selY->value()));
        if (box.width() > 0.0 && box.height() > 0.0)
            m_canvas->setSelectionRect(box);
        showSelection();
    };
    for (int i = 0; i < 4; ++i)
        connect(selBoxes[i], &QDoubleSpinBox::valueChanged, this, [applySelection, i] { applySelection(i); });
    connect(m_canvas, &Canvas::selectionChanged, this, showSelection);
    connect(m_document, &Document::changed, this, showSelection, Qt::QueuedConnection);
    connect(m_canvas, &Canvas::toolChanged, this, showSelection);
    showSelection();
}

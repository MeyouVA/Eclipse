// Eclipse - the main window: three views made of plain controls - Onion Skin, Message Log, Scene Markers.
#include "ui/window/MainWindow.h"
#include <QFrame>
#include <QLabel>
#include <QVector>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTime>
#include <QTreeWidget>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include "actions/manager/ActionManager.h"
#include "actions/dialogs/ShortcutsDialog.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/panels/LayerPanel.h"
#include "ui/io/MovieExport.h"
#include "ui/views/NodeView.h"
#include "ui/panels/PalettePanel.h"
#include "ui/panels/SubstitutionPanel.h"
#include "ui/timeline/Timeline.h"
#include "ui/window/ViewTitleBar.h"
#include "ui/window/WelcomeDialog.h"
#include "ui/timeline/FunctionView.h"
#include "ui/panels/LibraryView.h"
#include "ui/timeline/Xsheet.h"
#include "ui/views/DepthView.h"
#include "ui/views/LipSyncView.h"
#include "ui/views/PerspectiveView.h"

#include "ui/window/MainWindowPrivate.h"

// Onion Skin: on or off, how many drawings before and after, how faint.
QWidget* MainWindow::createOnionSkinView()
{
    QWidget* const onion = new QWidget(this);
    onion->setObjectName(QStringLiteral("OnionSkinView"));
    QFormLayout* const onionForm = new QFormLayout(onion);
    QCheckBox* const show = new QCheckBox(tr("Show Onion Skin"), onion);
    show->setObjectName(QStringLiteral("OnionShow"));
    QSpinBox* const previous = new QSpinBox(onion);
    previous->setObjectName(QStringLiteral("OnionPrevious"));
    previous->setRange(0, 6);
    QSpinBox* const next = new QSpinBox(onion);
    next->setObjectName(QStringLiteral("OnionNext"));
    next->setRange(0, 6);
    QSlider* const opacity = new QSlider(Qt::Horizontal, onion);
    opacity->setObjectName(QStringLiteral("OnionOpacity"));
    opacity->setRange(5, 100);
    onionForm->addRow(show);
    // A slider for each drawing shown: those before at the left (the furthest off first), those
    // after at the right (the nearest first), each setting how strongly that one drawing shows.
    QWidget* const bank = new QWidget(onion);
    bank->setObjectName(QStringLiteral("OnionBank"));
    QSlider* strengths[2][Canvas::kOnionSteps] = {};
    {
        QHBoxLayout* const across = new QHBoxLayout(bank);
        across->setContentsMargins(0, 0, 0, 0);
        across->setSpacing(3);
        auto one = [this, bank, across, &strengths](bool after, int step) {
            QSlider* const slider = new QSlider(Qt::Vertical, bank);
            slider->setObjectName((after ? QStringLiteral("OnionAfter%1") : QStringLiteral("OnionBefore%1")).arg(step + 1));
            slider->setRange(0, 100);
            slider->setFixedHeight(84);
            slider->setToolTip(after ? tr("How strongly drawing %1 after this one shows").arg(step + 1) : tr("How strongly drawing %1 before this one shows").arg(step + 1));
            QLabel* const number = new QLabel(QString::number(step + 1), bank);
            number->setAlignment(Qt::AlignHCenter);
            QVBoxLayout* const column = new QVBoxLayout;
            column->setSpacing(1);
            column->addWidget(number);
            column->addWidget(slider, 0, Qt::AlignHCenter);
            across->addLayout(column);
            strengths[after ? 1 : 0][step] = slider;
            connect(slider, &QSlider::valueChanged, this, [this, after, step](int percent) { m_canvas->setOnionStrength(after, step, percent); });
        };
        for (int step = Canvas::kOnionSteps - 1; step >= 0; --step)
            one(false, step);
        QFrame* const middle = new QFrame(bank);
        middle->setFrameShape(QFrame::VLine);
        across->addWidget(middle);
        for (int step = 0; step < Canvas::kOnionSteps; ++step)
            one(true, step);
        across->addStretch(1);
    }
    onionForm->addRow(bank);
    QVector<QSlider*> beforeSliders, afterSliders;
    for (int step = 0; step < Canvas::kOnionSteps; ++step) {
        beforeSliders << strengths[0][step];
        afterSliders << strengths[1][step];
    }
    onionForm->addRow(tr("Previous drawings"), previous);
    onionForm->addRow(tr("Next drawings"), next);
    onionForm->addRow(tr("Opacity"), opacity);
    QComboBox* const mode = new QComboBox(onion);
    mode->setObjectName(QStringLiteral("OnionMode"));
    mode->addItems({tr("By Drawing"), tr("By Frame")});
    mode->setToolTip(tr("By Drawing: the drawings before and after this one, however long each is held.\nBy Frame: the frames just before and after this one."));
    QPushButton* const beforeColour = new QPushButton(onion);
    beforeColour->setObjectName(QStringLiteral("OnionBefore"));
    QPushButton* const afterColour = new QPushButton(onion);
    afterColour->setObjectName(QStringLiteral("OnionAfter"));
    onionForm->addRow(tr("Show"), mode);
    onionForm->addRow(tr("Colour before"), beforeColour);
    onionForm->addRow(tr("Colour after"), afterColour);
    connect(mode, &QComboBox::currentIndexChanged, this, [this](int index) { m_canvas->setOnionByFrame(index == 1); });
    connect(beforeColour, &QPushButton::clicked, this, [this] {
        QColor const chosen = QColorDialog::getColor(m_canvas->onionBefore(), this, tr("Onion Skin: Before"));
        if (chosen.isValid())
            m_canvas->setOnionColours(chosen, m_canvas->onionAfter());
    });
    connect(afterColour, &QPushButton::clicked, this, [this] {
        QColor const chosen = QColorDialog::getColor(m_canvas->onionAfter(), this, tr("Onion Skin: After"));
        if (chosen.isValid())
            m_canvas->setOnionColours(m_canvas->onionBefore(), chosen);
    });
    auto follow = [this, show, previous, next, opacity, mode, beforeColour, afterColour, beforeSliders, afterSliders] {
        // A slider is live for a drawing that is shown: as many before and after as are asked for.
        for (int step = 0; step < Canvas::kOnionSteps; ++step) {
            QSignalBlocker const x(beforeSliders[step]), y(afterSliders[step]);
            beforeSliders[step]->setValue(m_canvas->onionStrength(false, step));
            afterSliders[step]->setValue(m_canvas->onionStrength(true, step));
            beforeSliders[step]->setEnabled(step < m_canvas->onionPrevious());
            afterSliders[step]->setEnabled(step < m_canvas->onionNext());
        }
        QSignalBlocker const a(show), b(previous), c(next), d(opacity), e(mode);
        mode->setCurrentIndex(m_canvas->onionByFrame() ? 1 : 0);
        beforeColour->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #202020; min-height: 16px;").arg(m_canvas->onionBefore().name()));
        afterColour->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #202020; min-height: 16px;").arg(m_canvas->onionAfter().name()));
        show->setChecked(m_canvas->onionSkin());
        previous->setValue(m_canvas->onionPrevious());
        next->setValue(m_canvas->onionNext());
        opacity->setValue(m_canvas->onionOpacity());
        // The command in the menus and on the toolbar is ticked with it.
        m_actions->setChecked(QStringLiteral("OnionSkinToggle"), m_canvas->onionSkin());
    };
    follow();
    {
        QSettings const s = settings();
        m_canvas->setOnionByFrame(s.value(QStringLiteral("onion/byFrame"), false).toBool());
        m_canvas->setOnionColours(QColor(s.value(QStringLiteral("onion/before"), m_canvas->onionBefore().name()).toString()),
                                  QColor(s.value(QStringLiteral("onion/after"), m_canvas->onionAfter().name()).toString()));
        QStringList const kept = s.value(QStringLiteral("onion/strengths")).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (kept.size() == 2 * Canvas::kOnionSteps)
            for (int i = 0; i < kept.size(); ++i)
                m_canvas->setOnionStrength(i >= Canvas::kOnionSteps, i % Canvas::kOnionSteps, kept[i].toInt());
    }
    follow();
    connect(m_canvas, &Canvas::onionChanged, this, follow);
    connect(m_canvas, &Canvas::onionChanged, this, [this] {
        QSettings s = settings();
        s.setValue(QStringLiteral("onion/byFrame"), m_canvas->onionByFrame());
        s.setValue(QStringLiteral("onion/before"), m_canvas->onionBefore().name());
        s.setValue(QStringLiteral("onion/after"), m_canvas->onionAfter().name());
        QStringList strengths;
        for (int i = 0; i < 2 * Canvas::kOnionSteps; ++i)
            strengths << QString::number(m_canvas->onionStrength(i >= Canvas::kOnionSteps, i % Canvas::kOnionSteps));
        s.setValue(QStringLiteral("onion/strengths"), strengths.join(QLatin1Char(',')));
    });
    connect(show, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setOnionSkin(on);
    });
    connect(previous, &QSpinBox::valueChanged, this, [this](int v) { m_canvas->setOnionRange(v, m_canvas->onionNext()); });
    connect(next, &QSpinBox::valueChanged, this, [this](int v) { m_canvas->setOnionRange(m_canvas->onionPrevious(), v); });
    connect(opacity, &QSlider::valueChanged, m_canvas, &Canvas::setOnionOpacity);
    return onion;
}

// Message Log: what the program has said, with the time it said it.
QWidget* MainWindow::createMessageLogView()
{
    QWidget* const log = new QWidget(this);
    QVBoxLayout* const logLayout = new QVBoxLayout(log);
    logLayout->setContentsMargins(4, 4, 4, 4);
    QPlainTextEdit* const text = new QPlainTextEdit(log);
    text->setObjectName(QStringLiteral("MessageLog"));
    text->setReadOnly(true);
    text->setMaximumBlockCount(2000);
    QPushButton* const clear = new QPushButton(tr("Clear"), log);
    clear->setObjectName(QStringLiteral("MessageLogClear"));
    logLayout->addWidget(text, 1);
    logLayout->addWidget(clear, 0, Qt::AlignRight);
    connect(clear, &QPushButton::clicked, text, &QPlainTextEdit::clear);
    connect(statusBar(), &QStatusBar::messageChanged, text, [text](QString const& message) {
        if (!message.isEmpty())
            text->appendPlainText(QStringLiteral("%1  %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), message));
    });
    return log;
}

// Scene Markers: the marks on the ruler as a list; a double click goes to one.
QWidget* MainWindow::createSceneMarkersView()
{
    QWidget* const markersView = new QWidget(this);
    QTreeWidget* const markers = new QTreeWidget(markersView);
    markers->setObjectName(QStringLiteral("SceneMarkers"));
    markers->setRootIsDecorated(false);
    markers->setHeaderLabels({tr("Name"), tr("Start"), tr("Length")});
    auto list = [this, markers] {
        markers->clear();
        for (Scene::Marker const& m : m_document->scene().markers) {
            QTreeWidgetItem* const item = new QTreeWidgetItem(markers, {m.name, QString::number(m.frame + 1), QString::number(m.length)});
            QPixmap swatch(12, 12);
            swatch.fill(m.color);
            item->setIcon(0, QIcon(swatch));
            item->setData(0, Qt::UserRole, m.frame);
        }
    };
    list();
    connect(m_document, &Document::changed, markers, list);
    connect(markers, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) { m_timeline->setCurrentFrame(item->data(0, Qt::UserRole).toInt()); });
    connect(markers, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) { m_timeline->setCurrentFrame(item->data(0, Qt::UserRole).toInt()); });
    {
        // Under the list: a marker made on the frames selected in the Timeline; the one picked changed or taken away.
        QVBoxLayout* const column = new QVBoxLayout(markersView);
        column->setContentsMargins(4, 4, 4, 4);
        column->addWidget(markers, 1);
        QHBoxLayout* const row = new QHBoxLayout;
        auto button = [&](QString const& text, char const* name, void (MainWindow::*slot)()) {
            QPushButton* const b = new QPushButton(text, markersView);
            b->setObjectName(QLatin1String(name));
            connect(b, &QPushButton::clicked, this, slot);
            row->addWidget(b);
        };
        button(tr("Add..."), "MarkerAdd", &MainWindow::onActionCreateSceneMarker);
        button(tr("Edit..."), "MarkerEdit", &MainWindow::onActionEditSceneMarker);
        button(tr("Delete"), "MarkerDelete", &MainWindow::onActionDeleteSceneMarker);
        row->addStretch();
        column->addLayout(row);
    }
    return markersView;
}

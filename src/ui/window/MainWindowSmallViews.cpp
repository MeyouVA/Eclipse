// Eclipse - the main window: three views made of plain controls - Onion Skin, Message Log, Scene Markers.
#include "ui/window/MainWindow.h"
#include <QLineEdit>
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
#include <QToolButton>
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
    QVector<QToolButton*> stepColours[2];
    stepColours[0].resize(Canvas::kOnionSteps);
    stepColours[1].resize(Canvas::kOnionSteps);
    {
        QHBoxLayout* const across = new QHBoxLayout(bank);
        across->setContentsMargins(0, 0, 0, 0);
        across->setSpacing(3);
        auto one = [this, bank, across, &strengths, &stepColours](bool after, int step) {
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
            // Under it, the colour that drawing shows in: pressed, one of its own is chosen; pressed with the
            // right button, it goes back to the colour of its side.
            QToolButton* const colour = new QToolButton(bank);
            colour->setObjectName((after ? QStringLiteral("OnionAfterColour%1") : QStringLiteral("OnionBeforeColour%1")).arg(step + 1));
            colour->setFixedSize(16, 12);
            colour->setContextMenuPolicy(Qt::CustomContextMenu);
            colour->setToolTip(tr("The colour this drawing shows in. Click to choose one of its own; right-click to go back to the colour of its side."));
            column->addWidget(colour, 0, Qt::AlignHCenter);
            stepColours[after ? 1 : 0][step] = colour;
            connect(colour, &QToolButton::clicked, this, [this, after, step] {
                QColor const chosen = QColorDialog::getColor(m_canvas->onionStepColour(after, step), this, tr("Onion Skin: Drawing %1").arg(step + 1));
                if (chosen.isValid())
                    m_canvas->setOnionStepColour(after, step, chosen);
            });
            connect(colour, &QToolButton::customContextMenuRequested, this, [this, after, step] { m_canvas->setOnionStepColour(after, step, QColor()); });
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
    QComboBox* const marks = new QComboBox(onion);
    marks->setObjectName(QStringLiteral("OnionMarks"));
    marks->addItems({tr("All Drawings"), tr("Key Drawings"), tr("Breakdown Drawings"), tr("In-between Drawings")});
    marks->setToolTip(tr("Which drawings are shown before and after: all of them, or only those marked so (Drawing > Mark Drawing As)."));
    onionForm->addRow(tr("Of"), marks);
    connect(marks, &QComboBox::currentIndexChanged, m_canvas, &Canvas::setOnionMark);
    QCheckBox* const outlines = new QCheckBox(tr("Outlines only"), onion);
    outlines->setObjectName(QStringLiteral("OnionOutlines"));
    outlines->setToolTip(tr("The drawings before and after are shown as outlines, so that they do not hide what is being drawn."));
    onionForm->addRow(outlines);
    connect(outlines, &QCheckBox::toggled, m_canvas, &Canvas::setOnionOutlines);
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
    QVector<QToolButton*> const beforeSteps = stepColours[0], afterSteps = stepColours[1];
    auto follow = [this, show, previous, next, opacity, mode, outlines, marks, beforeColour, afterColour, beforeSliders, afterSliders, beforeSteps, afterSteps] {
        for (int step = 0; step < Canvas::kOnionSteps; ++step) {
            for (bool after : {false, true}) {
                QToolButton* const button = (after ? afterSteps : beforeSteps)[step];
                // One of its own has a light edge; the colour of the side, a dark one.
                button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid %2;")
                                          .arg(m_canvas->onionStepColour(after, step).name(), m_canvas->onionStepHasColour(after, step) ? QStringLiteral("#e0e0e0") : QStringLiteral("#202020")));
                button->setEnabled(step < (after ? m_canvas->onionNext() : m_canvas->onionPrevious()));
            }
        }
        {
            QSignalBlocker const m(marks);
            marks->setCurrentIndex(m_canvas->onionMark());
        }
        {
            QSignalBlocker const o(outlines);
            outlines->setChecked(m_canvas->onionOutlines());
        }
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
        m_canvas->setOnionOutlines(s.value(QStringLiteral("onion/outlines"), false).toBool());
        m_canvas->setOnionColours(QColor(s.value(QStringLiteral("onion/before"), m_canvas->onionBefore().name()).toString()),
                                  QColor(s.value(QStringLiteral("onion/after"), m_canvas->onionAfter().name()).toString()));
        QStringList const kept = s.value(QStringLiteral("onion/strengths")).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (kept.size() == 2 * Canvas::kOnionSteps)
            for (int i = 0; i < kept.size(); ++i)
                m_canvas->setOnionStrength(i >= Canvas::kOnionSteps, i % Canvas::kOnionSteps, kept[i].toInt());
        QStringList const own = s.value(QStringLiteral("onion/stepColours")).toString().split(QLatin1Char(','));
        if (own.size() == 2 * Canvas::kOnionSteps)
            for (int i = 0; i < own.size(); ++i)
                m_canvas->setOnionStepColour(i >= Canvas::kOnionSteps, i % Canvas::kOnionSteps, own[i].isEmpty() ? QColor() : QColor(own[i]));
    }
    follow();
    connect(m_canvas, &Canvas::onionChanged, this, follow);
    connect(m_canvas, &Canvas::onionChanged, this, [this] {
        QSettings s = settings();
        s.setValue(QStringLiteral("onion/byFrame"), m_canvas->onionByFrame());
        s.setValue(QStringLiteral("onion/outlines"), m_canvas->onionOutlines());
        s.setValue(QStringLiteral("onion/before"), m_canvas->onionBefore().name());
        s.setValue(QStringLiteral("onion/after"), m_canvas->onionAfter().name());
        QStringList strengths;
        for (int i = 0; i < 2 * Canvas::kOnionSteps; ++i)
            strengths << QString::number(m_canvas->onionStrength(i >= Canvas::kOnionSteps, i % Canvas::kOnionSteps));
        s.setValue(QStringLiteral("onion/strengths"), strengths.join(QLatin1Char(',')));
        QStringList own;
        for (int i = 0; i < 2 * Canvas::kOnionSteps; ++i) {
            bool const after = i >= Canvas::kOnionSteps;
            own << (m_canvas->onionStepHasColour(after, i % Canvas::kOnionSteps) ? m_canvas->onionStepColour(after, i % Canvas::kOnionSteps).name() : QString());
        }
        s.setValue(QStringLiteral("onion/stepColours"), own.join(QLatin1Char(',')));
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
    // In Debug Mode: the commands as they are run, and the steps of undo as they come and go.
    auto note = [this, text](QString const& line) {
        if (m_debugMode)
            text->appendPlainText(QStringLiteral("%1  [debug] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")), line));
    };
    connect(m_actions, &ActionManager::commandRun, text, [note](QString const& id, qint64 milliseconds) {
        note(QStringLiteral("command %1 (%2 ms)").arg(id).arg(milliseconds));
    });
    connect(m_document->undoStack(), &QUndoStack::indexChanged, text, [this, note](int index) {
        QUndoStack const* const stack = m_document->undoStack();
        note(QStringLiteral("undo: step %1 of %2%3").arg(index).arg(stack->count()).arg(index > 0 ? QStringLiteral(" - ") + stack->text(index - 1) : QString()));
    });
    return log;
}

bool MainWindow::applyBreakdown(double percent)
{
    Scene const& scene = m_document->scene();
    if (layer() < 0 || layer() >= scene.layers.size())
        return false;
    // The keyframes either side of this frame (one on the frame itself is the one being set).
    Keyframe const* before = nullptr;
    Keyframe const* after = nullptr;
    for (Keyframe const& key : scene.layers[layer()].keys) {
        if (key.frame < frame())
            before = &key;
        else if (key.frame > frame() && !after)
            after = &key;
    }
    if (!before || !after) {
        statusBar()->showMessage(tr("A breakdown goes between two keyframes: this frame has none before it, or none after it."), 5000);
        return false;
    }
    Pose pose = Pose::lerp(before->pose, after->pose, percent / 100.0);
    pose.opacity = std::clamp(pose.opacity, 0.0, 1.0);
    m_document->setKeyframe(layer(), frame(), pose, tr("Breakdown"));
    return true;
}

// Breakdown Assistant: a keyframe on this frame, a chosen part of the way between its neighbours.
QWidget* MainWindow::createBreakdownView()
{
    QWidget* const view = new QWidget(this);
    view->setObjectName(QStringLiteral("BreakdownView"));
    QVBoxLayout* const column = new QVBoxLayout(view);
    column->setContentsMargins(6, 6, 6, 6);
    QLabel* const about = new QLabel(tr("A keyframe on this frame, so far from the keyframe before it to the keyframe after it:"), view);
    about->setWordWrap(true);
    QCheckBox* const overshoot = new QCheckBox(tr("Allow Overshoot"), view);
    overshoot->setObjectName(QStringLiteral("BreakdownOvershoot"));
    overshoot->setToolTip(tr("Lets the amount go below 0 and over 100: beyond the two keyframes."));
    overshoot->setChecked(true);
    QSlider* const amount = new QSlider(Qt::Horizontal, view);
    amount->setObjectName(QStringLiteral("BreakdownAmount"));
    amount->setRange(-125, 225);
    amount->setValue(50);
    QSpinBox* const number = new QSpinBox(view);
    number->setObjectName(QStringLiteral("BreakdownPercent"));
    number->setRange(-125, 225);
    number->setSuffix(QStringLiteral(" %"));
    number->setValue(50);
    connect(amount, &QSlider::valueChanged, number, &QSpinBox::setValue);
    connect(number, &QSpinBox::valueChanged, amount, &QSlider::setValue);
    auto bound = [overshoot, amount, number] {
        int const low = overshoot->isChecked() ? -125 : 0, high = overshoot->isChecked() ? 225 : 100;
        amount->setRange(low, high);
        number->setRange(low, high);
    };
    connect(overshoot, &QCheckBox::toggled, view, bound);
    QHBoxLayout* const row = new QHBoxLayout;
    row->addWidget(amount, 1);
    row->addWidget(number);
    // The amounts wanted most often, a press each.
    QHBoxLayout* const quick = new QHBoxLayout;
    quick->setSpacing(2);
    for (int percent : {-25, 0, 25, 33, 50, 67, 75, 100, 125}) {
        QPushButton* const b = new QPushButton(QString::number(percent), view);
        b->setObjectName(QStringLiteral("Breakdown%1").arg(percent < 0 ? QStringLiteral("Minus%1").arg(-percent) : QString::number(percent)));
        b->setMinimumWidth(30);
        connect(b, &QPushButton::clicked, this, [this, percent, number, overshoot] {
            if ((percent < 0 || percent > 100) && !overshoot->isChecked())
                return;
            number->setValue(percent);
            applyBreakdown(percent);
        });
        quick->addWidget(b);
    }
    QPushButton* const apply = new QPushButton(tr("Apply"), view);
    apply->setObjectName(QStringLiteral("BreakdownApply"));
    connect(apply, &QPushButton::clicked, this, [this, number] { applyBreakdown(number->value()); });
    column->addWidget(about);
    column->addWidget(overshoot);
    column->addLayout(row);
    column->addLayout(quick);
    column->addWidget(apply, 0, Qt::AlignRight);
    column->addStretch(1);
    return view;
}

// Scene Markers: the marks on the ruler as a list; a double click goes to one.
QWidget* MainWindow::createSceneMarkersView()
{
    QWidget* const markersView = new QWidget(this);
    QTreeWidget* const markers = new QTreeWidget(markersView);
    markers->setObjectName(QStringLiteral("SceneMarkers"));
    markers->setRootIsDecorated(false);
    markers->setHeaderLabels({tr("Name"), tr("Start"), tr("Length"), tr("Note")});
    // Over the list: only the markers whose name or note has the words typed.
    QLineEdit* const search = new QLineEdit(markersView);
    search->setObjectName(QStringLiteral("MarkerSearch"));
    search->setPlaceholderText(tr("Search in names and notes"));
    search->setClearButtonEnabled(true);
    auto list = [this, markers, search] {
        markers->clear();
        QString const wanted = search->text().trimmed();
        for (Scene::Marker const& m : m_document->scene().markers) {
            if (!wanted.isEmpty() && !m.name.contains(wanted, Qt::CaseInsensitive) && !m.note.contains(wanted, Qt::CaseInsensitive))
                continue;
            QTreeWidgetItem* const item = new QTreeWidgetItem(markers, {m.name, QString::number(m.frame + 1), QString::number(m.length), QString(m.note).replace(QChar(0x0a), QChar(0x20))});
            item->setToolTip(3, m.note);
            QPixmap swatch(12, 12);
            swatch.fill(m.color);
            item->setIcon(0, QIcon(swatch));
            item->setData(0, Qt::UserRole, m.frame);
        }
    };
    list();
    connect(m_document, &Document::changed, markers, list);
    connect(search, &QLineEdit::textChanged, markers, list);
    connect(markers, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) { m_timeline->setCurrentFrame(item->data(0, Qt::UserRole).toInt()); });
    connect(markers, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) { m_timeline->setCurrentFrame(item->data(0, Qt::UserRole).toInt()); });
    {
        // Under the list: a marker made on the frames selected in the Timeline; the one picked changed or taken away.
        QVBoxLayout* const column = new QVBoxLayout(markersView);
        column->setContentsMargins(4, 4, 4, 4);
        column->addWidget(search);
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
        button(tr("Note..."), "MarkerNote", &MainWindow::onActionSceneMarkerNote);
        button(tr("Delete"), "MarkerDelete", &MainWindow::onActionDeleteSceneMarker);
        row->addStretch();
        column->addLayout(row);
    }
    return markersView;
}

// Eclipse - Storyboard view (see StoryboardView.h).
#include "animation/ui/views/StoryboardView.h"

#include "canvas/FrameRender.h"
#include "model/document/Commands.h"
#include "model/scene/Scene.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
enum Column { NameColumn, SequenceColumn, ActColumn, StartColumn, DurationColumn, TransitionColumn, CaptionColumn, ColumnCount };
QSettings prefs()
{
    return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
}
}

Storyboard::Naming StoryboardView::naming()
{
    QSettings const s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    Storyboard::Naming n;
    n.customPanelNames = s.value(QStringLiteral("prefs/stCustomPanelNames"), false).toBool();
    n.panelRule = std::clamp(s.value(QStringLiteral("prefs/stPanelNameRule"), 0).toInt(), 0, 2);
    n.showTotal = s.value(QStringLiteral("prefs/stPanelNameTotal"), true).toBool();
    n.shotLeadingZeros = s.value(QStringLiteral("prefs/stShotLeadingZeros"), false).toBool();
    n.shotZeros = std::clamp(s.value(QStringLiteral("prefs/stShotZeros"), 2).toInt(), 1, 8);
    n.acts = s.value(QStringLiteral("prefs/stEnableAct"), false).toBool();
    return n;
}

bool StoryboardView::timecodeDurations()
{
    return prefs().value(QStringLiteral("prefs/stDisplayTimecode"), false).toBool();
}

QGroupBox* StoryboardView::preferencesPage(QWidget* parent, std::function<void()>* save)
{
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    auto* const page = new QGroupBox(tr("Storyboard"), parent);
    page->setObjectName(QStringLiteral("PrefStoryboardPage"));
    auto* const form = new QFormLayout(page);
    auto check = [&](QString const& name, QString const& text, QString const& key, bool initial, QString const& tip) {
        auto* const box = new QCheckBox(text, page);
        box->setObjectName(name);
        box->setToolTip(tip);
        box->setChecked(s.value(key, initial).toBool());
        form->addRow(box);
        return box;
    };
    auto colour = [&](QString const& name, QString const& label, QString const& key, QColor const& initial, QString const& tip) {
        auto* const button = new QPushButton(page);
        button->setObjectName(name);
        button->setToolTip(tip);
        auto show = [button](QColor const& c) {
            button->setProperty("colour", c);
            button->setText(c.name(QColor::HexArgb));
            button->setStyleSheet(QStringLiteral("background-color: %1;").arg(c.name(QColor::HexArgb)));
        };
        show(s.value(key, initial).value<QColor>());
        connect(button, &QPushButton::clicked, button, [button, show, label] {
            QColor const c = QColorDialog::getColor(button->property("colour").value<QColor>(), button, label, QColorDialog::ShowAlphaChannel);
            if (c.isValid())
                show(c);
        });
        form->addRow(label, button);
        return button;
    };
    QPushButton* const current = colour(QStringLiteral("PrefStPanelCurrentColour"), tr("Current panel colour"), QStringLiteral("prefs/stPanelCurrentColour"),
                                        QColor(255, 140, 0, 90), tr("The colour used to show the current panel (the one the current frame is in)."));
    QPushButton* const selected = colour(QStringLiteral("PrefStPanelSelectedColour"), tr("Selected panel colour"), QStringLiteral("prefs/stPanelSelectedColour"),
                                         QColor(60, 130, 230, 120), tr("The colour used to show the panel picked in the Storyboard view."));
    QCheckBox* const timecode = check(QStringLiteral("PrefStDisplayTimecode"), tr("Display durations in time code"), QStringLiteral("prefs/stDisplayTimecode"), false,
                                      tr("Starts and durations in the Storyboard view as time codes (hh:mm:ss:ff), else as frames. A duration may be typed either way."));
    QCheckBox* const custom = check(QStringLiteral("PrefStCustomPanelNames"), tr("Allow user to name panels"), QStringLiteral("prefs/stCustomPanelNames"), false,
                                    tr("A panel keeps a name typed for it; otherwise panels are named automatically."));
    auto* const rule = new QComboBox(page);
    rule->setObjectName(QStringLiteral("PrefStPanelNameRule"));
    rule->addItems({tr("Numbers (1, 2, 3)"), tr("Letters (A, B, C)"), tr("Shot and number (12-1, 12-2)")});
    rule->setCurrentIndex(std::clamp(s.value(QStringLiteral("prefs/stPanelNameRule"), 0).toInt(), 0, 2));
    rule->setToolTip(tr("Rule for automatic panel naming."));
    form->addRow(tr("Rule for panel naming"), rule);
    QCheckBox* const total = check(QStringLiteral("PrefStPanelNameTotal"), tr("Display total in automatic panel name"), QStringLiteral("prefs/stPanelNameTotal"), true,
                                   tr("Automatic panel names show how many panels the shot has: 2/3."));
    QCheckBox* const zeros = check(QStringLiteral("PrefStShotLeadingZeros"), tr("Automatically add leading zeros when naming shots"), QStringLiteral("prefs/stShotLeadingZeros"), false,
                                   tr("When naming shots, leading zeros are added if the name starts with digits (5 becomes 05)."));
    auto* const count = new QSpinBox(page);
    count->setObjectName(QStringLiteral("PrefStShotZeros"));
    count->setRange(1, 8);
    count->setValue(s.value(QStringLiteral("prefs/stShotZeros"), 2).toInt());
    count->setToolTip(tr("Number of characters to use for automatic leading zeros."));
    form->addRow(tr("Digits in shot numbers"), count);
    QCheckBox* const acts = check(QStringLiteral("PrefStEnableAct"), tr("Enable acts"), QStringLiteral("prefs/stEnableAct"), false,
                                  tr("Shots are grouped into acts as well as sequences: the Storyboard view shows an Act column."));
    auto* const delay = new QSpinBox(page);
    delay->setObjectName(QStringLiteral("PrefStThumbnailDelay"));
    delay->setRange(0, 60000);
    delay->setSingleStep(250);
    delay->setSuffix(tr(" ms"));
    delay->setValue(s.value(QStringLiteral("prefs/stThumbnailDelay"), 2000).toInt());
    delay->setToolTip(tr("Delay before updating the panel picture after drawing modifications."));
    form->addRow(tr("Thumbnail update delay"), delay);
    *save = [=] {
        QSettings out(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
        out.setValue(QStringLiteral("prefs/stPanelCurrentColour"), current->property("colour").value<QColor>());
        out.setValue(QStringLiteral("prefs/stPanelSelectedColour"), selected->property("colour").value<QColor>());
        out.setValue(QStringLiteral("prefs/stDisplayTimecode"), timecode->isChecked());
        out.setValue(QStringLiteral("prefs/stCustomPanelNames"), custom->isChecked());
        out.setValue(QStringLiteral("prefs/stPanelNameRule"), rule->currentIndex());
        out.setValue(QStringLiteral("prefs/stPanelNameTotal"), total->isChecked());
        out.setValue(QStringLiteral("prefs/stShotLeadingZeros"), zeros->isChecked());
        out.setValue(QStringLiteral("prefs/stShotZeros"), count->value());
        out.setValue(QStringLiteral("prefs/stEnableAct"), acts->isChecked());
        out.setValue(QStringLiteral("prefs/stThumbnailDelay"), delay->value());
    };
    return page;
}

StoryboardView::StoryboardView(Document* document, QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    auto* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* const titles = new QHBoxLayout;
    m_title = new QLineEdit(this);
    m_title->setObjectName(QStringLiteral("StoryboardTitle"));
    m_title->setPlaceholderText(tr("Project Title"));
    m_subtitle = new QLineEdit(this);
    m_subtitle->setObjectName(QStringLiteral("StoryboardSubtitle"));
    m_subtitle->setPlaceholderText(tr("Project Subtitle"));
    titles->addWidget(m_title);
    titles->addWidget(m_subtitle);
    layout->addLayout(titles);

    auto* const buttons = new QHBoxLayout;
    auto button = [&](QString const& name, QString const& text, QString const& tip, auto slot) {
        auto* const b = new QPushButton(text, this);
        b->setObjectName(name);
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, slot);
        buttons->addWidget(b);
        return b;
    };
    button(QStringLiteral("StoryboardNewShot"), tr("New Shot"), tr("A shot after the one picked, a second long."), [this] { newShot(); });
    button(QStringLiteral("StoryboardNewPanel"), tr("New Panel"), tr("The panel at the current frame split in two there."), [this] { newPanel(); });
    button(QStringLiteral("StoryboardSplitShot"), tr("Split Shot"), tr("A new shot from the current frame (the first frame of one of its panels)."), [this] { splitShot(); });
    button(QStringLiteral("StoryboardJoinShot"), tr("Join"), tr("The shot picked joined to the one before it."), [this] { joinShot(); });
    button(QStringLiteral("StoryboardDelete"), tr("Delete"), tr("The shot or panel picked deleted."), [this] { deletePicked(); });
    button(QStringLiteral("StoryboardRenumber"), tr("Renumber"), tr("Every shot named again in order: 1, 2, 3 (with leading zeros when the preferences say)."), [this] { renumberShots(); });
    button(QStringLiteral("StoryboardFit"), tr("Fit to Scene"), tr("The last panel made longer or shorter so the shots end with the scene."), [this] { fitToScene(); });
    buttons->addStretch();
    layout->addLayout(buttons);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("StoryboardList"));
    m_tree->setColumnCount(ColumnCount);
    m_tree->setHeaderLabels({tr("Shot / Panel"), tr("Sequence"), tr("Act"), tr("Start"), tr("Duration"), tr("Transition"), tr("Captions")});
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_tree->header()->setStretchLastSection(true);
    layout->addWidget(m_tree, 1);

    auto* const bottom = new QHBoxLayout;
    m_thumbnail = new QLabel(this);
    m_thumbnail->setObjectName(QStringLiteral("StoryboardThumbnail"));
    m_thumbnail->setFixedSize(192, 108);
    m_thumbnail->setAlignment(Qt::AlignCenter);
    m_thumbnail->setFrameShape(QFrame::Box);
    bottom->addWidget(m_thumbnail);
    auto* const captionSide = new QVBoxLayout;
    auto* const fieldRow = new QHBoxLayout;
    m_field = new QComboBox(this);
    m_field->setObjectName(QStringLiteral("StoryboardCaptionField"));
    auto* const addField = new QPushButton(tr("Add Field..."), this);
    addField->setObjectName(QStringLiteral("StoryboardAddField"));
    fieldRow->addWidget(new QLabel(tr("Caption:"), this));
    fieldRow->addWidget(m_field, 1);
    fieldRow->addWidget(addField);
    captionSide->addLayout(fieldRow);
    m_caption = new QPlainTextEdit(this);
    m_caption->setObjectName(QStringLiteral("StoryboardCaption"));
    m_caption->setPlaceholderText(tr("The caption of the panel picked."));
    captionSide->addWidget(m_caption);
    bottom->addLayout(captionSide, 1);
    layout->addLayout(bottom);

    m_thumbnailTimer = new QTimer(this);
    m_thumbnailTimer->setSingleShot(true);
    connect(m_thumbnailTimer, &QTimer::timeout, this, &StoryboardView::refreshThumbnail);

    connect(m_title, &QLineEdit::editingFinished, this, [this] {
        if (m_title->text() != m_document->scene().storyboard.title)
            edit(tr("Storyboard Title"), [t = m_title->text()](Storyboard& b) { b.title = t; });
    });
    connect(m_subtitle, &QLineEdit::editingFinished, this, [this] {
        if (m_subtitle->text() != m_document->scene().storyboard.subtitle)
            edit(tr("Storyboard Subtitle"), [t = m_subtitle->text()](Storyboard& b) { b.subtitle = t; });
    });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] {
        if (m_rebuilding)
            return;
        QPair<int, int> const at = picked();
        if (at.first >= 0)
            emit frameRequested(m_document->scene().storyboard.panelStart(at.first, std::max(0, at.second)));
        showCaption();
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (m_rebuilding)
            return;
        int const shot = item->data(0, Qt::UserRole).toInt(), panel = item->data(0, Qt::UserRole + 1).toInt();
        QString const text = item->text(column).trimmed();
        if (panel < 0) {
            if (column == NameColumn)
                edit(tr("Rename Shot"), [&](Storyboard& b) { b.shots[shot].name = Storyboard::shotName(text, naming()); });
            else if (column == SequenceColumn)
                edit(tr("Shot Sequence"), [&](Storyboard& b) { b.shots[shot].sequence = text; });
            else if (column == ActColumn)
                edit(tr("Shot Act"), [&](Storyboard& b) { b.shots[shot].act = text; });
            else if (column == TransitionColumn)
                edit(tr("Shot Transition"), [&](Storyboard& b) { b.shots[shot].transition = text; });
        } else if (column == NameColumn) {
            // (Panels keep a name of their own only where the preferences allow it; else the automatic one comes back.)
            QString name = text;
            if (name.startsWith(tr("Panel ")))
                name = name.mid(tr("Panel ").size()).trimmed();
            if (naming().customPanelNames)
                edit(tr("Rename Panel"), [&](Storyboard& b) { b.shots[shot].panels[panel].name = name; });
            else
                QTimer::singleShot(0, this, &StoryboardView::rebuild);
        } else if (column == DurationColumn) {
            int frames = text.toInt();
            if (text.contains(u':')) {
                // A time code: hh:mm:ss:ff.
                QStringList const parts = text.split(u':');
                int const rate = std::max(1, int(std::lround(m_document->scene().fps)));
                int seconds = 0;
                for (int p = 0; p + 1 < parts.size(); ++p)
                    seconds = seconds * 60 + parts[p].toInt();
                frames = seconds * rate + parts.last().toInt();
            }
            edit(tr("Panel Duration"), [&](Storyboard& b) { b.setPanelLength(shot, panel, frames); });
        }
    });
    connect(m_field, &QComboBox::currentIndexChanged, this, &StoryboardView::showCaption);
    connect(addField, &QPushButton::clicked, this, [this] {
        bool ok = false;
        QString const name = QInputDialog::getText(this, tr("Add Caption Field"), tr("Name:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if (ok && !name.isEmpty() && !m_document->scene().storyboard.captionFields.contains(name, Qt::CaseInsensitive))
            edit(tr("Add Caption Field"), [&](Storyboard& b) { b.captionFields << name; });
    });
    connect(m_caption, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_rebuilding)
            setCaption(m_field->currentText(), m_caption->toPlainText());
    });
    connect(m_document, &Document::changed, this, &StoryboardView::rebuild);
    rebuild();
}

void StoryboardView::edit(QString const& text, std::function<void(Storyboard&)> const& change)
{
    m_document->editScene(text, [&](Scene& scene) { change(scene.storyboard); });
}

QPair<int, int> StoryboardView::picked() const
{
    QTreeWidgetItem const* item = m_tree->currentItem();
    if (!item)
        return {-1, -1};
    return {item->data(0, Qt::UserRole).toInt(), item->data(0, Qt::UserRole + 1).toInt()};
}

void StoryboardView::pick(int shot, int panel)
{
    if (shot < 0 || shot >= m_tree->topLevelItemCount())
        return;
    QTreeWidgetItem* item = m_tree->topLevelItem(shot);
    if (panel >= 0 && panel < item->childCount())
        item = item->child(panel);
    m_tree->setCurrentItem(item);
}

void StoryboardView::rebuild()
{
    Scene const& scene = m_document->scene();
    Storyboard const& board = scene.storyboard;
    Storyboard::Naming const names = naming();
    bool const tc = timecodeDurations();
    QPair<int, int> const was = picked();
    m_rebuilding = true;
    if (!m_title->hasFocus())
        m_title->setText(board.title);
    if (!m_subtitle->hasFocus())
        m_subtitle->setText(board.subtitle);
    m_tree->setColumnHidden(ActColumn, !names.acts);
    m_tree->clear();
    QSettings const s = prefs();
    QColor const current = s.value(QStringLiteral("prefs/stPanelCurrentColour"), QColor(255, 140, 0, 90)).value<QColor>();
    QColor const selectedColour = s.value(QStringLiteral("prefs/stPanelSelectedColour"), QColor(60, 130, 230, 120)).value<QColor>();
    m_tree->setStyleSheet(QStringLiteral("QTreeWidget::item:selected { background: %1; color: palette(text); }").arg(selectedColour.name(QColor::HexArgb)));
    QPair<int, int> const here = board.panelAt(m_frame);
    for (int sh = 0; sh < board.shots.size(); ++sh) {
        Storyboard::Shot const& shot = board.shots[sh];
        auto* const top = new QTreeWidgetItem(m_tree);
        top->setFlags(top->flags() | Qt::ItemIsEditable);
        top->setData(0, Qt::UserRole, sh);
        top->setData(0, Qt::UserRole + 1, -1);
        top->setText(NameColumn, shot.name);
        top->setText(SequenceColumn, shot.sequence);
        top->setText(ActColumn, shot.act);
        top->setText(StartColumn, Storyboard::duration(board.shotStart(sh), scene.fps, tc));
        top->setText(DurationColumn, Storyboard::duration(shot.length(), scene.fps, tc));
        top->setText(TransitionColumn, shot.transition.isEmpty() ? QString() : QStringLiteral("%1 (%2)").arg(shot.transition).arg(shot.transitionLength));
        for (int p = 0; p < shot.panels.size(); ++p) {
            Storyboard::Panel const& panel = shot.panels[p];
            auto* const child = new QTreeWidgetItem(top);
            child->setFlags(child->flags() | Qt::ItemIsEditable);
            child->setData(0, Qt::UserRole, sh);
            child->setData(0, Qt::UserRole + 1, p);
            child->setText(NameColumn, tr("Panel %1").arg(board.panelName(sh, p, names)));
            child->setText(StartColumn, Storyboard::duration(board.panelStart(sh, p), scene.fps, tc));
            child->setText(DurationColumn, Storyboard::duration(panel.length, scene.fps, tc));
            QStringList captions;
            for (Storyboard::Caption const& c : panel.captions)
                captions << QStringLiteral("%1: %2").arg(c.field, c.text.section(u'\n', 0, 0));
            child->setText(CaptionColumn, captions.join(QStringLiteral("  |  ")));
            if (here.first == sh && here.second == p)
                for (int c = 0; c < ColumnCount; ++c)
                    child->setBackground(c, current);
        }
        top->setExpanded(true);
    }
    QString const field = m_field->currentText();
    m_field->clear();
    m_field->addItems(board.captionFields);
    if (int const i = m_field->findText(field); i >= 0)
        m_field->setCurrentIndex(i);
    m_rebuilding = false;
    if (was.first >= 0)
        pick(std::min(was.first, int(board.shots.size()) - 1), was.second);
    showCaption();
    m_thumbnailTimer->start(std::max(0, s.value(QStringLiteral("prefs/stThumbnailDelay"), 2000).toInt()));
}

void StoryboardView::showCaption()
{
    QPair<int, int> const at = picked();
    bool const was = m_rebuilding;
    m_rebuilding = true;
    m_caption->setEnabled(at.second >= 0);
    QString const text = m_document->scene().storyboard.caption(at.first, at.second, m_field->currentText());
    if (m_caption->toPlainText() != text)
        m_caption->setPlainText(text);
    m_rebuilding = was;
    m_thumbnailTimer->start(0);
}

void StoryboardView::refreshThumbnail()
{
    Scene const& scene = m_document->scene();
    QPair<int, int> at = picked();
    if (at.first < 0)
        at = scene.storyboard.panelAt(m_frame);
    if (at.first < 0 || !isVisible()) {
        m_thumbnail->setPixmap(QPixmap());
        m_thumbnail->setText(at.first < 0 ? tr("No panel") : QString());
        return;
    }
    int const frame = std::clamp(scene.storyboard.panelStart(at.first, std::max(0, at.second)), 0, scene.frameCount() - 1);
    QSize const size = QSize(scene.width, scene.height).scaled(m_thumbnail->size(), Qt::KeepAspectRatio);
    m_thumbnail->setPixmap(QPixmap::fromImage(FrameRender::frame(scene, frame, size, true, scene.background)));
}

void StoryboardView::setCurrentCell(int, int frame)
{
    if (frame == m_frame)
        return;
    QPair<int, int> const before = m_document->scene().storyboard.panelAt(m_frame);
    m_frame = frame;
    if (m_document->scene().storyboard.panelAt(m_frame) != before)
        rebuild();
}

void StoryboardView::newShot()
{
    QPair<int, int> const at = picked();
    int const rate = std::max(1, int(std::lround(m_document->scene().fps)));
    int place = -1;
    edit(tr("New Shot"), [&](Storyboard& b) {
        place = b.addShot(at.first >= 0 ? at.first + 1 : -1, rate);
        b.shots[place].name = Storyboard::shotName(b.shots[place].name, naming());
    });
    pick(place, -1);
}

bool StoryboardView::newPanel()
{
    Storyboard board = m_document->scene().storyboard;
    if (!board.splitPanel(m_frame))
        return false;
    edit(tr("New Panel"), [&](Storyboard& b) { b = board; });
    return true;
}

bool StoryboardView::splitShot()
{
    Storyboard board = m_document->scene().storyboard;
    if (!board.splitShot(m_frame))
        return false;
    edit(tr("Split Shot"), [&](Storyboard& b) { b = board; });
    return true;
}

bool StoryboardView::joinShot()
{
    int const shot = picked().first;
    Storyboard board = m_document->scene().storyboard;
    if (!board.joinShot(shot))
        return false;
    edit(tr("Join Shots"), [&](Storyboard& b) { b = board; });
    return true;
}

void StoryboardView::deletePicked()
{
    QPair<int, int> const at = picked();
    if (at.first < 0)
        return;
    if (at.second < 0)
        edit(tr("Delete Shot"), [&](Storyboard& b) { b.removeShot(at.first); });
    else
        edit(tr("Delete Panel"), [&](Storyboard& b) { b.removePanel(at.first, at.second); });
}

void StoryboardView::renumberShots()
{
    edit(tr("Renumber Shots"), [](Storyboard& b) { b.renumberShots(naming()); });
}

void StoryboardView::fitToScene()
{
    int const frames = m_document->scene().frameCount();
    edit(tr("Fit Storyboard to Scene"), [&](Storyboard& b) { b.fitTo(frames - b.start); });
}

void StoryboardView::setCaption(QString const& field, QString const& text)
{
    QPair<int, int> const at = picked();
    if (at.first < 0 || at.second < 0 || field.isEmpty() || m_document->scene().storyboard.caption(at.first, at.second, field) == text)
        return;
    edit(tr("Caption"), [&](Storyboard& b) { b.setCaption(at.first, at.second, field, text); });
}

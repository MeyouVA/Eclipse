// Eclipse - the Guides view: lines across the picture to snap to, and rulers, grids and perspectives to draw along.
#include "ui/views/GuidesView.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"
#include "model/scene/Scene.h"
#include "canvas/Canvas.h"

GuidesView::GuidesView(Document* document, Canvas* canvas, QWidget* parent) : QWidget(parent), m_document(document), m_canvas(canvas)
{
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("GuidesList"));
    m_position = new QDoubleSpinBox(this);
    m_position->setObjectName(QStringLiteral("GuidePosition"));
    m_position->setRange(-100000, 100000);
    m_position->setDecimals(1);
    m_position->setKeyboardTracking(false);
    m_position->setToolTip(tr("Where the guide chosen lies: from the left edge of the frame for a guide that runs down, from the top for one that runs across."));
    m_show = new QCheckBox(tr("Show guides"), this);
    m_show->setObjectName(QStringLiteral("GuidesShow"));
    m_show->setChecked(m_canvas->showsGuides());
    m_show->setToolTip(tr("Guides are seen in the Camera view; the shape tools and the points of the Contour Editor go to them when they come near."));
    QPushButton* const across = new QPushButton(tr("Add Across"), this);
    across->setObjectName(QStringLiteral("GuideAddHorizontal"));
    QPushButton* const down = new QPushButton(tr("Add Down"), this);
    down->setObjectName(QStringLiteral("GuideAddVertical"));
    // The guides that are drawn along: a ruler, a grid, a perspective.
    QPushButton* const more = new QPushButton(tr("Add Guide"), this);
    more->setObjectName(QStringLiteral("GuideAddMenu"));
    more->setToolTip(tr("A guide to draw along: with it chosen in the list, a line drawn with the brush or the pencil is kept straight\n"
                        "and runs one of the ways the guide has. Its points can be dragged in the Camera view with a drawing tool."));
    {
        QMenu* const kinds = new QMenu(more);
        // (In the order they are thought of, not the order they were added to the program.)
        for (int kind : {int(Scene::Guide::Ruler), int(Scene::Guide::SquareGrid), int(Scene::Guide::Isometric), int(Scene::Guide::OnePoint), int(Scene::Guide::TwoPoint),
                         int(Scene::Guide::ThreePoint), int(Scene::Guide::FourPoint), int(Scene::Guide::FivePoint), int(Scene::Guide::Symmetry)}) {
            QAction* const a = kinds->addAction(Scene::guideName(Scene::Guide::Kind(kind)));
            a->setObjectName(QStringLiteral("GuideKind%1").arg(kind));
            connect(a, &QAction::triggered, this, [this, kind] { addDrawingGuide(kind); });
        }
        kinds->addSeparator();
        int way = 0;
        for (QString const& text : {tr("Horizontal Symmetry"), tr("Vertical Symmetry"), tr("Dual Axis Symmetry")}) {
            QAction* const a = kinds->addAction(text);
            a->setObjectName(QStringLiteral("GuideSymmetry%1").arg(way));
            connect(a, &QAction::triggered, this, [this, way] { addSymmetry(way); });
            ++way;
        }
        more->setMenu(kinds);
    }
    QPushButton* const remove = new QPushButton(tr("Remove"), this);
    remove->setObjectName(QStringLiteral("GuideRemove"));
    QHBoxLayout* const buttons = new QHBoxLayout;
    buttons->addWidget(more);
    buttons->addWidget(across);
    buttons->addWidget(down);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    QHBoxLayout* const place = new QHBoxLayout;
    place->addWidget(new QLabel(tr("Position"), this));
    place->addWidget(m_position, 1);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_show);
    layout->addWidget(m_list, 1);
    layout->addLayout(place);
    layout->addLayout(buttons);
    connect(m_show, &QCheckBox::toggled, m_canvas, &Canvas::setShowGuides);
    connect(m_canvas, &Canvas::showGuidesChanged, m_show, &QCheckBox::setChecked);
    // A new guide lies through the middle of the frame, to be put where it is wanted.
    connect(across, &QPushButton::clicked, this, [this] { addGuide(false, m_document->scene().height / 2.0); });
    connect(down, &QPushButton::clicked, this, [this] { addGuide(true, m_document->scene().width / 2.0); });
    connect(remove, &QPushButton::clicked, this, [this] {
        QVector<Scene::Guide> guides = m_document->scene().guides;
        int const row = m_list->currentRow();
        if (row < 0 || row >= guides.size())
            return;
        guides.removeAt(row);
        m_document->setGuides(guides, tr("Remove Guide"));
    });
    connect(m_position, &QDoubleSpinBox::valueChanged, this, [this](double at) {
        QVector<Scene::Guide> guides = m_document->scene().guides;
        int const row = m_list->currentRow();
        if (m_rebuilding || row < 0 || row >= guides.size() || guides[row].at == at)
            return;
        guides[row].at = at;
        m_document->setGuides(guides, tr("Move Guide"));
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        QVector<Scene::Guide> const& guides = m_document->scene().guides;
        QSignalBlocker const block(m_position);
        bool const line = row >= 0 && row < guides.size() && guides[row].kind == Scene::Guide::Line;
        m_position->setEnabled(line);
        if (line)
            m_position->setValue(guides[row].at);
        m_canvas->setActiveGuide(row);     // the one chosen is the one in use
    });
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QWidget::customContextMenuRequested, this, &GuidesView::showMenu);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        int const row = m_list->row(item);
        bool ok = false;
        QString const name = QInputDialog::getText(this, tr("Rename Guide"), tr("Rename Guide:"), QLineEdit::Normal, item->text(), &ok);
        if (ok)
            renameGuide(row, name);
    });
    connect(m_document, &Document::changed, this, &GuidesView::rebuild);
    rebuild();
}

int GuidesView::addGuide(bool vertical, double at)
{
    QVector<Scene::Guide> guides = m_document->scene().guides;
    guides.push_back({vertical, at});
    m_document->setGuides(guides, tr("Add Guide"));
    m_list->setCurrentRow(int(guides.size()) - 1);
    return int(guides.size()) - 1;
}

int GuidesView::addDrawingGuide(int kind)
{
    if (kind <= Scene::Guide::Line || kind > Scene::Guide::kLastKind)
        return -1;
    QVector<Scene::Guide> guides = m_document->scene().guides;
    guides.push_back(m_document->scene().makeGuide(Scene::Guide::Kind(kind)));
    m_document->setGuides(guides, tr("Add Guide"));
    m_list->setCurrentRow(int(guides.size()) - 1);
    return int(guides.size()) - 1;
}

void GuidesView::rebuild()
{
    QVector<Scene::Guide> const& guides = m_document->scene().guides;
    int const row = m_list->currentRow();
    m_rebuilding = true;
    {
        QSignalBlocker const block(m_list);
        m_list->clear();
        for (Scene::Guide const& guide : guides)
            m_list->addItem(!guide.name.isEmpty() ? guide.name : guide.kind != Scene::Guide::Line ? Scene::guideName(guide.kind)
                            : guide.vertical ? tr("Down, %1 from the left").arg(guide.at, 0, 'f', 1) : tr("Across, %1 from the top").arg(guide.at, 0, 'f', 1));
        m_list->setCurrentRow(guides.isEmpty() ? -1 : std::clamp(row, 0, int(guides.size()) - 1));
    }
    int const now = m_list->currentRow();
    QSignalBlocker const block(m_position);
    bool const line = now >= 0 && guides[now].kind == Scene::Guide::Line;
    m_position->setEnabled(line);
    if (line)
        m_position->setValue(guides[now].at);
    m_canvas->setActiveGuide(now);
    m_rebuilding = false;
}

namespace {
// What Cut and Copy keep: guides of any kind, for this scene or another.
QVector<Scene::Guide>& guideClipboard()
{
    static QVector<Scene::Guide> kept;
    return kept;
}
}  // namespace

void GuidesView::addSymmetry(int way)
{
    // An axis level across the middle of the picture, upright down it, or both.
    Scene const& scene = m_document->scene();
    double const w = scene.width, h = scene.height;
    QVector<Scene::Guide> guides = scene.guides;
    Scene::Guide const axis = scene.makeGuide(Scene::Guide::Symmetry);
    if (way == 0 || way == 2) {
        Scene::Guide across = axis;
        across.points = {QPointF(w * 0.2, h * 0.5), QPointF(w * 0.8, h * 0.5)};
        guides.push_back(across);
    }
    if (way == 1 || way == 2)
        guides.push_back(axis);
    m_document->setGuides(guides, tr("Add Guide"));
    m_list->setCurrentRow(int(guides.size()) - 1);
}

QVector<int> GuidesView::selectedRows() const
{
    QVector<int> rows;
    for (QListWidgetItem* const item : m_list->selectedItems())
        rows.push_back(m_list->row(item));
    if (rows.isEmpty() && m_list->currentRow() >= 0)
        rows.push_back(m_list->currentRow());
    std::sort(rows.begin(), rows.end());
    return rows;
}

bool GuidesView::renameGuide(int row, QString const& name)
{
    QVector<Scene::Guide> guides = m_document->scene().guides;
    if (row < 0 || row >= guides.size() || guides[row].name == name.trimmed())
        return false;
    guides[row].name = name.trimmed();
    m_document->setGuides(guides, tr("Rename Guide"));
    return true;
}

int GuidesView::copyGuides(bool cut)
{
    QVector<int> const rows = selectedRows();
    QVector<Scene::Guide> guides = m_document->scene().guides;
    if (rows.isEmpty())
        return 0;
    guideClipboard().clear();
    for (int row : rows)
        guideClipboard().push_back(guides[row]);
    if (cut) {
        for (int i = int(rows.size()) - 1; i >= 0; --i)
            guides.removeAt(rows[i]);
        m_document->setGuides(guides, rows.size() > 1 ? tr("Cut Guides") : tr("Cut Guide"));
    }
    return int(rows.size());
}

int GuidesView::pasteGuides()
{
    if (guideClipboard().isEmpty())
        return 0;
    QVector<Scene::Guide> guides = m_document->scene().guides;
    guides += guideClipboard();
    m_document->setGuides(guides, guideClipboard().size() > 1 ? tr("Paste Guides") : tr("Paste Guide"));
    m_list->setCurrentRow(int(guides.size()) - 1);
    return int(guideClipboard().size());
}

int GuidesView::exportGuides(QString const& path, bool selectedOnly)
{
    QVector<Scene::Guide> const& guides = m_document->scene().guides;
    QJsonArray list;
    int written = 0;
    QVector<int> const rows = selectedRows();
    for (int row = 0; row < guides.size(); ++row)
        if (!selectedOnly || rows.contains(row)) {
            list.append(Scene::guideToJson(guides[row]));
            ++written;
        }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return -1;
    file.write(QJsonDocument(QJsonObject{{"eclipseGuides", 1}, {"guides", list}}).toJson());
    return written;
}

int GuidesView::importGuides(QString const& path, QString* message)
{
    // Guides already in the scene are not taken in twice.
    QFile file(path);
    QJsonDocument const read = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()) : QJsonDocument();
    if (!read.isObject() || !read.object().contains(QStringLiteral("guides"))) {
        if (message)
            *message = tr("Can not import guides.");
        return -1;
    }
    QVector<Scene::Guide> guides = m_document->scene().guides;
    int found = 0, taken = 0;
    for (QJsonValue const& value : read.object()[QStringLiteral("guides")].toArray()) {
        Scene::Guide guide;
        if (!Scene::guideFromJson(value, &guide))
            continue;
        ++found;
        if (guides.contains(guide))
            continue;
        guides.push_back(guide);
        ++taken;
    }
    if (taken == 0) {
        if (message)
            *message = found > 0 ? tr("All the guides were duplicates.\nTherefore, they were not imported.") : tr("No guide imported.");
        return 0;
    }
    m_document->setGuides(guides, tr("Import Guides"));
    return taken;
}

void GuidesView::showMenu(QPoint const& at)
{
    QMenu menu(this);
    bool const any = !selectedRows().isEmpty();
    QAction* const rename = menu.addAction(tr("Rename Guide..."));
    menu.addSeparator();
    QAction* const cut = menu.addAction(tr("Cut Guides"));
    QAction* const copy = menu.addAction(tr("Copy Guides"));
    QAction* const paste = menu.addAction(tr("Paste Guides"));
    QAction* const remove = menu.addAction(tr("Delete Guides"));
    menu.addSeparator();
    QAction* const import = menu.addAction(tr("Import Guides..."));
    QAction* const exportAll = menu.addAction(tr("Export Guides..."));
    QAction* const exportSome = menu.addAction(tr("Export Selected Guides..."));
    for (QAction* const a : {rename, cut, copy, remove, exportSome})
        a->setEnabled(any);
    paste->setEnabled(!guideClipboard().isEmpty());
    exportAll->setEnabled(!m_document->scene().guides.isEmpty());
    QAction* const chosen = menu.exec(m_list->viewport()->mapToGlobal(at));
    if (!chosen)
        return;
    if (chosen == rename) {
        int const row = selectedRows().first();
        bool ok = false;
        QString const name = QInputDialog::getText(this, tr("Rename Guide"), tr("Rename Guide:"), QLineEdit::Normal, m_list->item(row)->text(), &ok);
        if (ok)
            renameGuide(row, name);
    } else if (chosen == cut || chosen == copy) {
        copyGuides(chosen == cut);
    } else if (chosen == paste) {
        pasteGuides();
    } else if (chosen == remove) {
        QVector<int> const rows = selectedRows();
        QVector<Scene::Guide> guides = m_document->scene().guides;
        for (int i = int(rows.size()) - 1; i >= 0; --i)
            guides.removeAt(rows[i]);
        m_document->setGuides(guides, rows.size() > 1 ? tr("Delete Guides") : tr("Delete Guide"));
    } else if (chosen == import) {
        QString const path = QFileDialog::getOpenFileName(this, tr("Import Guides"), QString(), tr("Eclipse guides (*.eclguides)"));
        QString message;
        if (!path.isEmpty() && importGuides(path, &message) <= 0)
            QMessageBox::information(this, tr("Import Guides"), message);
    } else {
        QString path = QFileDialog::getSaveFileName(this, tr("Export Guides"), QStringLiteral("guides.eclguides"), tr("Eclipse guides (*.eclguides)"));
        if (path.isEmpty())
            return;
        if (!path.endsWith(QStringLiteral(".eclguides")))
            path += QStringLiteral(".eclguides");
        if (exportGuides(path, chosen == exportSome) < 0)
            QMessageBox::warning(this, tr("Export Guides"), tr("Can not export guides."));
    }
}

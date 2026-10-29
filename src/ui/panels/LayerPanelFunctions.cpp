// Eclipse - the Layer Properties view: the function buttons beside the values - shown, sent to the Function Editor, linked.
#include "ui/panels/LayerPanel.h"

#include <QApplication>
#include <QMenu>
#include <QToolButton>

#include "actions/manager/ActionManager.h"
#include "actions/theme/Theme.h"
#include "model/document/Commands.h"

QToolButton* LayerPanel::functionButton(int value)
{
    QToolButton* const button = new QToolButton(this);
    static char const* const kNames[] = {"X", "Y", "ScaleX", "ScaleY", "Angle", "Opacity", "Z", "Skew", "Fov"};
    button->setObjectName(QStringLiteral("LayerFunction") + QLatin1String(kNames[value]));
    button->setIcon(Theme::icon(ActionManager::defaultDirectory(), QStringLiteral("function/curved")));
    button->setIconSize(QSize(14, 14));
    button->setAutoRaise(true);
    button->setCheckable(false);
    button->setPopupMode(QToolButton::MenuButtonPopup);
    button->setToolTip(tr("Click to show function. Alt + click will send to the function editor."));
    QMenu* const menu = new QMenu(button);
    connect(menu, &QMenu::aboutToShow, this, [this, menu, value] { fillFunctionMenu(menu, value); });
    button->setMenu(menu);
    connect(button, &QToolButton::clicked, this, [this, value] {
        Scene const& scene = m_document->scene();
        if (m_layer < 0 || m_layer >= scene.layers.size())
            return;
        if (m_publishMode) {
            togglePublished(m_layer, value);
            return;
        }
        if (QApplication::keyboardModifiers() & Qt::AltModifier) {
            int const column = scene.layers[m_layer].linkOf(value);
            if (column == 0)
                emit message(tr("There is no bezier, ease or local value connecting to the layer."));
            else
                emit functionEditorWanted(column);
            return;
        }
        emit functionShown(m_layer, value);
    });
    m_functionButtons[value] = button;
    return button;
}

void LayerPanel::fillFunctionMenu(QMenu* menu, int value)
{
    menu->clear();
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    int const layer = m_layer;
    int const linked = scene.layers[layer].linkOf(value);
    // The columns it can be linked to (a 3D path for the position only).
    bool const position = value == Driver::X || value == Driver::Y || value == Driver::Z;
    QAction* const local = menu->addAction(tr("Local Value (Not Linked)"));
    local->setObjectName(QStringLiteral("LinkLocal"));
    local->setCheckable(true);
    local->setChecked(linked == 0);
    connect(local, &QAction::triggered, this, [this, layer, value] { m_document->linkValue(layer, value, 0, tr("Unlink Function Column")); });
    if (!scene.functions.isEmpty())
        menu->addSeparator();
    for (FunctionColumn const& column : scene.functions) {
        if (column.kind == FunctionColumn::Path3D && !position)
            continue;
        QAction* const a = menu->addAction(column.name + QStringLiteral(" (") + FunctionColumn::kindName(column.kind) + QLatin1Char(')'));
        a->setObjectName(QStringLiteral("LinkTo_") + column.name);
        a->setCheckable(true);
        a->setChecked(column.id == linked);
        int const id = column.id;
        connect(a, &QAction::triggered, this, [this, layer, value, id] { m_document->linkValue(layer, value, id, tr("Link Function Column")); });
    }
    menu->addSeparator();
    // A new column, holding the animation the value has now: of the kind Preferences say (Default Bezier), or any kind.
    int const usual = FunctionColumn::defaultBezier ? FunctionColumn::Bezier : FunctionColumn::Ease;
    QAction* const fresh = menu->addAction(tr("New Function Column"));
    fresh->setObjectName(QStringLiteral("LinkNew"));
    connect(fresh, &QAction::triggered, this, [this, layer, value, usual] { m_document->storeAsFunction(layer, value, usual); });
    for (int kind = 0; kind < FunctionColumn::KindCount; ++kind) {
        if (kind == FunctionColumn::Path3D && !position)
            continue;
        QAction* const a = menu->addAction(tr("New %1 Column").arg(FunctionColumn::kindName(kind)));
        a->setObjectName(QStringLiteral("LinkNew_") + QString::number(kind));
        connect(a, &QAction::triggered, this, [this, layer, value, kind] { m_document->storeAsFunction(layer, value, kind); });
    }
    addAttributeCommands(menu, value);
}

void LayerPanel::refreshFunctionButtons(Layer const& layer)
{
    Scene const& scene = m_document->scene();
    for (int v = 0; v < Driver::ValueCount; ++v) {
        QToolButton* const button = m_functionButtons[v];
        if (!button)
            continue;
        FunctionColumn const* column = scene.function(layer.linkOf(v));
        // Linked: the button says to which column, and stands out.
        button->setProperty("linked", column != nullptr);
        button->setText(column ? column->name : QString());
        button->setToolButtonStyle(column ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
        button->setToolTip(column ? tr("Linked to the function column %1 (%2).").arg(column->name, FunctionColumn::kindName(column->kind)) + QLatin1Char('\n')
                                        + tr("Click to show function. Alt + click will send to the function editor.")
                                  : tr("Click to show function. Alt + click will send to the function editor."));
    }
}

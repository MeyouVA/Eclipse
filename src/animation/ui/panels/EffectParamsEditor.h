// Eclipse - the named settings of an effect as rows to edit, for the Effects panel and the Node View's node settings.
#pragma once

#include "EclipseExport.h"

#include <QHash>
#include <QWidget>

#include "model/scene/Effect.h"

class QFormLayout;

class ECLIPSE_UI_API EffectParamsEditor : public QWidget {
    Q_OBJECT

public:
    explicit EffectParamsEditor(QWidget* parent = nullptr);
    // Rows for the settings of its kind (made again only when the kind changes), showing its values.
    void setEffect(Effect const& e);
    // The values shown, put into `e`.
    void takeInto(Effect& e) const;
    bool hasRows() const { return !m_fields.isEmpty(); }

signals:
    void edited(QString const& key, double value);

private:
    void changed(QString const& key, double value);

    Effect::Type m_type = Effect::TypeCount;
    QFormLayout* m_form = nullptr;
    QHash<QString, QWidget*> m_fields;
    QHash<QString, double> m_values;
    bool m_setting = false;
};

// Eclipse - the named settings of an effect as rows to edit.
#include "ui/panels/EffectParamsEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSpinBox>

#include <cmath>

#include "ui/panels/ColourEditor.h"

namespace {

QString said(char const* text)
{
    return text ? QCoreApplication::translate("Effect", text) : QString();
}

QColor colourOf(double value)
{
    return QColor::fromRgba(QRgb(quint32(std::max(0.0, value))));
}

void showColour(QPushButton* button, QColor const& colour)
{
    button->setText(colour.name());
    button->setStyleSheet(QStringLiteral("background-color: %1; color: %2;").arg(colour.name(), colour.lightnessF() > 0.5 ? QStringLiteral("black") : QStringLiteral("white")));
}

}  // namespace

EffectParamsEditor::EffectParamsEditor(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("EffectParams"));
    m_form = new QFormLayout(this);
    m_form->setContentsMargins(0, 0, 0, 0);
}

void EffectParamsEditor::changed(QString const& key, double value)
{
    m_values.insert(key, value);
    if (!m_setting)
        emit edited(key, value);
}

void EffectParamsEditor::setEffect(Effect const& e)
{
    m_setting = true;
    if (e.type != m_type) {
        m_type = e.type;
        while (m_form->rowCount() > 0)
            m_form->removeRow(0);
        m_fields.clear();
        m_values.clear();
        for (Effect::Param const& p : Effect::params(e.type)) {
            QString const key = QLatin1String(p.key);
            QWidget* field = nullptr;
            switch (p.kind) {
            case Effect::Param::Number: {
                QDoubleSpinBox* const box = new QDoubleSpinBox(this);
                box->setRange(p.low, p.high);
                double const span = p.high - p.low;
                box->setDecimals(span <= 10.0 ? 2 : 1);
                box->setSingleStep(span <= 1.0 ? 0.05 : span <= 10.0 ? 0.1 : span <= 400.0 ? 1.0 : 10.0);
                box->setKeyboardTracking(false);
                connect(box, &QDoubleSpinBox::valueChanged, this, [this, key](double v) { changed(key, v); });
                field = box;
                break;
            }
            case Effect::Param::Whole: {
                QSpinBox* const box = new QSpinBox(this);
                box->setRange(int(p.low), int(p.high));
                box->setKeyboardTracking(false);
                connect(box, &QSpinBox::valueChanged, this, [this, key](int v) { changed(key, v); });
                field = box;
                break;
            }
            case Effect::Param::Toggle: {
                QCheckBox* const box = new QCheckBox(said(p.label), this);
                connect(box, &QCheckBox::toggled, this, [this, key](bool on) { changed(key, on ? 1.0 : 0.0); });
                field = box;
                break;
            }
            case Effect::Param::Choice: {
                QComboBox* const box = new QComboBox(this);
                box->addItems(said(p.choices).split(QLatin1Char('|')));
                connect(box, &QComboBox::currentIndexChanged, this, [this, key](int index) { changed(key, index); });
                field = box;
                break;
            }
            case Effect::Param::Colour: {
                QPushButton* const button = new QPushButton(this);
                QString const title = said(p.label);
                connect(button, &QPushButton::clicked, this, [this, key, button, title] {
                    QColor const picked = ColourEditor::getColour(colourOf(m_values.value(key)), this, title);
                    if (!picked.isValid())
                        return;
                    showColour(button, picked);
                    changed(key, double(picked.rgba()));
                });
                field = button;
                break;
            }
            }
            field->setObjectName(QStringLiteral("EffectParam_%1").arg(key));
            if (p.tip)
                field->setToolTip(said(p.tip));
            if (p.kind == Effect::Param::Toggle)
                m_form->addRow(field);
            else
                m_form->addRow(said(p.label), field);
            m_fields.insert(key, field);
        }
    }
    for (Effect::Param const& p : Effect::params(e.type)) {
        QString const key = QLatin1String(p.key);
        double const v = e.value(p.key);
        m_values.insert(key, v);
        QWidget* const field = m_fields.value(key);
        if (QDoubleSpinBox* const box = qobject_cast<QDoubleSpinBox*>(field))
            box->setValue(v);
        else if (QSpinBox* const whole = qobject_cast<QSpinBox*>(field))
            whole->setValue(int(std::lround(v)));
        else if (QCheckBox* const check = qobject_cast<QCheckBox*>(field))
            check->setChecked(v != 0.0);
        else if (QComboBox* const choice = qobject_cast<QComboBox*>(field))
            choice->setCurrentIndex(int(std::lround(v)));
        else if (QPushButton* const button = qobject_cast<QPushButton*>(field))
            showColour(button, colourOf(v));
    }
    m_setting = false;
}

void EffectParamsEditor::takeInto(Effect& e) const
{
    for (auto v = m_values.cbegin(); v != m_values.cend(); ++v)
        e.setValue(v.key(), v.value());
}

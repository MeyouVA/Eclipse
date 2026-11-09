#include "board/model/BoardDocument.h"

namespace {
bool validField(QString const& field)
{
    return !field.isEmpty() && field.size() <= 128;
}

bool matches(QString const& left, QString const& right)
{
    return left.compare(right, Qt::CaseInsensitive) == 0;
}
}

bool BoardDocument::setCaption(int shot, int panel, QString const& field, QString const& text, quint64 session)
{
    QString const name = field.trimmed();
    if (!hasPanel(shot, panel) || !validField(name))
        return false;
    auto next = m_project;
    auto& captions = next.shots[shot].panels[panel].captions;
    for (qsizetype i = 0; i < captions.size(); ++i) {
        if (!matches(captions[i].field, name))
            continue;
        if (captions[i].text == text)
            return false;
        if (text.isEmpty())
            captions.removeAt(i);
        else
            captions[i].text = text;
        apply(std::move(next), tr("Edit Caption"), session);
        return true;
    }
    if (text.isEmpty())
        return false;
    if (!next.captionFields.contains(name, Qt::CaseInsensitive)) {
        if (next.captionFields.size() >= 128)
            return false;
        next.captionFields.push_back(name);
    }
    captions.push_back({name, text});
    apply(std::move(next), tr("Edit Caption"), session);
    return true;
}

bool BoardDocument::addCaptionField(QString const& field)
{
    QString const name = field.trimmed();
    if (m_path.isEmpty() || !validField(name) || m_project.captionFields.size() >= 128
        || m_project.captionFields.contains(name, Qt::CaseInsensitive))
        return false;
    auto next = m_project;
    next.captionFields.append(name);
    apply(std::move(next), tr("Add Caption Field"));
    return true;
}

bool BoardDocument::removeCaptionField(QString const& field)
{
    if (!m_project.captionFields.contains(field, Qt::CaseInsensitive)
        || Storyboard::defaultCaptionFields().contains(field, Qt::CaseInsensitive))
        return false;
    auto next = m_project;
    next.captionFields.removeIf([&](QString const& name) { return matches(name, field); });
    for (auto& shot : next.shots)
        for (auto& panel : shot.panels)
            panel.captions.removeIf([&](Storyboard::Caption const& caption) { return matches(caption.field, field); });
    apply(std::move(next), tr("Remove Caption Field"));
    return true;
}

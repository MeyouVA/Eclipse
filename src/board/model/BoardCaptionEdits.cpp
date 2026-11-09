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

bool BoardDocument::writePanelCaption(BoardPanel& panel, QString const& field, QString const& text)
{
    auto& captions = panel.captions;
    for (qsizetype i = 0; i < captions.size(); ++i) {
        if (!matches(captions[i].field, field))
            continue;
        if (captions[i].text == text)
            return false;
        if (text.isEmpty())
            captions.removeAt(i);
        else
            captions[i].text = text;
        return true;
    }
    if (text.isEmpty())
        return false;
    captions.push_back({field, text});
    return true;
}

bool BoardDocument::setCaption(int shot, int panel, QString const& field, QString const& text, quint64 session)
{
    QString const name = field.trimmed();
    if (!hasPanel(shot, panel) || !validField(name))
        return false;
    bool const addField = !text.isEmpty() && !m_project.captionFields.contains(name, Qt::CaseInsensitive);
    if (addField && m_project.captionFields.size() >= 128)
        return false;
    auto next = m_project;
    if (!writePanelCaption(next.shots[shot].panels[panel], name, text))
        return false;
    if (addField)
        next.captionFields.push_back(name);
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

int BoardDocument::replaceCaptionText(QString const& find, QString const& replacement, Qt::CaseSensitivity sensitivity, QString* error)
{
    if (error)
        error->clear();
    if (find.isEmpty())
        return 0;
    BoardProject next = m_project;
    int changed = 0;
    qsizetype remainingGrowth = captionCsvLimit;
    qsizetype const extra = replacement.size() - find.size();
    for (auto& shot : next.shots)
        for (auto& panel : shot.panels) {
            for (qsizetype i = panel.captions.size(); i-- > 0;) {
                auto& caption = panel.captions[i];
                qsizetype const first = caption.text.indexOf(find, 0, sensitivity);
                if (first < 0)
                    continue;
                if (extra > 0) {
                    for (qsizetype at = first; at >= 0; at = caption.text.indexOf(find, at + find.size(), sensitivity)) {
                        if (extra > remainingGrowth) {
                            if (error)
                                *error = tr("This replacement would add too much text in one operation.");
                            return -1;
                        }
                        remainingGrowth -= extra;
                    }
                }
                QString text = caption.text;
                text.replace(find, replacement, sensitivity);
                if (text != caption.text) {
                    if (text.isEmpty())
                        panel.captions.removeAt(i);
                    else
                        caption.text = std::move(text);
                    ++changed;
                }
            }
        }
    if (changed > 0) {
        if (next.toBytes(error).isEmpty())
            return -1;
        apply(std::move(next), tr("Replace Text in Captions"));
    }
    return changed;
}

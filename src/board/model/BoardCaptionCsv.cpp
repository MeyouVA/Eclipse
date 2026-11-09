#include "board/model/BoardDocument.h"

#include <QStringDecoder>
#include <algorithm>
#include <map>
#include <tuple>
#include <utility>

namespace {
enum class ReadRow { Ok, End, Invalid };
enum class Cell { Start, Plain, Quoted, Closed };
constexpr qsizetype metadataColumns = 6;
constexpr qsizetype maxColumns = metadataColumns + 128;

ReadRow readRow(QStringView input, qsizetype& position, QStringList& row)
{
    row.clear();
    if (position == input.size())
        return ReadRow::End;
    QString value;
    Cell state = Cell::Start;
    while (position < input.size()) {
        QChar const ch = input[position++];
        if (state == Cell::Quoted) {
            if (ch == u'"')
                state = Cell::Closed;
            else
                value += ch;
        } else if (state == Cell::Closed && ch == u'"') {
            value += ch;
            state = Cell::Quoted;
        } else if (ch == u',' || ch == u'\r' || ch == u'\n') {
            if (row.size() == maxColumns)
                return ReadRow::Invalid;
            row.append(std::exchange(value, QString()));
            if (ch != u',') {
                if (ch == u'\r' && position < input.size() && input[position] == u'\n')
                    ++position;
                return ReadRow::Ok;
            }
            state = Cell::Start;
        } else if (state == Cell::Start && ch == u'"') {
            state = Cell::Quoted;
        } else {
            if (state == Cell::Closed || ch == u'"')
                return ReadRow::Invalid;
            value += ch;
            state = Cell::Plain;
        }
    }
    if (state == Cell::Quoted || row.size() == maxColumns)
        return ReadRow::Invalid;
    row.append(std::move(value));
    return ReadRow::Ok;
}

using PanelKey = std::tuple<QString, QString, QString, QString>;

PanelKey panelKey(BoardProject const& project, BoardSpan const& span)
{
    auto const& shot = project.shots[span.shot];
    QString const name = shot.panels[span.panel].name.trimmed();
    return {shot.act, shot.sequence, shot.name, name.isEmpty() ? QString::number(span.panel + 1) : name};
}
}

int BoardDocument::updateCaptionsFromCsv(QByteArray const& bytes, QString* error)
{
    if (error)
        error->clear();
    auto fail = [error](QString const& message) {
        if (error)
            *error = message;
        return -1;
    };
    if (m_path.isEmpty() || bytes.size() > captionCsvLimit)
        return fail(tr("Open a project and choose a CSV file of up to 32 MiB."));
    QStringDecoder decode(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    QString text = decode(bytes);
    if (decode.hasError() || text.contains(QChar::Null))
        return fail(tr("Choose a valid UTF-8 CSV file."));
    if (text.startsWith(QChar::ByteOrderMark))
        text.remove(0, 1);

    qsizetype position = 0;
    QStringList row;
    QStringList const headings{QStringLiteral("Act"), QStringLiteral("Sequence"), QStringLiteral("Shot"),
                               QStringLiteral("Panel"), QStringLiteral("Start"), QStringLiteral("Duration")};
    if (readRow(text, position, row) != ReadRow::Ok || row.size() <= metadataColumns)
        return fail(tr("Use a CSV exported from this project with at least one caption column."));
    for (qsizetype column = 0; column < metadataColumns; ++column)
        if (row[column].trimmed().compare(headings[column], Qt::CaseInsensitive) != 0)
            return fail(tr("Keep the Act, Sequence, Shot, Panel, Start and Duration columns in their exported order."));
    QStringList fields;
    for (qsizetype column = metadataColumns; column < row.size(); ++column) {
        QString const heading = row[column].trimmed();
        auto const found = std::find_if(m_project.captionFields.cbegin(), m_project.captionFields.cend(),
            [&](QString const& field) { return field.compare(heading, Qt::CaseInsensitive) == 0; });
        if (found == m_project.captionFields.cend() || fields.contains(*found, Qt::CaseInsensitive))
            return fail(tr("Unknown or repeated caption column “%1”.").arg(heading));
        fields.append(*found);
    }

    auto const spans = m_project.spans();
    std::map<PanelKey, int> counts;
    for (auto const& span : spans)
        ++counts[panelKey(m_project, span)];
    QSet<int> seenStarts;
    BoardProject next = m_project;
    int changed = 0;
    for (int number = 2;; ++number) {
        auto const result = readRow(text, position, row);
        if (result == ReadRow::End)
            break;
        if (result == ReadRow::Ok && row.size() == 1 && row.first().isEmpty())
            continue;
        if (result == ReadRow::Invalid || row.size() != metadataColumns + fields.size())
            return fail(tr("CSV row %1 is malformed or has the wrong number of columns.").arg(number));
        bool validStart = false, validLength = false;
        int const start = row[4].toInt(&validStart);
        int const length = row[5].toInt(&validLength);
        auto const span = std::lower_bound(spans.cbegin(), spans.cend(), start,
            [](BoardSpan const& item, int frame) { return item.start < frame; });
        PanelKey const key{row[0], row[1], row[2], row[3]};
        if (!validStart || !validLength || span == spans.cend() || span->start != start || span->length != length
            || panelKey(m_project, *span) != key)
            return fail(tr("CSV row %1 does not match the current board. Export a fresh CSV and retry.").arg(number));
        if (seenStarts.contains(start) || counts.at(key) != 1)
            return fail(tr("CSV row %1 repeats a panel or uses a panel name that is ambiguous.").arg(number));
        seenStarts.insert(start);
        auto& panel = next.shots[span->shot].panels[span->panel];
        for (qsizetype column = 0; column < fields.size(); ++column)
            changed += int(writePanelCaption(panel, fields[column], row[column + metadataColumns]));
    }
    if (changed > 0) {
        if (next.toBytes(error).isEmpty())
            return -1;
        apply(std::move(next), tr("Update Captions from CSV"));
    }
    return changed;
}

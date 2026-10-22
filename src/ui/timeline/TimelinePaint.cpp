// Eclipse - the Timeline painted: what it is painted with, and its cells.
#include "ui/timeline/Timeline.h"
#include "ui/timeline/TimelinePaintSpace.h"
#include "actions/theme/InterfaceColours.h"

#include <QPainter>

#include <functional>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include <QScrollBar>
#include "ui/timeline/TimelinePaintHeadersShared.h"

using namespace timeline_paint_headers_shared;

void Timeline::paintEvent(QPaintEvent*)
{
    QPainter p(viewport());
    TimelinePaintSpace const space = paintSpace();
    p.fillRect(0, 0, space.w, space.h, space.bg);
    paintCells(p, space);
    paintRuler(p, space);
    paintHeaders(p, space);
}

TimelinePaintSpace Timeline::paintSpace() const
{
    Scene const& scene = m_document->scene();
    int const w = viewport()->width();
    int const h = viewport()->height();
    int const sx = horizontalScrollBar()->value();
    int const sy = verticalScrollBar()->value();
    int const cw = m_cellWidth;
    int const frames = m_dragEnd ? m_endFrames : scene.frameCount();
    int const sceneEndX = kHeaderWidth + frames * cw - sx;
    QColor const bg = InterfaceColours::get("timelineBackground"), rowA(83, 83, 83), rowB(78, 78, 78), line(52, 52, 52);
    QColor const text(210, 210, 210), dim(130, 130, 130), exposed = InterfaceColours::get("timelineExposed"), exposedCurrent = exposed.lighter(123);
    QColor const currentRow = InterfaceColours::get("timelineCurrentLayer"), playhead = InterfaceColours::get("timelineCurrentFrame"), headerBg(69, 69, 69);
    int const firstFrame = std::max(0, sx / cw);
    int const lastFrame = std::min(scene.frameCount() - 1, (sx + w - kHeaderWidth) / cw);
    return TimelinePaintSpace{scene, w, h, sx, sy, cw, frames, sceneEndX, firstFrame, lastFrame,
                              bg, rowA, rowB, line, text, dim, exposed, exposedCurrent, currentRow, playhead, headerBg};
}

// The cells: each layer's exposures and keys, the selection, where a dragged block would land, the current cell.
void Timeline::paintCells(QPainter& p, TimelinePaintSpace const& space)
{
    [[maybe_unused]] auto const& [scene, w, h, sx, sy, cw, frames, sceneEndX, firstFrame, lastFrame, bg, rowA, rowB, line, text, dim, exposed, exposedCurrent, currentRow, playhead, headerBg] = space;
    p.save();
    p.setClipRect(kHeaderWidth, kRulerHeight, w - kHeaderWidth, h - kRulerHeight);
    for (int row = 0; row < m_rows.size(); ++row) {
        int const layerIndex = m_rows[row];
        Layer const& layer = scene.layers[layerIndex];
        int const y = kRulerHeight + row * kRowHeight - sy;
        if (y + kRowHeight < kRulerHeight || y > h)
            continue;
        int const rowRight = std::min(w, kHeaderWidth + scene.frameCount() * cw - sx);
        bool const param = isParamRow(row);
        QColor rowColor = param ? QColor(70, 70, 70) : layerIndex == m_layer ? currentRow : (row % 2 ? rowA : rowB);
        if (layer.trackColor.isValid())
            rowColor = mixed(rowColor, layer.trackColor, 0.38);
        p.fillRect(kHeaderWidth, y, rowRight - kHeaderWidth, kRowHeight, rowColor);
        for (int f = firstFrame; f <= lastFrame; ++f) {
            int const x = kHeaderWidth + f * cw - sx;
            if (!param && layer.drawingAt(f) != Layer::kNoDrawing) {
                p.fillRect(x, y + 3, cw, kRowHeight - 6, layerIndex == m_layer ? exposedCurrent : exposed);
                if (layer.isKey(f)) {
                    p.fillRect(x, y + 3, 1, kRowHeight - 6, line);
                    p.setPen(Qt::NoPen);
                    // (In the colour of its mark, when it is marked as a key, a breakdown or an in-between.)
                    p.setBrush(Drawing::markColour(layer.drawings[layer.drawingAt(f)].mark));
                    p.setRenderHint(QPainter::Antialiasing, true);
                    double const r = std::min(3.0, cw / 2.0 - 0.5);
                    p.drawEllipse(QPointF(x + cw / 2.0, y + kRowHeight / 2.0), r, r);
                    p.setRenderHint(QPainter::Antialiasing, false);
                }
            }
            // A frame marked with a colour: a band of it along the top of the cell.
            if (!param && layer.frameMarks.contains(f))
                p.fillRect(x, y + 1, cw, 3, layer.frameMarks.value(f));
            // A line between frames where there is room for one; else every fifth.
            if (cw >= 7 || (f + 1) % 5 == 0) {
                p.setPen(line);
                p.drawLine(x + cw - 1, y, x + cw - 1, y + kRowHeight - 1);
            }
        }
        // A sound layer shows its waveform: how loud the sound is, a bar every two pixels.
        if (layer.kind == Layer::Sound && m_waveforms) {
            if (std::shared_ptr<SoundData> const sound = Sound::load(layer.soundFile)) {
                double const perPixel = double(SoundData::kRate) / scene.fps / cw;  // sample frames
                int const left = std::max(kHeaderWidth, kHeaderWidth + layer.soundStart * cw - sx);
                int const right = std::min(w, kHeaderWidth + scene.frameCount() * cw - sx);
                int const mid = y + kRowHeight / 2;
                p.setPen(QColor(120, 200, 150));
                for (int x = left; x < right; x += 2) {
                    qint64 const first = qint64((x - kHeaderWidth + sx - layer.soundStart * cw) * perPixel);
                    if (first >= sound->frames())
                        break;
                    int const half = int(sound->peak(first, qint64(perPixel * 2) + 1) * (kRowHeight / 2 - 2) + 0.5);
                    p.drawLine(x, mid - half, x, mid + half);
                }
            } else {
                p.setPen(QColor(220, 120, 100));
                p.drawText(QRect(kHeaderWidth + 6, y, w - kHeaderWidth - 6, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft,
                           tr("sound file missing"));
            }
        }
        // A layer that shows a symbol with sound in it: the waveform of what it plays.
        if (!param && layer.showsSymbol() && m_waveforms) {
            if (std::shared_ptr<SoundData> const sound = symbolSound(layerIndex)) {
                double const perPixel = double(SoundData::kRate) / scene.fps / cw;
                int const left = std::max(kHeaderWidth, kHeaderWidth - sx);
                int const right = std::min(w, kHeaderWidth + scene.frameCount() * cw - sx);
                int const mid = y + kRowHeight / 2;
                p.setPen(QColor(120, 200, 150, 170));
                for (int x = left; x < right; x += 2) {
                    qint64 const first = qint64((x - kHeaderWidth + sx) * perPixel);
                    if (first >= sound->frames())
                        break;
                    int const half = int(sound->peak(first, qint64(perPixel * 2) + 1) * (kRowHeight / 2 - 2) + 0.5);
                    if (half > 0)
                        p.drawLine(x, mid - half, x, mid + half);
                }
            }
        }
        // Lip-sync: the shape of the mouth on each frame, where the frames are wide enough to letter.
        if (layer.kind == Layer::Sound && !layer.lipsync.isEmpty() && cw >= 9) {
            QFont lettering = p.font();
            lettering.setPointSizeF(std::max(6.0, lettering.pointSizeF() - 1.5));
            lettering.setBold(true);
            p.save();
            p.setFont(lettering);
            p.setClipRect(kHeaderWidth, y, w - kHeaderWidth, kRowHeight);
            int const firstShown = std::max(0, sx / cw), lastShown = std::min(scene.frameCount() - 1, (sx + w - kHeaderWidth) / cw);
            for (int f = firstShown; f <= lastShown; ++f) {
                QChar const shape = layer.mouthAt(f);
                if (shape.isNull() || shape == QLatin1Char('X'))
                    continue;
                QRect const cell(kHeaderWidth + f * cw - sx, y, cw, kRowHeight);
                p.setPen(QColor(20, 20, 20));
                p.drawText(cell.translated(1, 1), Qt::AlignCenter, QString(shape));
                p.setPen(QColor(255, 226, 160));
                p.drawText(cell, Qt::AlignCenter, QString(shape));
            }
            p.restore();
        }
        // A morphing: an arrow from its first key drawing to its second, along the top of the row.
        if (layer.holdsDrawings() && !param) {
            QColor const morphColor(170, 215, 120);
            for (int m = 0; m < layer.morphs.size(); ++m) {
                int const to = layer.morphEnd(m);
                if (to < 0)
                    continue;
                int const x1 = kHeaderWidth + layer.morphs[m].from * cw - sx + cw / 2;
                int const x2 = kHeaderWidth + to * cw - sx + cw / 2;
                int const my = y + 5;
                p.setPen(QPen(morphColor, 2));
                p.drawLine(x1, my, x2 - 2, my);
                p.drawLine(x2 - 2, my, x2 - 6, my - 3);
                p.drawLine(x2 - 2, my, x2 - 6, my + 3);
                p.fillRect(x1 - 1, my - 2, 3, 5, morphColor);
            }
        }
        // Keyframes: a diamond on the frame, and a line from a motion keyframe to the next.
        QColor const keyColor = InterfaceColours::get("timelineKeyframe");
        int const ky = layer.holdsDrawings() && !param ? y + kRowHeight - 6 : y + kRowHeight / 2;
        bool const effectRow = param && m_params[row] >= ParameterCount && m_params[row] < kBoneRows;
        int const effect = effectRow ? m_params[row] - ParameterCount : -1;
        bool const keyedEffect = effectRow && effect < layer.effects.size() && layer.effects[effect].animated;
        int const part = !param ? Keyframe::AllParts : m_params[row] >= ParameterCount ? Keyframe::PartRest : 1 << m_params[row];
        QVector<int> mine;
        if (param && isControllerRow(Parameter(m_params[row]))) {
            // A pose controller's row: its own keyframes, the frames it was set on, joined by a line.
            int const c = m_params[row] - kControllerRows;
            if (c < scene.controllers.size()) {
                Scene::Controller const& controller = scene.controllers[c];
                QList<int> const keyed = controller.point ? controller.pointKeys.keys() : controller.keys.keys();
                p.setPen(QPen(keyColor, 1));
                for (int n = 0; n + 1 < keyed.size(); ++n)
                    p.drawLine(kHeaderWidth + keyed[n] * cw - sx + cw / 2, ky, kHeaderWidth + keyed[n + 1] * cw - sx + cw / 2, ky);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setPen(QPen(QColor(40, 30, 10), 1));
                p.setBrush(keyColor);
                double const kd = std::clamp(cw / 2.0, 2.5, 4.5);
                for (int frame : keyed) {
                    double const x = kHeaderWidth + frame * cw - sx + cw / 2.0;
                    QPolygonF diamond;
                    diamond << QPointF(x, ky - kd) << QPointF(x + kd, ky) << QPointF(x, ky + kd) << QPointF(x - kd, ky);
                    p.drawPolygon(diamond);
                }
                p.setRenderHint(QPainter::Antialiasing, false);
            }
            p.setPen(line);
            p.drawLine(kHeaderWidth, y + kRowHeight - 1, rowRight, y + kRowHeight - 1);
            continue;
        }
        for (int k = 0; k < layer.keys.size(); ++k)
            if ((layer.keys[k].parts & part) && (!effectRow || (keyedEffect && effect < layer.keys[k].pose.fx.size())))
                mine.push_back(k);
        for (int n = 0; n + 1 < mine.size(); ++n) {
            Keyframe const& key = layer.keys[mine[n]];
            if (key.tween) {
                int const x = kHeaderWidth + key.frame * cw - sx + cw / 2;
                int const x2 = kHeaderWidth + layer.keys[mine[n + 1]].frame * cw - sx + cw / 2;
                p.setPen(QPen(keyColor, 1));
                p.drawLine(x, ky, x2, ky);
            }
        }
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(40, 30, 10), 1));
        p.setBrush(keyColor);
        double const kd = std::clamp(cw / 2.0, 2.5, 4.5);
        for (int k : std::as_const(mine)) {
            Keyframe const& key = layer.keys[k];
            double const x = kHeaderWidth + key.frame * cw - sx + cw / 2.0;
            // (On the layer's row, a keyframe that sets only some of the pose is drawn smaller.)
            double const size = !param && key.parts != Keyframe::AllParts ? kd * 0.6 : kd;
            QPolygonF diamond;
            diamond << QPointF(x, ky - size) << QPointF(x + size, ky) << QPointF(x, ky + size) << QPointF(x - size, ky);
            p.drawPolygon(diamond);
        }
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(line);
        p.drawLine(kHeaderWidth, y + kRowHeight - 1, rowRight, y + kRowHeight - 1);
    }
    // the block selected: tinted, with a line round it
    if (int const row = rowOfLayer(m_layer); row >= 0 && m_extentRow >= 0) {
        int const r0 = std::min(row, m_extentRow), r1 = std::max(row, m_extentRow);
        QRect const block(kHeaderWidth + selectionFirstFrame() * cw - sx, kRulerHeight + r0 * kRowHeight - sy,
                          (selectionLastFrame() - selectionFirstFrame() + 1) * cw, (r1 - r0 + 1) * kRowHeight);
        p.fillRect(block, InterfaceColours::get("timelineSelection"));
        p.setPen(QPen(QColor(120, 220, 235), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(block.adjusted(0, 0, -1, -1));
    }
    // where the block being dragged would land
    if (int const row = rowOfLayer(m_layer); m_moving && row >= 0 && (m_moveRows || m_moveFrames)) {
        int const r0 = (m_extentRow < 0 ? row : std::min(row, m_extentRow)) + m_moveRows;
        int const r1 = (m_extentRow < 0 ? row : std::max(row, m_extentRow)) + m_moveRows;
        QRect const block(kHeaderWidth + (selectionFirstFrame() + m_moveFrames) * cw - sx, kRulerHeight + r0 * kRowHeight - sy,
                          (selectionLastFrame() - selectionFirstFrame() + 1) * cw, (r1 - r0 + 1) * kRowHeight);
        p.setPen(QPen(Qt::white, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(block.adjusted(0, 0, -1, -1));
    }
    // the selected cell
    if (int const row = rowOfLayer(m_layer); row >= 0) {
        int const x = kHeaderWidth + m_frame * cw - sx;
        int const y = kRulerHeight + row * kRowHeight - sy;
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(x, y, std::max(2, cw - 2), kRowHeight - 2);
    }
    p.restore();

}

// Eclipse - the Timeline painted: the ruler and the layer headers; a symbol's sound.
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

// The ruler: frame numbers, markers, the onion skin, the playback range, the scene's ends, the playhead.
void Timeline::paintRuler(QPainter& p, TimelinePaintSpace const& space)
{
    [[maybe_unused]] auto const& [scene, w, h, sx, sy, cw, frames, sceneEndX, firstFrame, lastFrame, bg, rowA, rowB, line, text, dim, exposed, exposedCurrent, currentRow, playhead, headerBg] = space;
    p.save();
    p.setClipRect(kHeaderWidth, 0, w - kHeaderWidth, h);
    p.fillRect(kHeaderWidth, 0, w - kHeaderWidth, kRulerHeight, QColor(22, 22, 22));
    if (sceneEndX > kHeaderWidth)
        p.fillRect(kHeaderWidth, 0, std::min(w, sceneEndX) - kHeaderWidth, kRulerHeight, QColor(98, 98, 98));
    QFont small = font();
    small.setPointSizeF(std::max(6.5, small.pointSizeF() - 1.5));
    p.setFont(small);
    int const step = cw >= 14 ? 5 : cw >= 7 ? 10 : 20;
    int const lastTick = (sx + w - kHeaderWidth) / cw;
    for (int f = firstFrame; f <= lastTick; ++f) {
        int const x = kHeaderWidth + f * cw - sx;
        bool const inScene = f < frames;
        bool const major = (f + 1) % step == 0;
        if (cw < 6 && !major && (f + 1) % 2)
            continue;  // too close together: every other one
        p.setPen(inScene ? QColor(40, 40, 40) : QColor(150, 150, 150));
        p.drawLine(x + cw / 2, major ? kRulerHeight - 9 : kRulerHeight - 5, x + cw / 2, kRulerHeight - 2);
        if (major) {
            p.setPen(inScene ? QColor(20, 20, 20) : QColor(200, 200, 200));
            p.drawText(QRect(x + cw / 2 - 20, 1, 40, 12), Qt::AlignHCenter | Qt::AlignTop, QString::number(f + 1));
        }
    }
    // Scene markers: a band of their colour along the top, and the name where there is room.
    for (int i = 0; i < scene.markers.size(); ++i) {
        QRect const band = markerRect(i);
        p.fillRect(band, scene.markers[i].color);
        p.fillRect(band.x(), 0, 1, kRulerHeight - 2, scene.markers[i].color);
        // Its name, on a tag of its colour, as far as the marker reaches.
        QString const& name = scene.markers[i].name;
        if (!name.isEmpty() && band.right() > kHeaderWidth) {
            QFont tiny = p.font();
            tiny.setPixelSize(9);
            p.save();
            p.setFont(tiny);
            QFontMetrics const metrics(tiny);
            int const room = std::max(band.width() - 2, 0);
            QString const shown = metrics.elidedText(name, Qt::ElideRight, std::max(room - 4, 0));
            if (!shown.isEmpty() && room >= 12) {
                QRect const tag(band.x() + 1, 5, std::min(metrics.horizontalAdvance(shown) + 5, room), 11);
                p.setClipRect(QRect(kHeaderWidth, 0, w - kHeaderWidth, kRulerHeight));
                p.fillRect(tag, scene.markers[i].color);
                p.setPen(scene.markers[i].color.lightness() > 140 ? QColor(20, 20, 20) : QColor(245, 245, 245));
                p.drawText(tag.adjusted(2, 0, -1, 0), Qt::AlignVCenter | Qt::AlignLeft, shown);
            }
            p.restore();
        }
    }
    // The onion skin: a blue bar along the foot of the ruler over the frames it reaches, a handle at each end.
    if (m_onionOn) {
        QColor const onion = InterfaceColours::get("timelineOnionSkin");
        QRect const before = onionHandleRect(true), after = onionHandleRect(false);
        p.save();
        p.setClipRect(QRect(kHeaderWidth, 0, w - kHeaderWidth, kRulerHeight));
        p.fillRect(QRect(before.center().x(), kRulerHeight - 4, after.center().x() - before.center().x(), 2), onion);
        p.fillRect(before, onion);
        p.fillRect(after, onion);
        p.restore();
    }
    // Where playback starts and stops, when that is not the whole scene.
    int const stop = m_playStop < 0 ? scene.frameCount() - 1 : std::min(m_playStop, scene.frameCount() - 1);
    if (m_playStart > 0 || stop < scene.frameCount() - 1) {
        QColor const range(60, 190, 205);
        int const x1 = kHeaderWidth + m_playStart * cw - sx;
        int const x2 = kHeaderWidth + (stop + 1) * cw - sx;
        p.fillRect(x1, kRulerHeight - 3, x2 - x1, 2, range);
        p.fillRect(x1, 2, 2, kRulerHeight - 4, range);
        p.fillRect(x2 - 2, 2, 2, kRulerHeight - 4, range);
    }
    // The start and the end of the scene: a red bracket each.  The one at the end can be dragged.
    auto bracket = [&](int x, int foot) {
        p.fillRect(x - 1, 0, 3, kRulerHeight, playhead);
        p.fillRect(foot < 0 ? x - 4 : x, 0, 5, 3, playhead);
        p.fillRect(foot < 0 ? x - 4 : x, kRulerHeight - 3, 5, 3, playhead);
    };
    bracket(kHeaderWidth - sx + 1, +1);
    if (m_showSceneEnd)
        bracket(sceneEndX, -1);
    // playhead
    int const px = kHeaderWidth + m_frame * cw - sx;
    p.fillRect(px, 0, std::max(2, cw - 1), kRulerHeight, QColor(playhead.red(), playhead.green(), playhead.blue(), 150));
    p.setPen(playhead);
    p.drawLine(px + cw / 2, kRulerHeight, px + cw / 2, h);
    p.restore();

}

// The layer headers: the switches, names, parameters, and where a dragged layer would go.
void Timeline::paintHeaders(QPainter& p, TimelinePaintSpace const& space)
{
    [[maybe_unused]] auto const& [scene, w, h, sx, sy, cw, frames, sceneEndX, firstFrame, lastFrame, bg, rowA, rowB, line, text, dim, exposed, exposedCurrent, currentRow, playhead, headerBg] = space;
    p.setFont(font());
    p.fillRect(0, 0, kHeaderWidth, h, headerBg);
    int const nameEnd = kHeaderWidth - kParamWidth;
    p.setRenderHint(QPainter::Antialiasing, true);
    // The pictures of the four switches, for the row over the layers and for each layer.
    auto eye = [&](QRect const& r, bool on) {
        QPointF const c = QRectF(r).center();
        p.setPen(QPen(on ? text : dim, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, 5.0, 3.2);
        if (on) {
            p.setPen(Qt::NoPen);
            p.setBrush(text);
            p.drawEllipse(c, 1.8, 1.8);
        }
    };
    auto soloMark = [&](QRect const& r, bool on) {
        QPointF const c = QRectF(r).center();
        p.setPen(QPen(on ? QColor(250, 205, 70) : dim, 1.2));
        p.setBrush(on ? QColor(250, 205, 70) : QColor(Qt::transparent));
        p.drawEllipse(c, 3.6, 3.6);
    };
    auto padlock = [&](QRect const& r, bool on) {
        QPointF const c = QRectF(r).center();
        QColor const colour = on ? QColor(240, 150, 60) : dim;
        p.setPen(QPen(colour, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(c.x() - 2.6, c.y() - 5.5, 5.2, 7.0), 0, 180 * 16);
        p.setBrush(on ? colour : QColor(Qt::transparent));
        p.drawRect(QRectF(c.x() - 4.0, c.y() - 1.5, 8.0, 6.0));
    };
    auto onionMark = [&](QRect const& r, bool on) {
        // Two sheets, one behind the other.
        QPointF const c = QRectF(r).center();
        QColor const colour = on ? QColor(120, 200, 150) : dim;
        p.setPen(QPen(colour, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(c.x() - 4.5, c.y() - 4.5, 6, 6));
        p.setBrush(on ? colour : QColor(Qt::transparent));
        p.drawRect(QRectF(c.x() - 1.5, c.y() - 1.5, 6, 6));
    };
    auto swatch = [&](QRect const& r, QColor const& colour) {
        QPointF const c = QRectF(r).center();
        p.setPen(QPen(QColor(30, 30, 30), 1));
        p.setBrush(colour);
        p.drawRect(QRectF(c.x() - 4.5, c.y() - 4.5, 9, 9));
    };

    // The row over the layers: the same four, acting on every layer; then the names.
    {
        bool allLocked = !scene.layers.isEmpty(), anySolo = false, anyOnion = false;
        for (Layer const& l : scene.layers) {
            allLocked = allLocked && l.locked;
            anySolo = anySolo || l.solo;
            anyOnion = anyOnion || l.onion;
        }
        QRect const top(0, 0, kSwitchWidth, kRulerHeight);
        eye(top.translated(3, 0), true);
        soloMark(top.translated(3 + kSwitchWidth, 0), anySolo);
        padlock(top.translated(3 + 2 * kSwitchWidth, 0), allLocked);
        onionMark(top.translated(3 + 3 * kSwitchWidth, 0), anyOnion);
        swatch(top.translated(3 + 4 * kSwitchWidth, 0), QColor(235, 235, 235));
        p.setPen(dim);
        QFont label = font();
        label.setPointSizeF(std::max(6.5, label.pointSizeF() - 1.0));
        p.setFont(label);
        p.drawText(QRect(kSwitches + 14, 0, 80, kRulerHeight), Qt::AlignVCenter | Qt::AlignLeft, tr("Layers"));
        p.drawText(QRect(nameEnd + 6, 0, kParamWidth - 6, kRulerHeight), Qt::AlignVCenter | Qt::AlignLeft, tr("Parameters"));
        p.setFont(font());
        // + (add a layer), - (delete), and a peg.
        p.setPen(QPen(text, 1.6));
        QPointF const add = QRectF(headerButtonRect(AddLayer)).center();
        p.drawLine(add + QPointF(-4.5, 0), add + QPointF(4.5, 0));
        p.drawLine(add + QPointF(0, -4.5), add + QPointF(0, 4.5));
        QPointF const del = QRectF(headerButtonRect(DeleteLayer)).center();
        p.drawLine(del + QPointF(-4.5, 0), del + QPointF(4.5, 0));
        QPointF const peg = QRectF(headerButtonRect(AddPeg)).center();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 190, 70));
        p.drawPolygon(QPolygonF({peg + QPointF(-5, -4), peg + QPointF(5, -4), peg + QPointF(0, 5)}));
    }

    p.save();
    p.setClipRect(0, kRulerHeight, kHeaderWidth, h - kRulerHeight);
    for (int row = 0; row < m_rows.size(); ++row) {
        int const layerIndex = m_rows[row];
        Layer const& layer = scene.layers[layerIndex];
        int const y = kRulerHeight + rowTop(row) - sy;
        int const rh = rowHeight(row);
        if (y + rh < kRulerHeight || y > h)
            continue;
        if (isParamRow(row)) {
            // The row of a parameter: its name, set in under the layer, and its value on this frame.
            Parameter const which = Parameter(m_params[row]);
            p.fillRect(0, y, kHeaderWidth, kRowHeight, QColor(60, 60, 60));
            p.setPen(dim);
            int const nameLeft = kSwitches + indentOf(layerIndex) + 40;
            p.drawText(QRect(nameLeft, y, nameEnd - nameLeft - 4, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft, rowName(layerIndex, which));
            QRect const box = parameterValueRect(layerIndex, which);
            p.setRenderHint(QPainter::Antialiasing, false);
            p.fillRect(box, QColor(46, 46, 46));
            if (int const by = drivenBy(layerIndex, which); by >= 0) {
                // Following another layer: its value, and whose it is.
                p.setPen(QColor(120, 190, 255));
                p.drawText(box.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                           parameterText(layerIndex, which) + QStringLiteral("  \u2190 ") + scene.layers[by].name);
            } else {
                bool keyed = layer.keyIndex(m_frame) >= 0;
                if (isControllerRow(which) && m_params[row] - kControllerRows < scene.controllers.size()) {
                    Scene::Controller const& c = scene.controllers[m_params[row] - kControllerRows];
                    keyed = c.point ? c.pointKeys.contains(m_frame) : c.keys.contains(m_frame);
                }
                p.setPen(keyed ? QColor(255, 190, 70) : text);
                p.drawText(box.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, parameterText(layerIndex, which));
            }
            p.setPen(line);
            p.drawLine(0, y + kRowHeight - 1, kHeaderWidth, y + kRowHeight - 1);
            p.setRenderHint(QPainter::Antialiasing, true);
            continue;
        }
        if (layerIndex == m_layer)
            p.fillRect(0, y, kHeaderWidth, rh, currentRow);
        else if (m_extraLayers.contains(layerIndex)
                 || (m_extentRow >= 0 && row >= std::min(rowOfLayer(m_layer), m_extentRow) && row <= std::max(rowOfLayer(m_layer), m_extentRow)))
            p.fillRect(0, y, kHeaderWidth, rh, mixed(headerBg, QColor(42, 155, 176), 0.45));
        eye(switchRect(layerIndex, Enable), layer.visible);
        soloMark(switchRect(layerIndex, Solo), layer.solo);
        padlock(switchRect(layerIndex, Lock), layer.locked);
        if (layer.holdsDrawings())
            onionMark(switchRect(layerIndex, Onion), layer.onion);
        swatch(switchRect(layerIndex, Colour), layer.trackColor.isValid() ? layer.trackColor : QColor(120, 120, 120));

        int const indent = indentOf(layerIndex);
        int textX = kSwitches + indent;
        if (scene.hasChildren(layerIndex)) {
            QPointF const c(textX + 5.5, y + kRowHeight / 2.0);
            p.setPen(Qt::NoPen);
            p.setBrush(text);
            if (layer.collapsed)
                p.drawPolygon(QPolygonF({c + QPointF(-2.5, -4), c + QPointF(3, 0), c + QPointF(-2.5, 4)}));
            else
                p.drawPolygon(QPolygonF({c + QPointF(-4, -2.5), c + QPointF(4, -2.5), c + QPointF(0, 3)}));
        }
        textX += 13;
        int const iy = y + (kRowHeight - 24) / 2;  // the little pictures were drawn for a 24 pixel row
        if (layer.kind == Layer::Sound) {
            // A little loudspeaker.
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(120, 200, 150));
            QPolygonF speaker;
            speaker << QPointF(textX, iy + 10) << QPointF(textX + 4, iy + 10) << QPointF(textX + 9, iy + 6) << QPointF(textX + 9, iy + 18)
                    << QPointF(textX + 4, iy + 14) << QPointF(textX, iy + 14);
            p.drawPolygon(speaker);
        } else if (layer.kind == Layer::Camera) {
            // A little camera: a body and a lens.
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(111, 168, 232));
            p.drawRect(textX, iy + 8, 7, 8);
            QPolygonF lens;
            lens << QPointF(textX + 7, iy + 12) << QPointF(textX + 11, iy + 8) << QPointF(textX + 11, iy + 16);
            p.drawPolygon(lens);
        } else if (layer.kind == Layer::Peg) {
            QPolygonF mark;
            mark << QPointF(textX, iy + 7) << QPointF(textX + 9, iy + 7) << QPointF(textX + 4.5, iy + 16);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 190, 70));
            p.drawPolygon(mark);
        } else if (layer.kind == Layer::ColourCard) {
            // A card of its colour.
            p.setPen(QPen(QColor(30, 30, 30), 1));
            p.setBrush(layer.cardColor);
            p.drawRect(QRectF(textX + 0.5, iy + 7.5, 10, 9));
        } else if (layer.kind == Layer::Group) {
            // A folder.
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(200, 180, 120));
            p.drawRect(QRectF(textX, iy + 7, 5, 2));
            p.drawRect(QRectF(textX, iy + 9, 11, 7.5));
        } else if (layer.kind == Layer::Bone) {
            // A bone: a kite on its side.
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(120, 220, 235));
            p.drawPolygon(QPolygonF({QPointF(textX, iy + 12), QPointF(textX + 2.5, iy + 8.5), QPointF(textX + 11, iy + 12), QPointF(textX + 2.5, iy + 15.5)}));
        } else if (layer.kind == Layer::Quadmap) {
            // Four corners pulled out of square.
            p.setPen(QPen(QColor(120, 220, 235), 1.3));
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(QPolygonF({QPointF(textX + 1, iy + 8), QPointF(textX + 10, iy + 9.5), QPointF(textX + 8.5, iy + 16), QPointF(textX, iy + 14)}));
        } else if (layer.showsSymbol()) {
            // A layer that shows a symbol: a box with something in it.
            p.setPen(QPen(QColor(190, 150, 240), 1.3));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(textX + 0.5, iy + 7.5, 9, 9));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(190, 150, 240));
            p.drawEllipse(QPointF(textX + 5.0, iy + 12.0), 2.2, 2.2);
        } else {
            // A drawing layer: a sheet with a stroke on it.
            p.setPen(QPen(QColor(200, 200, 200), 1));
            p.setBrush(QColor(236, 236, 236));
            p.drawRect(QRectF(textX + 0.5, iy + 7.5, 9, 9));
            p.setPen(QPen(QColor(214, 80, 60), 1.4));
            p.drawLine(QPointF(textX + 2.5, iy + 14), QPointF(textX + 7.5, iy + 10));
        }
        textX += 15;
        // The + that opens the parameters of the layer (a - once they are open).
        if (hasParameters(layerIndex)) {
            QRectF const box = QRectF(parameterToggleRect(layerIndex)).adjusted(2.5, 2.5, -2.5, -2.5);
            QPointF const c = box.center();
            p.setPen(QPen(dim, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(box);
            p.setPen(QPen(text, 1.2));
            p.drawLine(c + QPointF(-2.5, 0), c + QPointF(2.5, 0));
            if (!parametersShown(layerIndex))
                p.drawLine(c + QPointF(0, -2.5), c + QPointF(0, 2.5));
        }
        if (layer.tagged) {
            // A tagged layer: a small flag in the corner of its name.
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(240, 200, 60));
            p.drawPolygon(QPolygonF({QPointF(nameEnd - 30, y + 2), QPointF(nameEnd - 23, y + 2), QPointF(nameEnd - 23, y + 9)}));
            p.setBrush(Qt::NoBrush);
        }
        if (QRect const mark = syncMarkRect(layerIndex); !mark.isNull()) {
            // A synced layer: two links of a chain, in the colour of the layers it keeps time with.
            p.setRenderHint(QPainter::Antialiasing, true);
            // (A clone: the links are filled.)
            QColor const colour = layer.cloneGroup != 0 ? syncColour(layer.cloneGroup + 3) : syncColour(layer.syncGroup);
            p.setPen(QPen(colour, 1.6));
            p.setBrush(layer.cloneGroup != 0 ? QBrush(colour) : QBrush(Qt::NoBrush));
            p.drawRoundedRect(QRectF(mark.left() + 0.5, mark.top() + 0.5, 8, 7), 3.5, 3.5);
            p.drawRoundedRect(QRectF(mark.left() + 5.5, mark.top() + 0.5, 8, 7), 3.5, 3.5);
            p.setRenderHint(QPainter::Antialiasing, false);
        }
        p.setPen(!layer.visible ? dim : layer.locked ? QColor(190, 170, 150) : text);
        p.drawText(QRect(textX, y, nameEnd - textX - 22, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(layer.name, Qt::ElideRight, nameEnd - textX - 22));
        // Parameters: what the layer shows on the current frame.
        QString value;
        if (layer.holdsDrawings()) {
            int const drawing = layer.drawingAt(m_frame);
            value = drawing == Layer::kNoDrawing ? QString() : layer.drawingLabel(drawing);
        } else if (layer.kind == Layer::Sound) {
            value = QString::number(layer.soundStart + 1);
        } else if (!layer.keys.isEmpty()) {
            value = QString::number(layer.keys.size());
        }
        p.setPen(dim);
        p.drawText(QRect(nameEnd + 6, y, kParamWidth - 10, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft, value);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(line);
        p.drawLine(0, y + rh - 1, kHeaderWidth, y + rh - 1);
        p.setRenderHint(QPainter::Antialiasing, true);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    // Where a dragged layer would go.
    if (m_dragging && m_drop != DropNone && m_dropLayer >= 0 && rowOfLayer(m_dropLayer) >= 0) {
        int const y = kRulerHeight + rowTop(rowOfLayer(m_dropLayer)) - sy;
        int const rh = rowHeight(rowOfLayer(m_dropLayer));
        QColor const mark(64, 156, 255);
        if (m_drop == DropOnto) {
            p.setPen(QPen(mark, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(kSwitches, y + 1, nameEnd - kSwitches - 3, rh - 3);
        } else {
            int const edge = m_drop == DropAbove ? y : y + rh - 1;
            p.fillRect(0, edge - 1, kHeaderWidth, 3, mark);
        }
    }
    p.restore();
    p.setPen(line);
    p.drawLine(kHeaderWidth - 1, 0, kHeaderWidth - 1, h);
    p.drawLine(nameEnd, 0, nameEnd, h);
    p.drawLine(0, kRulerHeight - 1, w, kRulerHeight - 1);
}

std::shared_ptr<SoundData> Timeline::symbolSound(int l) const
{
    Scene const& scene = m_document->scene();
    if (l < 0 || l >= scene.layers.size() || !scene.layers[l].showsSymbol())
        return nullptr;
    Layer const& layer = scene.layers[l];
    // What the sound depends on: the layer's frames, and the sounds and frames inside the symbol (and the symbols in it).
    quint64 key = qHash(layer.cells) ^ (quint64(scene.frameCount()) << 32) ^ quint64(layer.visible) ^ qHash(scene.fps);
    bool any = false;
    std::function<void(int, int)> look = [&](int symbolId, int depth) {
        Scene::Symbol const* const symbol = scene.findSymbol(symbolId);
        if (!symbol || depth > Scene::kSymbolDepth)
            return;
        key = key * 1099511628211ull ^ quint64(symbolId);
        for (Layer const& inner : symbol->layers) {
            if (inner.kind == Layer::Sound) {
                any = true;
                key = key * 1099511628211ull ^ qHash(inner.soundFile) ^ quint64(inner.soundStart) * 31u ^ quint64(inner.visible);
            } else if (inner.showsSymbol()) {
                key = key * 1099511628211ull ^ qHash(inner.cells);
                look(inner.symbolId, depth + 1);
            }
        }
    };
    look(layer.symbolId, 0);
    if (!any)
        return nullptr;
    auto const found = m_symbolSounds.constFind(layer.id);
    if (found != m_symbolSounds.constEnd() && found->first == key)
        return found->second;
    // The scene with that layer alone, mixed.
    Scene alone = scene;
    alone.layers = {layer};
    alone.layers[0].parent = 0;
    auto made = std::make_shared<SoundData>();
    made->samples = Sound::mix(alone);
    std::shared_ptr<SoundData> const sound = made->samples.isEmpty() ? nullptr : made;
    m_symbolSounds.insert(layer.id, qMakePair(key, sound));
    return sound;
}

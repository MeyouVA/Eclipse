// Eclipse - rendering frames of the scene, several at once.
#include "canvas/FrameRender.h"

#include <QMutex>
#include <QPainter>
#include <QThread>
#include <QThreadPool>
#include <QColorSpace>

#include <algorithm>
#include <atomic>
#include <cmath>

#include "canvas/Canvas.h"
#include "colour/images/ColourTransform.h"
#include "colour/icc/Icc.h"

namespace {

std::atomic<int> gThreads{0};

struct Precision {
    int bits=8;
    QColorSpace space;
    bool mixed=false;
};

Precision precision(Scene const& scene,int frame,int depth=0)
{
    Precision result;
    if(scene.colourManaged) result.bits=16;
    if(depth>Scene::kSymbolDepth) return result;
    auto note=[&](QColorSpace const& space) {
        if(!space.isValid()) return;
        if(result.space.isValid() && result.space!=space) result.mixed=true;
        else result.space=space;
    };
    for(int l=0;l<scene.layers.size();++l) {
        if(!scene.enabled(l)) continue;
        Layer const& layer=scene.layers[l];
        int drawn=frame,moved=frame; scene.timedFrames(l,frame,&drawn,&moved);
        int const index=layer.drawingAt(drawn);
        if(layer.symbolId && index>=0) {
            if(auto const* symbol=scene.findSymbol(layer.symbolId)) {
                Precision const inner=precision(scene.symbolScene(*symbol),index,depth+1);
                result.bits=std::max(result.bits,inner.bits); note(inner.space); result.mixed=result.mixed||inner.mixed;
            }
        } else if(index>=0 && index<layer.drawings.size()) {
            for(Picture const& picture : layer.drawings[index].pictures) {
                result.bits=std::max(result.bits,picture.image.pixelFormat().typeInterpretation()==QPixelFormat::FloatingPoint ? 32 : picture.image.depth()>32 ? 16 : 8);
                note(picture.image.colorSpace());
            }
        }
    }
    if(scene.colourManaged) result.space=QColorSpace::fromIccProfile(Cms::Icc::profileOf(scene.workingSpace));
    else if(result.mixed) result.space=QColorSpace(QColorSpace::SRgb);
    return result;
}

}  // namespace

namespace FrameRender {

void setThreads(int count)
{
    gThreads = std::clamp(count, 0, 64);
}

int threads()
{
    return gThreads;
}

int threadsUsed()
{
    int const set = gThreads;
    return set > 0 ? set : std::clamp(QThread::idealThreadCount(), 1, 16);
}

QSize sizeAt(Scene const& scene, double scale)
{
    return QSize(std::max(1, int(std::lround(scene.width * scale))), std::max(1, int(std::lround(scene.height * scale))));
}

QImage frame(Scene const& scene, int f, QSize size, bool quick, QColor const& under)
{
    // (Colour managed: painted with 16 bits a channel, so that light kept linear does not band in the darks.)
    Precision const source=precision(scene,f);
    QImage image(size, source.bits>16 ? QImage::Format_RGBA32FPx4_Premultiplied : source.bits>8 ? QImage::Format_RGBA64_Premultiplied : QImage::Format_ARGB32_Premultiplied);
    if(source.bits>8 && source.space.isValid()) image.setColorSpace(source.space);
    image.fill(under);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, !quick);
    painter.scale(double(size.width()) / std::max(1, scene.width), double(size.height()) / std::max(1, scene.height));
    Canvas::paintFrame(painter, scene, f);
    painter.end();
    return image;
}

QImage inSpace(Scene const& scene, QImage const& image, QString const& space, bool floats)
{
    // A picture of 16 bits a channel comes out with 8 (or floats): converted first, so the 8 bits are of the colour space written.
    bool const deep = image.depth()>32;
    if (space.isEmpty())
        return deep && !floats ? image.convertToFormat(QImage::Format_ARGB32_Premultiplied) : image;
    Cms::Transform const out = scene.outputTransform(space);
    if (floats)
        return out.toFloat(image);
    QImage converted = out.applied(image);
    return deep ? converted.convertToFormat(QImage::Format_ARGB32_Premultiplied) : converted;
}

bool render(Scene const& scene, QVector<int> const& frames, QSize size, bool quick,
            std::function<QString(int, QImage const&)> const& keep, std::function<bool(int)> const& progress, QString* error, QColor const& under)
{
    int const count = std::min(threadsUsed(), int(frames.size()));
    if (count <= 1) {
        int done = 0;
        for (int f : frames) {
            QString const why = keep(f, frame(scene, f, size, quick, under));
            if (!why.isEmpty()) {
                if (error)
                    *error = why;
                return false;
            }
            ++done;
            if (progress && !progress(done))
                return false;
        }
        return true;
    }
    QThreadPool pool;
    pool.setMaxThreadCount(count);
    pool.setStackSize(8u * 1024u * 1024u);
    std::atomic<bool> stop{false};
    std::atomic<int> done{0};
    QMutex lock;
    QString first;
    for (int f : frames)
        pool.start([&, f] {
            if (stop)
                return;
            QString const why = keep(f, frame(scene, f, size, quick, under));
            if (!why.isEmpty()) {
                QMutexLocker const locked(&lock);
                if (first.isEmpty())
                    first = why;
                stop = true;
            }
            ++done;
        });
    bool stopped = false;
    while (!pool.waitForDone(30))
        if (progress && !stopped && !progress(done)) {
            stopped = true;
            stop = true;
        }
    // (Told at the end too: a stop asked for while the last frames were made still counts.)
    if (progress && !stopped && !stop && !progress(done))
        stopped = true;
    if (!first.isEmpty() && error)
        *error = first;
    return first.isEmpty() && !stopped;
}

}  // namespace FrameRender

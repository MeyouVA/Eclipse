#include "audio/sound/Sound.h"
#include <algorithm>
#include <cmath>

QVector<qint16> Sound::scrub(QVector<qint16> const& stereo,double first,double last,int outputFrames)
{
    if(stereo.size()<2 || outputFrames<=0 || !std::isfinite(first) || !std::isfinite(last)) return {};
    outputFrames=std::min(outputFrames,SoundData::kRate/2);
    QVector<qint16> result(outputFrames*2,0);
    for(int i=0;i<outputFrames;++i) {
        double const at=first+(last-first)*i/outputFrames;
        if(at<0 || at>=stereo.size()/2) continue;
        qsizetype const a=qsizetype(at),b=std::min<qsizetype>(a+1,stereo.size()/2-1);
        double const t=at-double(a);
        for(int c=0;c<2;++c) result[i*2+c]=qint16(std::lround(stereo[a*2+c]*(1-t)+stereo[b*2+c]*t));
    }
    return result;
}

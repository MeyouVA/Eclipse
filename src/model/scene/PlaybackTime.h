#pragma once
#include <algorithm>
#include <cmath>
#include <QtGlobal>

namespace PlaybackTime {
struct Position { int frame=0; qint64 cycle=0; bool holding=false,ended=false; };
inline Position at(int first,int last,int from,int direction,double rate,double elapsed,int pause,bool loop)
{
    from=std::clamp(from,first,last);
    bool const backward=direction<0;
    int const terminal=backward?first:last,begin=backward?last:first;
    double const duration=(std::abs(terminal-from)+1)/rate;
    double const hold=std::max(0,pause)/1000.0;
    double time=std::max(0.0,elapsed);
    if(time<duration) return {from+(backward?-1:1)*int(std::floor(time*rate)),0,false,false};
    if(!loop) return {terminal,0,false,true};
    if(time<duration+hold) return {terminal,0,true,false};
    time-=duration+hold;
    double const playing=(last-first+1)/rate,period=playing+hold;
    qint64 const cycle=1+qint64(std::floor(time/period));
    double const within=std::fmod(time,period);
    if(within>=playing) return {terminal,cycle,true,false};
    return {std::clamp(begin+(backward?-1:1)*int(std::floor(within*rate)),first,last),cycle,false,false};
}
}

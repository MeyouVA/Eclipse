#pragma once
#include <algorithm>
#include <cmath>
#include <QtGlobal>

namespace PlaybackTime {
struct Position { int frame=0; qint64 cycle=0; bool holding=false,ended=false; };
struct PreviewPosition { Position position; bool blank=false; };
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
inline PreviewPosition preview(int first,int last,int from,int direction,double rate,double elapsed,int pause,bool loop,int padding,int initialPadding)
{
    if(padding<=0 && initialPadding<=0) return {at(first,last,from,direction,rate,elapsed,pause,loop),false};
    from=std::clamp(from,first,last); bool const backward=direction<0; int const terminal=backward?first:last,begin=backward?last:first;
    double const lead=std::max(0,initialPadding)/rate,play=(std::abs(terminal-from)+1)/rate,hold=std::max(0,pause)/1000.0;
    double time=std::max(0.0,elapsed);
    if(time<lead) return {{from,0,false,false},true};
    time-=lead;
    if(time<play) return {{from+(backward?-1:1)*int(std::floor(time*rate)),0,false,false},false};
    if(!loop) return {{terminal,0,false,true},false};
    if(time<play+hold) return {{terminal,0,true,false},false};
    time-=play+hold;
    double const pad=std::max(0,padding)/rate,full=(last-first+1)/rate,period=2*pad+full+hold;
    qint64 const cycle=1+qint64(std::floor(time/period)); double const within=std::fmod(time,period);
    if(within<2*pad) return {{within<pad?terminal:begin,cycle,false,false},true};
    if(within<2*pad+full) return {{std::clamp(begin+(backward?-1:1)*int(std::floor((within-2*pad)*rate)),first,last),cycle,false,false},false};
    return {{terminal,cycle,true,false},false};
}
}

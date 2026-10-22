// Eclipse - a layer: what its files share.
#pragma once

#include "model/scene/Scene.h"
#include "model/geometry/Morph.h"

#include "model/geometry/Deform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace layer_shared {

inline double between(double x, double v, double a, double lo, double hi, double t, double keep)
{
    for (int hits = 0; hits < 64; ++hits) {
        // The soonest of the two walls it reaches: x + v s + a s^2 / 2 = wall.
        double hit = -1.0;
        for (double const wall : {lo, hi}) {
            double const qa = 0.5 * a, qb = v, qc = x - wall;
            QVector<double> roots;
            if (std::abs(qa) < 1e-12) {
                if (std::abs(qb) > 1e-12)
                    roots << -qc / qb;
            } else if (double const d = qb * qb - 4.0 * qa * qc; d >= 0.0) {
                roots << (-qb - std::sqrt(d)) / (2.0 * qa) << (-qb + std::sqrt(d)) / (2.0 * qa);
            }
            for (double s : roots)
                if (s > 1e-9 && (hit < 0.0 || s < hit))
                    hit = s;
        }
        if (hit < 0.0 || hit > t)
            return std::clamp(x + v * t + 0.5 * a * t * t, lo, hi);
        x = x + v * hit + 0.5 * a * hit * hit;
        x = std::clamp(x, lo, hi);
        v = -(v + a * hit) * keep;
        t -= hit;
    }
    return std::clamp(x, lo, hi);
}


}  // namespace layer_shared

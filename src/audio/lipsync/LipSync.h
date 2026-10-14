// Eclipse - lip-sync: which shape a mouth has on each frame of a sound.
//
// There are eight shapes, lettered as animators letter them:
//   A  closed, lips together (m, b, p)        E  round (o)
//   B  a little open, teeth together (s, t, d, k, i)   F  small and pushed out (u, w, oo)
//   C  open (e)                                G  top teeth on the lower lip (f, v)
//   D  wide open (a)                           X  at rest: nothing is being said
// The detection listens to how loud each frame of the sound is and where its weight lies -
// a hiss, a hum, a shout - and, for a voiced sound, which vowel it is by the two lowest
// resonances of the voice (its formants); and chooses the shape that goes with it.  It does not
// understand words: it is a first pass for the animator to correct, as such detection always is.
#pragma once

#include <QChar>
#include <QString>
#include <QVector>

namespace LipSync {

inline constexpr char kShapes[] = "ABCDEFGX";
inline constexpr int kShapeCount = 8;

// The place of a letter among the shapes, or -1.
int indexOf(QChar shape);
// What the shape is, in a few words.
QString describe(QChar shape);
// A letter for every frame of the sound (16-bit stereo at `rate`), at `fps` frames a second.
QString detect(QVector<qint16> const& stereo, int rate, double fps);
// The two lowest resonances of a stretch of voice (mono, -1..1, at `rate`), in Hz, found from its spectral envelope
// (linear prediction); false when there are not two to be found.
bool formants(QVector<double> const& samples, int rate, double& first, double& second);
// The shape that goes with a vowel of those resonances.
QChar vowelShape(double first, double second);
// The mouth shapes a word says, one for each sound of it, read from its spelling (English, as it is mostly spelt).
QString wordShapes(QString const& word);
// The words said (`text`) laid over a sound whose shapes were detected (`detected`, a letter a frame): each stretch of speech
// in it (the frames not at rest, a pause of a frame within one not counting) says as many of the words as its length is a
// share of all the speech; within it each sound of a word takes as long as each other.  Frames between stay at rest.  With
// no speech found, the words take the whole sound.
QString fromText(QString const& text, QString const& detected);

}

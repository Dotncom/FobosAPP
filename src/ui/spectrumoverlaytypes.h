#ifndef SPECTRUMOVERLAYTYPES_H
#define SPECTRUMOVERLAYTYPES_H

#include <QString>

struct GraphBandMarker {
    double startHz = 0.0;
    double endHz = 0.0;
    QString label;
    bool amateur = false;
};

#endif // SPECTRUMOVERLAYTYPES_H

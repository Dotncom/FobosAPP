#ifndef SPECTRUMDISPLAYREDUCER_H
#define SPECTRUMDISPLAYREDUCER_H

#include <vector>

struct SpectrumDisplayFrame {
    std::vector<float> frequencies;
    std::vector<float> levels;
    std::vector<float> overlayLevels;
};

void prepareSpectrumDisplayFrame(const std::vector<float> &sourceFrequencies,
                                 const std::vector<float> &sourceLevels,
                                 const std::vector<float> *sourceOverlayLevels,
                                 int sourceCount,
                                 double minFrequency,
                                 double maxFrequency,
                                 int targetBins,
                                 float fallbackLevel,
                                 SpectrumDisplayFrame &output);

#endif // SPECTRUMDISPLAYREDUCER_H

#ifndef TRANSMITWAVEFORMGENERATOR_H
#define TRANSMITWAVEFORMGENERATOR_H

#include "transmitterbackend.h"

#include <QString>
#include <QVector>

#include <array>
#include <complex>

struct TxGenerationResult {
    QVector<std::complex<float>> iq;
    QString error;
    QString summary;
    double durationSeconds = 0.0;

    bool ok() const { return error.isEmpty() && !iq.isEmpty(); }
};

struct TxModulatorState {
    double phase = 0.0;
    std::array<float, 63> hilbertHistory{};
    int hilbertWriteIndex = 0;
};

class TransmitWaveformGenerator {
public:
    static bool isTextMode(TxModulation modulation);
    static QString modulationName(TxModulation modulation);
    static TxGenerationResult generateText(const TxConfiguration &configuration,
                                           const QString &text);
    static QVector<std::complex<float>> modulateAudio(const TxConfiguration &configuration,
                                                      const QVector<float> &audio,
                                                      TxModulatorState *state = nullptr);

private:
    static TxGenerationResult generateCw(const TxConfiguration &configuration,
                                         const QString &text);
    static TxGenerationResult generateFt8(const TxConfiguration &configuration,
                                          const QString &text);
};

#endif // TRANSMITWAVEFORMGENERATOR_H

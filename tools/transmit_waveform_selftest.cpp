#include "transmitwaveformgenerator.h"

#include <QCoreApplication>
#include <QDebug>

#include <cmath>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    TxConfiguration configuration;
    configuration.modulation = TxModulation::Cw;
    configuration.sampleRate = 8000;
    configuration.toneHz = 700.0;
    configuration.cwWpm = 18;
    const TxGenerationResult cw =
        TransmitWaveformGenerator::generateText(configuration,
                                                QString::fromUtf8(u8"SOS УКРАЇНА"));
    if (!cw.ok() || cw.durationSeconds < 1.0) {
        qCritical().noquote() << "CW generation failed:" << cw.error;
        return 1;
    }

    configuration.modulation = TxModulation::Ft8;
    configuration.sampleRate = 48000;
    const TxGenerationResult ft8 =
        TransmitWaveformGenerator::generateText(configuration, QStringLiteral("CQ TEST KN34"));
    const int expectedSamples = qRound(79.0 * 0.160 * configuration.sampleRate);
    if (!ft8.ok() || ft8.iq.size() != expectedSamples) {
        qCritical().noquote() << "FT8 generation failed:" << ft8.error
                              << "samples" << ft8.iq.size()
                              << "expected" << expectedSamples;
        return 2;
    }

    QVector<float> audio(4096);
    for (int i = 0; i < audio.size(); ++i) {
        audio[i] = static_cast<float>(0.5 * std::sin(6.283185307179586 * 1000.0 * i /
                                                   configuration.sampleRate));
    }
    for (TxModulation mode : {TxModulation::Am,
                              TxModulation::Nfm,
                              TxModulation::Wfm,
                              TxModulation::Dsb,
                              TxModulation::Usb,
                              TxModulation::Lsb}) {
        configuration.modulation = mode;
        TxModulatorState state;
        const QVector<std::complex<float>> iq =
            TransmitWaveformGenerator::modulateAudio(configuration, audio, &state);
        if (iq.size() != audio.size()) return 3;
        for (const std::complex<float> &sample : iq) {
            if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) return 4;
        }
    }
    return 0;
}

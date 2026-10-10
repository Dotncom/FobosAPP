#include "transmitwaveformgenerator.h"
#include "transmitmediagenerator.h"
#include "videoprocessor.h"

#include <QCoreApplication>
#include <QColor>
#include <QDebug>
#include <QImage>

#include <algorithm>
#include <cmath>
#include <cstring>

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

    QImage image(32, 24, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixelColor(x, y, QColor::fromRgb((x * 255) / image.width(),
                                                      (y * 255) / image.height(),
                                                      ((x + y) * 255) /
                                                          (image.width() + image.height())));
        }
    }
    const TxAudioMedia sstv = TransmitMediaGenerator::generateSstv(
        image, QStringLiteral("robot36"), 12000);
    if (!sstv.ok() || sstv.samples.size() < 12000) return 5;

    configuration.sampleRate = 2400000;
    configuration.modulation = TxModulation::Am;
    const QVector<std::complex<float>> atv =
        TransmitMediaGenerator::generateAtvFrame(image, configuration, false, 384, 288);
    if (atv.isEmpty()) return 6;
    for (const std::complex<float> &sample : atv) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) return 7;
    }

    QByteArray atvIq;
    atvIq.resize(atv.size() * 4);
    char *atvBytes = atvIq.data();
    for (int i = 0; i < atv.size(); ++i) {
        const qint16 iSample = static_cast<qint16>(std::lround(
            (std::clamp)(atv[i].real(), -1.0f, 1.0f) * 32767.0f));
        const qint16 qSample = static_cast<qint16>(std::lround(
            (std::clamp)(atv[i].imag(), -1.0f, 1.0f) * 32767.0f));
        std::memcpy(atvBytes + i * 4, &iSample, sizeof(iSample));
        std::memcpy(atvBytes + i * 4 + 2, &qSample, sizeof(qSample));
    }
    VideoProcessor decoder;
    QImage decodedAtv;
    QObject::connect(&decoder, &VideoProcessor::frameReady,
                     [&decodedAtv](const QImage &frame) { decodedAtv = frame; });
    decoder.configure(true, VideoProcessor::AmVideo, 15625.0, 384, 288,
                      false, true, true);
    decoder.processIqFrame(atvIq, configuration.sampleRate, atv.size());
    if (decodedAtv.isNull()) return 8;
    int darkest = 255;
    int brightest = 0;
    for (int y = 0; y < decodedAtv.height(); y += 8) {
        const uchar *line = decodedAtv.constScanLine(y);
        for (int x = 0; x < decodedAtv.width(); x += 8) {
            darkest = (std::min)(darkest, static_cast<int>(line[x]));
            brightest = (std::max)(brightest, static_cast<int>(line[x]));
        }
    }
    if (brightest - darkest < 40) return 9;
    return 0;
}

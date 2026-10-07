#include "videoprocessor.h"

#include <QApplication>
#include <QDebug>
#include <QImage>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace {
constexpr int SampleRate = 48000;
constexpr double TwoPi = 6.28318530717958647692;

void appendTone(QByteArray &pcm,
                double frequencyHz,
                double seconds,
                double &phase,
                double toneOffsetHz = 0.0) {
    const int sampleCount = qRound(seconds * SampleRate);
    const double phaseStep = TwoPi * (frequencyHz + toneOffsetHz) / SampleRate;
    pcm.reserve(pcm.size() + sampleCount * static_cast<int>(sizeof(qint16)));
    for (int sample = 0; sample < sampleCount; ++sample) {
        const qint16 value = static_cast<qint16>(std::lround(std::sin(phase) * 22000.0));
        pcm.append(static_cast<char>(value & 0xff));
        pcm.append(static_cast<char>((value >> 8) & 0xff));
        phase = std::remainder(phase + phaseStep, TwoPi);
    }
}

void appendSilence(QByteArray &pcm, double seconds) {
    pcm.append(QByteArray(qRound(seconds * SampleRate) * static_cast<int>(sizeof(qint16)), '\0'));
}

void appendVis(QByteArray &pcm, int visCode, double &phase, double toneOffsetHz) {
    appendTone(pcm, 1900.0, 0.300, phase, toneOffsetHz);
    appendTone(pcm, 1200.0, 0.010, phase, toneOffsetHz);
    appendTone(pcm, 1900.0, 0.300, phase, toneOffsetHz);
    appendTone(pcm, 1200.0, 0.030, phase, toneOffsetHz);

    int oneCount = 0;
    for (int bit = 0; bit < 7; ++bit) {
        const bool one = (visCode & (1 << bit)) != 0;
        oneCount += one ? 1 : 0;
        appendTone(pcm, one ? 1100.0 : 1300.0, 0.030, phase, toneOffsetHz);
    }
    appendTone(pcm, (oneCount & 1) != 0 ? 1100.0 : 1300.0, 0.030, phase, toneOffsetHz);
    appendTone(pcm, 1200.0, 0.030, phase, toneOffsetHz);
}

void appendMartinM1Line(QByteArray &pcm,
                        double &phase,
                        double toneOffsetHz,
                        double timingScale) {
    const int initialBytes = pcm.size();
    appendTone(pcm, 1200.0, 0.004862 * timingScale, phase, toneOffsetHz);
    appendTone(pcm, 1500.0, 0.000572 * timingScale, phase, toneOffsetHz);
    appendTone(pcm, 1700.0, 0.146432 * timingScale, phase, toneOffsetHz);
    appendTone(pcm, 1500.0, 0.000572 * timingScale, phase, toneOffsetHz);
    appendTone(pcm, 1900.0, 0.146432 * timingScale, phase, toneOffsetHz);
    appendTone(pcm, 1500.0, 0.000572 * timingScale, phase, toneOffsetHz);
    appendTone(pcm, 2100.0, 0.146432 * timingScale, phase, toneOffsetHz);
    appendTone(pcm, 1500.0, 0.000572 * timingScale, phase, toneOffsetHz);

    const int writtenSamples = (pcm.size() - initialBytes) /
                               static_cast<int>(sizeof(qint16));
    const int targetSamples = qRound(0.446446 * timingScale * SampleRate);
    if (writtenSamples < targetSamples) {
        appendTone(pcm,
                   1500.0,
                   static_cast<double>(targetSamples - writtenSamples) / SampleRate,
                   phase,
                   toneOffsetHz);
    }
}

void appendScottieStart(QByteArray &pcm,
                        double &phase,
                        double toneOffsetHz) {
    appendTone(pcm, 1200.0, 0.009, phase, toneOffsetHz);
    appendTone(pcm, 1500.0, 0.0015, phase, toneOffsetHz);
    appendTone(pcm, 1700.0, 0.138240, phase, toneOffsetHz); // G0
    appendTone(pcm, 1500.0, 0.0015, phase, toneOffsetHz);
    appendTone(pcm, 1900.0, 0.138240, phase, toneOffsetHz); // B0
}

void appendScottieRegularSegment(QByteArray &pcm,
                                 double &phase,
                                 double toneOffsetHz) {
    appendTone(pcm, 1200.0, 0.009, phase, toneOffsetHz);
    appendTone(pcm, 1500.0, 0.0015, phase, toneOffsetHz);
    appendTone(pcm, 2100.0, 0.138240, phase, toneOffsetHz); // current R
    appendTone(pcm, 1500.0, 0.0015, phase, toneOffsetHz);
    appendTone(pcm, 1700.0, 0.138240, phase, toneOffsetHz); // next G
    appendTone(pcm, 1500.0, 0.0015, phase, toneOffsetHz);
    appendTone(pcm, 1900.0, 0.138240, phase, toneOffsetHz); // next B
}

void appendRobot36Line(QByteArray &pcm,
                       double &phase,
                       bool redDifferenceLine) {
    appendTone(pcm, 1200.0, 0.009, phase);
    appendTone(pcm, 1500.0, 0.003, phase);
    appendTone(pcm, 1500.0, 0.044, phase);
    appendTone(pcm, 2300.0, 0.044, phase);
    appendTone(pcm, redDifferenceLine ? 1500.0 : 2300.0, 0.0045, phase);
    appendTone(pcm, 1900.0, 0.0015, phase);
    appendTone(pcm, redDifferenceLine ? 2100.0 : 1700.0, 0.044, phase);
}
}

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    VideoProcessor decoder;
    QStringList statuses;
    QImage latestFrame;
    int frameCount = 0;
    QObject::connect(&decoder, &VideoProcessor::statusChanged,
                     [&statuses](const QString &status) { statuses.append(status); });
    QObject::connect(&decoder, &VideoProcessor::frameReady,
                     [&latestFrame, &frameCount](const QImage &frame) {
                         latestFrame = frame;
                         ++frameCount;
                     });

    QByteArray pcm;
    double phase = 0.0;
    constexpr double ToneOffsetHz = 75.0;
    constexpr double TimingScale = 0.997;
    appendSilence(pcm, 1.2037);
    appendVis(pcm, 44, phase, ToneOffsetHz);
    for (int line = 0; line < 6; ++line) {
        appendMartinM1Line(pcm, phase, ToneOffsetHz, TimingScale);
        if (line == 1) {
            appendSilence(pcm, 0.180);
        }
    }
    appendSilence(pcm, 3.0);

    decoder.configureSstv(true);
    const int chunkBytes = SampleRate / 50 * static_cast<int>(sizeof(qint16));
    for (int offset = 0; offset < pcm.size(); offset += chunkBytes) {
        decoder.processSstvPcmFrame(pcm.mid(offset, (std::min)(chunkBytes, pcm.size() - offset)),
                                    SampleRate);
    }

    bool martinDetected = false;
    for (const QString &status : statuses) {
        if (status.contains(QStringLiteral("VIS 44")) &&
            status.contains(QStringLiteral("Martin M1"))) {
            martinDetected = true;
            break;
        }
    }
    qInfo().noquote() << statuses.join(QStringLiteral("\n"));
    qInfo() << "Frame" << latestFrame.size();
    if (!martinDetected) return 1;
    if (latestFrame.size() != QSize(320, 256)) return 2;
    if (frameCount < 2) return 3;
    if (latestFrame.pixel(0, 0) == qRgb(8, 10, 12)) return 4;
    bool timedOutAfterTransmission = false;
    for (const QString &status : statuses) {
        timedOutAfterTransmission = timedOutAfterTransmission ||
                                    status.contains(QStringLiteral("sync timeout"));
    }
    if (!timedOutAfterTransmission) return 5;

    VideoProcessor scottieDecoder;
    QImage scottieFrame;
    QObject::connect(&scottieDecoder, &VideoProcessor::frameReady,
                     [&scottieFrame](const QImage &frame) { scottieFrame = frame; });
    QByteArray scottiePcm;
    phase = 0.0;
    appendVis(scottiePcm, 60, phase, 0.0);
    appendScottieStart(scottiePcm, phase, 0.0);
    for (int line = 0; line < 4; ++line) {
        appendScottieRegularSegment(scottiePcm, phase, 0.0);
    }
    appendSilence(scottiePcm, 0.5);
    scottieDecoder.configureSstv(true);
    for (int offset = 0; offset < scottiePcm.size(); offset += chunkBytes) {
        scottieDecoder.processSstvPcmFrame(
            scottiePcm.mid(offset, (std::min)(chunkBytes, scottiePcm.size() - offset)),
            SampleRate);
    }
    if (scottieFrame.size() != QSize(320, 256)) return 6;
    const QRgb scottiePixel = scottieFrame.pixel(160, 0);
    if (!(qRed(scottiePixel) > qBlue(scottiePixel) + 30 &&
          qBlue(scottiePixel) > qGreen(scottiePixel) + 30)) {
        qWarning() << "Unexpected Scottie channel order" << QColor(scottiePixel);
        return 7;
    }


    VideoProcessor robotDecoder;
    QImage robotFrame;
    QObject::connect(&robotDecoder, &VideoProcessor::frameReady,
                     [&robotFrame](const QImage &frame) { robotFrame = frame; });
    QByteArray robotPcm;
    phase = 0.0;
    appendVis(robotPcm, 8, phase, 0.0);
    for (int line = 0; line < 6; ++line) {
        appendRobot36Line(robotPcm, phase, (line % 2) == 0);
    }
    appendSilence(robotPcm, 0.5);
    robotDecoder.configureSstv(true);
    for (int offset = 0; offset < robotPcm.size(); offset += chunkBytes) {
        robotDecoder.processSstvPcmFrame(
            robotPcm.mid(offset, (std::min)(chunkBytes, robotPcm.size() - offset)),
            SampleRate);
    }
    if (robotFrame.size() != QSize(320, 240)) return 8;
    const QRgb robotPixel = robotFrame.pixel(160, 0);
    if (qGray(robotFrame.pixel(40, 0)) >= qGray(robotFrame.pixel(280, 0)) - 80) {
        qWarning() << "Unexpected Robot36 luma decode" << QColor(robotPixel);
        return 9;
    }
    int firstTransition = -1;
    int lastTransition = -1;
    for (int y = 0; y < 4; ++y) {
        int transition = -1;
        for (int x = 1; x < 320; ++x) {
            if (qGray(robotFrame.pixel(x, y)) > 150) {
                transition = x;
                break;
            }
        }
        if (transition < 0) return 10;
        firstTransition = firstTransition < 0 ? transition : (std::min)(firstTransition, transition);
        lastTransition = (std::max)(lastTransition, transition);
    }
    if (lastTransition - firstTransition > 3) {
        std::fprintf(stderr, "Robot36 line jitter %d..%d\n", firstTransition, lastTransition);
        qWarning() << "Robot36 line jitter" << firstTransition << lastTransition;
        return 11;
    }

    VideoProcessor noiseDecoder;
    int falseFrames = 0;
    QObject::connect(&noiseDecoder, &VideoProcessor::frameReady,
                     [&falseFrames](const QImage &) { ++falseFrames; });
    QByteArray noisePcm;
    quint32 randomState = 0x12345678u;
    for (int sample = 0; sample < SampleRate * 3; ++sample) {
        randomState = randomState * 1664525u + 1013904223u;
        const qint16 value = static_cast<qint16>((randomState >> 16) - 32768);
        noisePcm.append(static_cast<char>(value & 0xff));
        noisePcm.append(static_cast<char>((value >> 8) & 0xff));
    }
    noiseDecoder.configureSstv(true);
    for (int offset = 0; offset < noisePcm.size(); offset += chunkBytes) {
        noiseDecoder.processSstvPcmFrame(
            noisePcm.mid(offset, (std::min)(chunkBytes, noisePcm.size() - offset)),
            SampleRate);
    }
    if (falseFrames != 0) return 12;
    return 0;
}

#ifndef HACKRFBACKEND_H
#define HACKRFBACKEND_H

#include <QString>
#include <QVector>

#include <cstdint>

struct HackRfDeviceInfo {
    int nativeIndex = 0;
    QString label;
};

struct HackRfTransfer {
    void *device = nullptr;
    unsigned char *buffer = nullptr;
    int bufferLength = 0;
    int validLength = 0;
    void *rxContext = nullptr;
    void *txContext = nullptr;
};

using HackRfRxCallback = int (*)(HackRfTransfer *transfer);

bool hackRfLibraryAvailable(QString *loadedPath = nullptr, QString *errorMessage = nullptr);
QString hackRfLastErrorMessage();
QVector<HackRfDeviceInfo> enumerateHackRfDevices();

int openHackRfDeviceSafely(void **dev, int nativeIndex);
int closeHackRfDeviceSafely(void *dev);
int setHackRfCenterFrequencySafely(void *dev, std::uint64_t frequencyHz);
int setHackRfSampleRateSafely(void *dev, double sampleRateHz);
int setHackRfBandwidthSafely(void *dev, std::uint32_t bandwidthHz);
std::uint32_t recommendedHackRfBandwidth(std::uint32_t sampleRateHz);
int setHackRfLnaGainSafely(void *dev, std::uint32_t gainDb);
int setHackRfVgaGainSafely(void *dev, std::uint32_t gainDb);
int setHackRfAmpEnabledSafely(void *dev, bool enabled);
int setHackRfBiasTeeEnabledSafely(void *dev, bool enabled);
int startHackRfRxSafely(void *dev, HackRfRxCallback callback, void *context);
int stopHackRfRxSafely(void *dev);
bool isHackRfStreamingSafely(void *dev);

#endif // HACKRFBACKEND_H

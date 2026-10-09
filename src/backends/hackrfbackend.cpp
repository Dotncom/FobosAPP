#include "hackrfbackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

#include <algorithm>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

constexpr int HACKRF_SUCCESS = 0;
constexpr int HACKRF_ERR_NOT_LOADED = -16001;
constexpr int HACKRF_ERR_NOT_OPEN = -16002;
constexpr int HACKRF_ERR_UNSUPPORTED_INDEX = -16003;

struct HackRfNativeDeviceList {
    char **serialNumbers = nullptr;
    int *usbBoardIds = nullptr;
    int *usbDeviceIndex = nullptr;
    int deviceCount = 0;
    void **usbDevices = nullptr;
    int usbDeviceCount = 0;
};

struct HackRfApi {
    QLibrary library;
    QString loadedPath;
    QString lastError;
    bool initialized = false;

    int (*init)() = nullptr;
    int (*open)(void **device) = nullptr;
    HackRfNativeDeviceList *(*device_list)() = nullptr;
    int (*device_list_open)(HackRfNativeDeviceList *list, int index, void **device) = nullptr;
    void (*device_list_free)(HackRfNativeDeviceList *list) = nullptr;
    int (*close)(void *device) = nullptr;
    const char *(*error_name)(int status) = nullptr;
    int (*set_freq)(void *device, std::uint64_t frequencyHz) = nullptr;
    int (*set_sample_rate)(void *device, double sampleRateHz) = nullptr;
    int (*set_baseband_filter_bandwidth)(void *device, std::uint32_t bandwidthHz) = nullptr;
    std::uint32_t (*compute_baseband_filter_bw_round_down_lt)(std::uint32_t bandwidthHz) = nullptr;
    int (*set_lna_gain)(void *device, std::uint32_t gainDb) = nullptr;
    int (*set_vga_gain)(void *device, std::uint32_t gainDb) = nullptr;
    int (*set_amp_enable)(void *device, std::uint8_t enabled) = nullptr;
    int (*set_antenna_enable)(void *device, std::uint8_t enabled) = nullptr;
    int (*start_rx)(void *device, HackRfRxCallback callback, void *context) = nullptr;
    int (*stop_rx)(void *device) = nullptr;
    int (*is_streaming)(void *device) = nullptr;
};

QMutex &apiMutex() {
    static QMutex mutex;
    return mutex;
}

HackRfApi &api() {
    static HackRfApi instance;
    return instance;
}

QStringList libraryCandidates() {
    const QString appDir = QCoreApplication::applicationDirPath();
    return {
#ifdef _WIN32
        QDir(appDir).absoluteFilePath(QStringLiteral("hackrf/hackrf.dll")),
        QDir(appDir).absoluteFilePath(QStringLiteral("hackrf/libhackrf.dll")),
        QDir(appDir).absoluteFilePath(QStringLiteral("hackrf.dll")),
        QDir(appDir).absoluteFilePath(QStringLiteral("libhackrf.dll")),
        QStringLiteral("hackrf.dll"),
        QStringLiteral("libhackrf.dll"),
#else
        QDir(appDir).absoluteFilePath(QStringLiteral("hackrf/libhackrf.so")),
        QDir(appDir).absoluteFilePath(QStringLiteral("libhackrf.so")),
        QStringLiteral("hackrf"),
        QStringLiteral("libhackrf"),
#endif
        QStringLiteral("hackrf"),
        QStringLiteral("libhackrf")
    };
}

template <typename T>
bool resolveSymbol(HackRfApi &hackrf, T &target, const char *name, bool required = true) {
    target = reinterpret_cast<T>(hackrf.library.resolve(name));
    if (!target && required) {
        hackrf.lastError = QStringLiteral("Missing libhackrf symbol: %1").arg(QString::fromLatin1(name));
        return false;
    }
    return true;
}

QString statusToStringLocked(HackRfApi &hackrf, int status) {
    if (hackrf.error_name) {
        const char *message = hackrf.error_name(status);
        if (message && *message) {
            return QString::fromLatin1(message);
        }
    }
    return QStringLiteral("HackRF status %1").arg(status);
}

bool ensureLoadedLocked() {
    HackRfApi &hackrf = api();
    if (hackrf.library.isLoaded()) {
        return true;
    }

    QStringList loadErrors;
    for (const QString &candidate : libraryCandidates()) {
        hackrf.library.setFileName(candidate);
#ifdef _WIN32
        const QFileInfo candidateInfo(candidate);
        const bool hasExplicitDirectory = candidateInfo.isAbsolute() && candidateInfo.dir().exists();
        if (hasExplicitDirectory) {
            SetDllDirectoryW(reinterpret_cast<LPCWSTR>(candidateInfo.dir().absolutePath().utf16()));
        }
#endif
        if (!hackrf.library.load()) {
            hackrf.lastError = hackrf.library.errorString();
            loadErrors << QStringLiteral("%1: %2").arg(candidate, hackrf.lastError);
#ifdef _WIN32
            if (hasExplicitDirectory) {
                SetDllDirectoryW(nullptr);
            }
#endif
            continue;
        }
#ifdef _WIN32
        if (hasExplicitDirectory) {
            SetDllDirectoryW(nullptr);
        }
#endif

        hackrf.loadedPath = candidate;
        const bool ok =
            resolveSymbol(hackrf, hackrf.init, "hackrf_init") &&
            resolveSymbol(hackrf, hackrf.open, "hackrf_open") &&
            resolveSymbol(hackrf, hackrf.close, "hackrf_close") &&
            resolveSymbol(hackrf, hackrf.set_freq, "hackrf_set_freq") &&
            resolveSymbol(hackrf, hackrf.set_sample_rate, "hackrf_set_sample_rate") &&
            resolveSymbol(hackrf, hackrf.set_baseband_filter_bandwidth,
                          "hackrf_set_baseband_filter_bandwidth") &&
            resolveSymbol(hackrf, hackrf.set_lna_gain, "hackrf_set_lna_gain") &&
            resolveSymbol(hackrf, hackrf.set_vga_gain, "hackrf_set_vga_gain") &&
            resolveSymbol(hackrf, hackrf.set_amp_enable, "hackrf_set_amp_enable") &&
            resolveSymbol(hackrf, hackrf.start_rx, "hackrf_start_rx") &&
            resolveSymbol(hackrf, hackrf.stop_rx, "hackrf_stop_rx") &&
            resolveSymbol(hackrf, hackrf.is_streaming, "hackrf_is_streaming");
        resolveSymbol(hackrf, hackrf.error_name, "hackrf_error_name", false);
        resolveSymbol(hackrf, hackrf.device_list, "hackrf_device_list", false);
        resolveSymbol(hackrf, hackrf.device_list_open, "hackrf_device_list_open", false);
        resolveSymbol(hackrf, hackrf.device_list_free, "hackrf_device_list_free", false);
        resolveSymbol(hackrf, hackrf.compute_baseband_filter_bw_round_down_lt,
                      "hackrf_compute_baseband_filter_bw_round_down_lt", false);
        resolveSymbol(hackrf, hackrf.set_antenna_enable, "hackrf_set_antenna_enable", false);

        if (!ok) {
            loadErrors << QStringLiteral("%1: %2").arg(candidate, hackrf.lastError);
            hackrf.library.unload();
            continue;
        }
        return true;
    }

    hackrf.lastError = loadErrors.isEmpty()
                           ? QStringLiteral("libhackrf runtime was not found")
                           : loadErrors.join(QStringLiteral(" | "));
    return false;
}

bool ensureInitializedLocked() {
    HackRfApi &hackrf = api();
    if (!ensureLoadedLocked()) {
        return false;
    }
    if (hackrf.initialized) {
        return true;
    }
    const int result = hackrf.init();
    if (result != HACKRF_SUCCESS) {
        hackrf.lastError = statusToStringLocked(hackrf, result);
        return false;
    }
    hackrf.initialized = true;
    return true;
}

template <typename Callable>
int withApi(Callable callable) {
    QMutexLocker locker(&apiMutex());
    if (!ensureInitializedLocked()) {
        return HACKRF_ERR_NOT_LOADED;
    }
    return callable(api());
}

} // namespace

bool hackRfLibraryAvailable(QString *loadedPath, QString *errorMessage) {
    QMutexLocker locker(&apiMutex());
    const bool ok = ensureInitializedLocked();
    const HackRfApi &hackrf = api();
    if (loadedPath) {
        *loadedPath = hackrf.loadedPath;
    }
    if (errorMessage) {
        *errorMessage = ok ? QString() : hackrf.lastError;
    }
    return ok;
}

QString hackRfLastErrorMessage() {
    QMutexLocker locker(&apiMutex());
    return api().lastError;
}

QVector<HackRfDeviceInfo> enumerateHackRfDevices(HackRfRuntimeStatus *status) {
    QVector<HackRfDeviceInfo> devices;
    if (status) {
        *status = HackRfRuntimeStatus{};
    }
    const int result = withApi([&devices, status](HackRfApi &hackrf) {
        if (status) {
            status->libraryAvailable = true;
            status->loadedPath = hackrf.loadedPath;
            status->modernDeviceListAvailable =
                hackrf.device_list && hackrf.device_list_open && hackrf.device_list_free;
        }
        if (hackrf.device_list && hackrf.device_list_open && hackrf.device_list_free) {
            HackRfNativeDeviceList *list = hackrf.device_list();
            if (!list) {
                hackrf.lastError = QStringLiteral("hackrf_device_list returned null");
                if (status) {
                    status->errorMessage = hackrf.lastError;
                }
                return HACKRF_SUCCESS;
            }
            const int count = (std::max)(0, list->deviceCount);
            if (status) {
                status->deviceCount = count;
            }
            devices.reserve(count);
            for (int index = 0; index < count; ++index) {
                HackRfDeviceInfo info;
                info.nativeIndex = index;
                if (list->serialNumbers && list->serialNumbers[index]) {
                    info.serial = QString::fromLatin1(list->serialNumbers[index]).trimmed();
                }
                info.label = info.serial.isEmpty()
                                 ? QStringLiteral("HackRF native #%1").arg(index + 1)
                                 : QStringLiteral("HackRF native #%1 (%2)")
                                       .arg(index + 1)
                                       .arg(info.serial);
                devices.append(info);
            }
            hackrf.device_list_free(list);
            return HACKRF_SUCCESS;
        }

        HackRfDeviceInfo info;
        info.nativeIndex = 0;
        info.label = QStringLiteral("HackRF native auto (legacy libhackrf)");
        devices.append(info);
        if (status) {
            status->deviceCount = 1;
        }
        return HACKRF_SUCCESS;
    });
    if (status && result == HACKRF_ERR_NOT_LOADED) {
        status->errorMessage = hackRfLastErrorMessage();
    }
    return devices;
}

int openHackRfDeviceSafely(void **dev, int nativeIndex) {
    if (!dev) {
        return HACKRF_ERR_NOT_OPEN;
    }
    *dev = nullptr;
    if (nativeIndex < 0) {
        return HACKRF_ERR_UNSUPPORTED_INDEX;
    }
    return withApi([dev, nativeIndex](HackRfApi &hackrf) {
        int result = HACKRF_ERR_UNSUPPORTED_INDEX;
        if (hackrf.device_list && hackrf.device_list_open && hackrf.device_list_free) {
            HackRfNativeDeviceList *list = hackrf.device_list();
            if (!list) {
                hackrf.lastError = QStringLiteral("hackrf_device_list returned null");
                return HACKRF_ERR_NOT_OPEN;
            }
            if (nativeIndex < list->deviceCount) {
                result = hackrf.device_list_open(list, nativeIndex, dev);
            }
            hackrf.device_list_free(list);
        } else if (nativeIndex == 0) {
            result = hackrf.open(dev);
        }
        if (result != HACKRF_SUCCESS) {
            hackrf.lastError = statusToStringLocked(hackrf, result);
        }
        return result;
    });
}

int closeHackRfDeviceSafely(void *dev) {
    if (!dev) {
        return HACKRF_ERR_NOT_OPEN;
    }
    return withApi([dev](HackRfApi &hackrf) {
        const int result = hackrf.close(dev);
        if (result != HACKRF_SUCCESS) {
            hackrf.lastError = statusToStringLocked(hackrf, result);
        }
        return result;
    });
}

int setHackRfCenterFrequencySafely(void *dev, std::uint64_t frequencyHz) {
    if (!dev) {
        return HACKRF_ERR_NOT_OPEN;
    }
    return withApi([dev, frequencyHz](HackRfApi &hackrf) {
        const int result = hackrf.set_freq(dev, frequencyHz);
        if (result != HACKRF_SUCCESS) {
            hackrf.lastError = statusToStringLocked(hackrf, result);
        }
        return result;
    });
}

int setHackRfSampleRateSafely(void *dev, double sampleRateHz) {
    if (!dev) {
        return HACKRF_ERR_NOT_OPEN;
    }
    return withApi([dev, sampleRateHz](HackRfApi &hackrf) {
        const int result = hackrf.set_sample_rate(dev, sampleRateHz);
        if (result != HACKRF_SUCCESS) {
            hackrf.lastError = statusToStringLocked(hackrf, result);
        }
        return result;
    });
}

std::uint32_t recommendedHackRfBandwidth(std::uint32_t sampleRateHz) {
    const std::uint32_t requested = static_cast<std::uint32_t>(
        (std::max)(1750000.0, static_cast<double>(sampleRateHz) * 0.75));
    QMutexLocker locker(&apiMutex());
    if (ensureInitializedLocked() && api().compute_baseband_filter_bw_round_down_lt) {
        return api().compute_baseband_filter_bw_round_down_lt(requested);
    }
    return requested;
}

int setHackRfBandwidthSafely(void *dev, std::uint32_t bandwidthHz) {
    if (!dev) {
        return HACKRF_ERR_NOT_OPEN;
    }
    return withApi([dev, bandwidthHz](HackRfApi &hackrf) {
        const int result = hackrf.set_baseband_filter_bandwidth(dev, bandwidthHz);
        if (result != HACKRF_SUCCESS) {
            hackrf.lastError = statusToStringLocked(hackrf, result);
        }
        return result;
    });
}

int setHackRfLnaGainSafely(void *dev, std::uint32_t gainDb) {
    if (!dev) {
        return HACKRF_ERR_NOT_OPEN;
    }
    const std::uint32_t value = (std::min)(40u, (gainDb / 8u) * 8u);
    return withApi([dev, value](HackRfApi &hackrf) {
        const int result = hackrf.set_lna_gain(dev, value);
        if (result != HACKRF_SUCCESS) {
            hackrf.lastError = statusToStringLocked(hackrf, result);
        }
        return result;
    });
}

int setHackRfVgaGainSafely(void *dev, std::uint32_t gainDb) {
    if (!dev) {
        return HACKRF_ERR_NOT_OPEN;
    }
    const std::uint32_t value = (std::min)(62u, (gainDb / 2u) * 2u);
    return withApi([dev, value](HackRfApi &hackrf) {
        const int result = hackrf.set_vga_gain(dev, value);
        if (result != HACKRF_SUCCESS) {
            hackrf.lastError = statusToStringLocked(hackrf, result);
        }
        return result;
    });
}

int setHackRfAmpEnabledSafely(void *dev, bool enabled) {
    if (!dev) {
        return HACKRF_ERR_NOT_OPEN;
    }
    return withApi([dev, enabled](HackRfApi &hackrf) {
        const int result = hackrf.set_amp_enable(dev, enabled ? 1u : 0u);
        if (result != HACKRF_SUCCESS) {
            hackrf.lastError = statusToStringLocked(hackrf, result);
        }
        return result;
    });
}

int setHackRfBiasTeeEnabledSafely(void *dev, bool enabled) {
    if (!dev) {
        return HACKRF_ERR_NOT_OPEN;
    }
    return withApi([dev, enabled](HackRfApi &hackrf) {
        if (!hackrf.set_antenna_enable) {
            return enabled ? HACKRF_ERR_NOT_LOADED : HACKRF_SUCCESS;
        }
        const int result = hackrf.set_antenna_enable(dev, enabled ? 1u : 0u);
        if (result != HACKRF_SUCCESS) {
            hackrf.lastError = statusToStringLocked(hackrf, result);
        }
        return result;
    });
}

int startHackRfRxSafely(void *dev, HackRfRxCallback callback, void *context) {
    if (!dev || !callback) {
        return HACKRF_ERR_NOT_OPEN;
    }
    return withApi([dev, callback, context](HackRfApi &hackrf) {
        const int result = hackrf.start_rx(dev, callback, context);
        if (result != HACKRF_SUCCESS) {
            hackrf.lastError = statusToStringLocked(hackrf, result);
        }
        return result;
    });
}

int stopHackRfRxSafely(void *dev) {
    if (!dev) {
        return HACKRF_ERR_NOT_OPEN;
    }
    return withApi([dev](HackRfApi &hackrf) {
        const int result = hackrf.stop_rx(dev);
        if (result != HACKRF_SUCCESS) {
            hackrf.lastError = statusToStringLocked(hackrf, result);
        }
        return result;
    });
}

bool isHackRfStreamingSafely(void *dev) {
    if (!dev) {
        return false;
    }
    return withApi([dev](HackRfApi &hackrf) {
        return hackrf.is_streaming(dev);
    }) == 1;
}

#include "audioprocessor.h"
#include <main.h>
#include <cmath>
#include <QDebug>
#include <QAudioDeviceInfo>
#include <QAudio>
#include <QAudioOutput>
#include <QThread>
#include <QUdpSocket>
#include <QByteArray>
#include <QByteArray>
#include <QHostAddress>
#include <vector>
#include <algorithm>
#include <iostream>
#include <fstream>

#define DEFAULT_BUFS_COUNT 32 
extern int DEFAULT_BUF_LEN;
extern double globalFrequency;
extern float* iqData;
extern double listeningFrequency;
double audioSamplerate = 16000.0;
extern double globalBandwidth;
extern double globalSampleRate;
extern int globalModulationType;
int currentModulationType = 0;
extern int globalMode;
extern int fftLength;
extern double minFrequency;
extern double maxFrequency;
extern std::vector<float> fftMagnitudes;
extern std::vector<float> fftFrequencies;
 

AudioProcessor::AudioProcessor(QObject *parent)
    : QObject(parent), running(false), workerThread(nullptr), audioOutput(nullptr), audioIODevice(nullptr) {
	//	udpSocket = new QUdpSocket(this);

	}

AudioProcessor::~AudioProcessor() {
    stopDemodulation();
}

void AudioProcessor::setAudioDevice(const QString &deviceName) {
    QMutexLocker locker(&mutex);
    audioDeviceName = deviceName;

    if (audioOutput) {
        audioOutput->stop();
        delete audioOutput;
        audioOutput = nullptr;
    }

    QAudioDeviceInfo deviceInfo = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput).at(0);
    for (const QAudioDeviceInfo &device : QAudioDeviceInfo::availableDevices(QAudio::AudioOutput)) {
        if (device.deviceName() == deviceName) {
            deviceInfo = device;
            break;
        }
    }

    QAudioFormat format;
    format.setSampleRate(audioSamplerate);
    format.setChannelCount(1);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);

    if (!deviceInfo.isFormatSupported(format)) {
        qWarning() << "Raw audio format not supported by backend, cannot play audio.";
        return;
    }

    audioOutput = new QAudioOutput(deviceInfo, format, this);
    audioIODevice = audioOutput->start();
}

void AudioProcessor::startDemodulation() {
    QMutexLocker locker(&mutex);
    if (!running) {
        running = true;
        workerThread = QThread::create([this] { processAudioData(); });
        connect(workerThread, &QThread::finished, workerThread, &QThread::deleteLater);
        workerThread->start();
        qDebug() << "Demodulation started.";
    }
}

void AudioProcessor::stopDemodulation() {
    QMutexLocker locker(&mutex);
    qDebug() << "Attempting to stop FM Demodulation.";
    if (running) {
        running = false;
        condition.wakeAll();
    }
    if (workerThread) {
        workerThread->requestInterruption();
        if (!workerThread->wait(5000)) { // Timeout for waiting
            qDebug() << "Error: Worker thread did not quit within timeout. Terminating...";
            workerThread->terminate(); // Forcefully terminate the thread
            workerThread->wait(); // Ensure thread is terminated
        } else {
            qDebug() << "Worker thread successfully quit.";
        }
        delete workerThread;
        workerThread = nullptr;
    }
    qDebug() << "Demodulation stopped.";
}

void AudioProcessor::processAudioData() {
    QUdpSocket udpSocket;
    constexpr int MAX_UDP_PACKET_SIZE = 65507;
	        std::vector<float> filteredData;
        std::vector<float> demodulatedData;
        std::vector<float> lowPassFilteredData;
		float lastPhase = 0.0f;
    while (!QThread::currentThread()->isInterruptionRequested()) {
        QMutexLocker locker(&mutex);
        if (!running) break;
        filteredData = filterIQData(iqData, globalFrequency, globalSampleRate, listeningFrequency, globalBandwidth);
        //switch (globalModulationType) {
         //   case 0: // AM
                demodulatedData = demodulateAM(filteredData);
         //       break;
          //  case 1: // FM
          //      demodulatedData = demodulateFM(filteredData, lastPhase);
          //      break;
          //  case 2: // SSB
          //      demodulatedData = demodulateSSB(filteredData, listeningFrequency, globalBandwidth, audioSamplerate);
          //      break;
          //  case 9: // FSK
          //      demodulatedData = demodulateFSK(filteredData, listeningFrequency, globalBandwidth, audioSamplerate);
          //      break;
           // default:
          //      qWarning() << "Unknown modulation type.";
           //     break;
        //}
        applyLowPassFilter(demodulatedData, lowPassFilteredData, 8000, globalSampleRate);
        //applyDeemphasisFilter(lowPassFilteredData, audioSamplerate);
        //resampleToAudioRate(lowPassFilteredData, globalBandwidth, audioSamplerate);
        if (audioIODevice) {
            QByteArray byteArray(reinterpret_cast<const char*>(lowPassFilteredData.data()), lowPassFilteredData.size() * sizeof(float));
            audioIODevice->write(byteArray);
            //udpSocket.writeDatagram(byteArray, QHostAddress("192.168.88.255"), 2109);
        }

        locker.unlock();
        //QThread::msleep(10);
    }
}

std::vector<float> AudioProcessor::filterIQData(float* iqData, double centerFrequency, double globalSampleRate, double listeningFrequency, double globalBandwidth) {
    std::vector<float> filteredData;
    double minFreq = listeningFrequency - globalBandwidth / 2;
    double maxFreq = listeningFrequency + globalBandwidth / 2;
    double freqStep = globalSampleRate / DEFAULT_BUF_LEN;
    for (size_t i = 0; i < DEFAULT_BUF_LEN; i += 2) { 
		if (globalMode == 0 || globalMode == 1){
        double freq = (centerFrequency - globalSampleRate / 2) + (i / 2) * freqStep;
        if (freq >= minFreq && freq <= maxFreq) {
            filteredData.push_back(iqData[i]);      // I
            filteredData.push_back(iqData[i + 1]);  // Q
        }
	} else {
		double freq = centerFrequency + (i / 2) * freqStep;
        if (freq >= minFreq && freq <= maxFreq) {
            filteredData.push_back(iqData[i]);      // I
            filteredData.push_back(iqData[i + 1]);  // Q
        }
	}
    }
    return filteredData;
}

inline float fastAtan2(float y, float x) {
    float abs_y = fabsf(y);
    float r, angle;
    if (x == 0.0f && y == 0.0f) { return 0.0f; }
    if (x >= 0.0f) {
        r = (x - abs_y) / (x + abs_y);
        angle = M_PI_4 - M_PI_4 * r; // M_PI_4 ??? p/4
    } else {
        r = (x + abs_y) / (abs_y - x);
        angle = 3 * M_PI_4 - M_PI_4 * r;
    }
    return y < 0.0f ? -angle : angle;
}

std::vector<float> AudioProcessor::demodulateFM(const std::vector<float>& filteredData, float& lastPhase) {
    std::vector<float> demodulatedData(filteredData.size() / 2);
    for (size_t i = 0; i < demodulatedData.size(); ++i) {
        float I = filteredData[2 * i];
        float Q = filteredData[2 * i + 1];
        float phase = fastAtan2(Q, I);
        float deltaPhase = phase - lastPhase;
        if (deltaPhase > M_PI) {
            deltaPhase -= 2 * M_PI;
        } else if (deltaPhase < -M_PI) {
            deltaPhase += 2 * M_PI;
        }
        lastPhase = phase;
        demodulatedData[i] = deltaPhase;
    }
    return demodulatedData;
}

 
 
void AudioProcessor::applyLowPassFilter(const std::vector<float>& demodulatedData, std::vector<float>& lowPassFilteredData, float cutoffFreq, float sampleRate) {
    const int filterOrder = 100;
    std::vector<float> filterCoeffs(filterOrder);
    float normCutoff = cutoffFreq / sampleRate;
    for (int i = 0; i < filterOrder; ++i) {
        if (i == filterOrder / 2) {
            filterCoeffs[i] = 2 * normCutoff;
        } else {
            float sinc = std::sin(2 * M_PI * normCutoff * (i - filterOrder / 2)) / (M_PI * (i - filterOrder / 2));
            filterCoeffs[i] = sinc * (0.54 - 0.46 * std::cos(2 * M_PI * i / (filterOrder - 1))); 
        }
    }
    lowPassFilteredData.resize(demodulatedData.size());
    for (size_t i = 0; i < demodulatedData.size(); ++i) {
        lowPassFilteredData[i] = 0;
        for (int j = 0; j < filterOrder; ++j) {
            if (i >= j) {
                lowPassFilteredData[i] += demodulatedData[i - j] * filterCoeffs[j];
            }
        }
    }
}


void AudioProcessor::applyDeemphasisFilter(std::vector<float>& lowPassFilteredData, float sampleRate) {
    const float deemphasisTau = 50e-6;
    float deemDt = 1.0f / sampleRate;
    float alpha = deemDt / (deemphasisTau + deemDt);
    float prevOutput = 0.0f;
    for (size_t i = 0; i < lowPassFilteredData.size(); ++i) {
        float input = lowPassFilteredData[i];
        float output = alpha * input + (1 - alpha) * prevOutput;
        lowPassFilteredData[i] = output;
        prevOutput = output;
    }
}

void AudioProcessor::resampleToAudioRate(std::vector<float>& lowPassFilteredData, double sourceRate, double targetRate) {
    if (lowPassFilteredData.empty() || sourceRate <= 0 || targetRate <= 0) {
        std::cerr << "Invalid parameters in resampleToAudioRate" << std::endl;
        return;
    }
    double ratio = targetRate / sourceRate;
    size_t newSize = static_cast<size_t>(lowPassFilteredData.size() * ratio);
    std::vector<float> resampledData(newSize);
    for (size_t i = 0; i < newSize; ++i) {
        double srcIndex = i / ratio;
        size_t index1 = static_cast<size_t>(srcIndex);
        size_t index2 = index1 + 1;
        if (index2 >= lowPassFilteredData.size()) {
            resampledData[i] = lowPassFilteredData[index1];
        } else {
            double frac = srcIndex - index1;
            resampledData[i] = (1 - frac) * lowPassFilteredData[index1] + frac * lowPassFilteredData[index2];
        }
    }
    lowPassFilteredData = std::move(resampledData);
}


 
std::vector<float> AudioProcessor::demodulateAM(const std::vector<float>& filteredData) {
    std::vector<float> demodulatedData(filteredData.size() / 2);
    for (size_t i = 0; i < demodulatedData.size(); ++i) {
        float I = filteredData[2 * i];
        float Q = filteredData[2 * i + 1];
        demodulatedData[i] = std::sqrt(I * I + Q * Q);
    }
    return demodulatedData;
}


std::vector<float> AudioProcessor::demodulateSSB(const std::vector<float>& iqData, double frequency, double globalBandwidth, double sampleRate) {
    std::vector<float> demodulatedData;
    const float PI = 3.14159265358979323846f;
    float prevPhase = 0.0f;
    for (size_t i = 0; i < iqData.size(); i += 2) {
        float I = iqData[i];
        float Q = iqData[i + 1];
        float phase = atan2(Q, I);
        float phaseDiff = phase - prevPhase;
        if (phaseDiff > PI) phaseDiff -= 2 * PI;
        if (phaseDiff < -PI) phaseDiff += 2 * PI;
        demodulatedData.push_back(phaseDiff * sampleRate / (2 * PI));
        prevPhase = phase;
    }
    return demodulatedData;
}

std::vector<float> AudioProcessor::demodulateFSK(const std::vector<float>& iqData, double frequency, double globalBandwidth, double sampleRate) {
    std::vector<float> demodulatedData;
    const float PI = 3.14159265358979323846f;
    float prevPhase = 0.0f;
    float prevFreq = 0.0f;
    float bandwidth = globalBandwidth / 2.0;
    for (size_t i = 0; i < iqData.size(); i += 2) {
        float I = iqData[i];
        float Q = iqData[i + 1]; 
        float phase = atan2(Q, I);
        float phaseDiff = phase - prevPhase;
        if (phaseDiff > PI) phaseDiff -= 2 * PI;
        if (phaseDiff < -PI) phaseDiff += 2 * PI;
        float freq = phaseDiff * sampleRate / (2 * PI);
        if (std::abs(freq - prevFreq) > bandwidth) {
            float bit = (freq > prevFreq) ? 1.0f : 0.0f;
            demodulatedData.push_back(bit);
        }
        prevPhase = phase;
        prevFreq = freq;
    }
    return demodulatedData;
}


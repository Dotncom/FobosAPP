#ifndef AUDIOPROCESSOR_H
#define AUDIOPROCESSOR_H

#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QThread>
#include <vector>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <QAudioDeviceInfo>
#include <QAudio>
#include <QAudioOutput>
#include <QUdpSocket>
#include <QByteArray>
#include <QHostAddress>
//#include <udp_sender.h>

extern double listeningFrequency;
extern float* iqData;
extern double audioSamplerate;
extern double globalSampleRate;
extern double globalBandwidth;
extern int globalModulationType;
extern int DEFAULT_BUF_LEN;
extern int globalMode;
extern double globalFrequency;
extern int fftLength;
extern double minFrequency;
extern double maxFrequency;

class AudioProcessor : public QObject {
    Q_OBJECT

public:
    explicit AudioProcessor(QObject *parent = nullptr);
    ~AudioProcessor();
    void setAudioDevice(const QString &deviceName);
// void setUdpSocket(QUdpSocket *socket);

public slots:
    void startDemodulation();
    void stopDemodulation();

private:
    void processAudioData();
    //void resampleToAudioRate(std::vector<float>& demodulatedData, double sourceRate, double targetRate);
    void resampleToAudioRate(std::vector<float>& lowPassFilteredData, double sourceRate, double targetRate);
    std::vector<float> filterIQData(float* iqData, double centerFrequency, double globalSampleRate, double listeningFrequency, double globalBandwidth);
    void applyLowPassFilter(const std::vector<float>& demodulatedData, std::vector<float>& lowPassFilteredData, float cutoffFreq, float sampleRate);
    void applyDeemphasisFilter(std::vector<float>& lowPassFilteredData, float sampleRate);
    std::vector<float> demodulateAM(const std::vector<float>& filteredData);
	std::vector<float> demodulateFM(const std::vector<float>& filteredData, float& lastPhase);
	std::vector<float> demodulateSSB(const std::vector<float>& lowPassFilteredData, double frequency, double globalBandwidth, double sampleRate);
	std::vector<float> demodulateFSK(const std::vector<float>& lowPassFilteredData, double frequency, double globalBandwidth, double sampleRate);
	QUdpSocket *udpSocket;  
    QString serverIp;       
    quint16 serverPort;    
	//UdpSender *udpSender;
	QByteArray byteArray;
    void initializeUdpSocket();
    void sendAudioData(const QByteArray &data);
	void handleSocketError(QAbstractSocket::SocketError socketError);	
    QString audioDeviceName;
    QThread *workerThread = nullptr;
    bool running;
    // float lastPhase;
    QMutex mutex;
    QWaitCondition condition;
    QAudioOutput *audioOutput = nullptr;
    QIODevice *audioIODevice = nullptr;
};

#endif // AUDIOPROCESSOR_H

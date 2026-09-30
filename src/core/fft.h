#ifndef FFT_H
#define FFT_H

#include <fftw3.h>
#include <vector>
#include <cmath>
#include <QVector>
#include <thread>
#include <mutex>
#include <complex>
#include <QObject>
#include <QThread>
#include <QWaitCondition>
#include <algorithm>
#include <QtConcurrent/QtConcurrent>
#include "iqbuffer.h"
#include "radiosettings.h"

extern int DEFAULT_BUF_LEN;

extern float* iqData;
extern int globalMode;
extern std::vector<float> fftMagnitudes;
extern std::vector<float> fftFrequencies;
extern int fftLength;
extern double currentScale;
extern double minFrequency;
extern double maxFrequency;
extern double globalFrequency;
extern double globalSampleRate;

class FFTResult : public QObject {
    Q_OBJECT
public:
    explicit FFTResult(bool optimizeLargePlans = false, QObject *parent = nullptr);
    ~FFTResult();
    bool storeFFTResults(const RadioSettings &settings,
                         std::vector<float> &outFrequencies,
                         std::vector<float> &outMagnitudes,
                         std::vector<float> *outReferenceMagnitudes = nullptr,
                         IqBuffer::BlockMetadata *outMetadata = nullptr);
    void storeFFTResults();
    void resetHfNoiseCancelState();
    std::mutex fftMutex;
	void performFFTInThread();
private:
    bool ensurePlan(int length);
    void ensureWindow(int length, int windowType);
    float windowCoefficient(int index) const;
    double windowAmplitudeSumForSamples(int sampleCount) const;
    void releasePlan();
    fftwf_complex *fftIn;
    fftwf_complex *fftOut;
    fftwf_plan plan;
    int planLength;
    int windowLength = 0;
    int windowType = FFT_WINDOW_RECTANGULAR;
    double windowAmplitudeSum = 0.0;
    std::vector<float> windowCoefficients;
    bool optimizeLargePlans;
    int profileLength = 0;
    int profileFrames = 0;
    qint64 profileSnapshotNs = 0;
    qint64 profileInputNs = 0;
    qint64 profileExecuteNs = 0;
    qint64 profileOutputNs = 0;
    std::vector<std::complex<float>> hfNoiseCancelBins;
    std::vector<std::complex<float>> hfNoiseCancelCrossPower;
    std::vector<float> hfNoiseCancelMainPower;
    std::vector<float> hfNoiseCancelRefPower;
    std::vector<float> iqSnapshotScratch;
};


#endif // FFT_H

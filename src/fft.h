#ifndef FFT_H
#define FFT_H

#include <vector>
#include <fftw3.h> 
//#include <main.h>
#include <cmath>
#include <QVector>
#include <thread>
#include <mutex>
#include <QObject>
#include <QThread>
#include <QWaitCondition>

extern int DEFAULT_BUF_LEN;

extern float* iqData;
extern int globalMode;
extern std::vector<float> fftMagnitudes;
extern std::vector<float> fftFrequencies;
extern int fftLength;
extern int currentScale;
extern double minFrequency;
extern double maxFrequency;

class FFTResult : public QObject {
    Q_OBJECT
public:
    explicit FFTResult(QObject *parent = nullptr);
    ~FFTResult();
    void storeFFTResults();
    

private:
    fftwf_complex *fftIn;
    fftwf_complex *fftOut;
    fftwf_plan plan;
};


#endif // FFT_H

#ifndef DATAPROCESSOR_H
#define DATAPROCESSOR_H

#include <fobos.h>
//#include "spectrumwindow.h"
//#include <fftw3.h> 
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QVector>
#include <cmath>






extern float* iqData;
extern fobos_dev_t *device;
extern int globalMode;
extern int actualBufLength;
//extern std::vector<float> fftMagnitudes;
//extern std::vector<float> fftFrequencies;
//extern int fftLength;
extern int DEFAULT_BUF_LEN;
//extern int currentScale;
//extern double minFrequency;
//extern double maxFrequency;

class DataProcessor : public QThread {
    Q_OBJECT
public:
    explicit DataProcessor(fobos_dev_t* dev, QObject *parent = nullptr);
    ~DataProcessor();
	void handleData(float *buf, uint32_t buf_length);
    void run() override;
    void stop();
	//void storeFFTResults();
	//void workerThreadFunction();
signals:
    void dataReady(); 

private:
	//fftwf_plan plan;
    //fftwf_complex* fftIn;
    //fftwf_complex* fftOut;
    bool running;
    bool isReading;  // ???????? ??? ??????????
    //std::mutex dataMutex;
    //std::condition_variable dataCondition;
    //std::thread workerThread;
};

#endif // DATAPROCESSOR_H

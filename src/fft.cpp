#include "fft.h"
#include <fftw3.h> 
#include <main.h>
#include <cmath>
#include <QVector>
#include <thread>
#include <mutex>
#include <QObject>
#include <QThread>
#include <QWaitCondition>
#include <vector>

extern int DEFAULT_BUF_LEN;

extern float* iqData;
extern int globalMode;
extern std::vector<float> fftMagnitudes;
extern std::vector<float> fftFrequencies;
extern int fftLength;
extern int currentScale;
extern double minFrequency;
extern double maxFrequency;


FFTResult::FFTResult(QObject *parent)
	: QObject(parent) {
	fftIn = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftLength);
    fftOut = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftLength);
    plan = fftwf_plan_dft_1d(fftLength, fftIn, fftOut, FFTW_FORWARD, FFTW_ESTIMATE);
}

FFTResult::~FFTResult() {
	    if (fftIn) {
        fftwf_free(fftIn);
        fftIn = nullptr;
    }
    
    if (fftOut) {
        fftwf_free(fftOut);
        fftOut = nullptr;
    }
    fftwf_destroy_plan(plan);
}


 void FFTResult::storeFFTResults() {
    fftMagnitudes.resize(fftLength);
    fftFrequencies.resize(fftLength);
    std::vector<float> magnitude(fftLength);
    
    if (globalMode == 0 || globalMode == 1) {		
        int startIdx = static_cast<int>((minFrequency - (globalFrequency - globalSampleRate / 2.0)) / (globalSampleRate / fftLength));
        int endIdx = static_cast<int>((maxFrequency - (globalFrequency - globalSampleRate / 2.0)) / (globalSampleRate / fftLength));
        startIdx = std::max(0, startIdx);
		endIdx = std::min(fftLength - 1, endIdx);
        for (int iq = 0; iq < fftLength; ++iq) {
            int dataIndex = startIdx + iq;
            if (dataIndex >= 0 && dataIndex <= endIdx) {
                fftIn[iq][0] = iqData[2 * dataIndex];     // I component
                fftIn[iq][1] = iqData[2 * dataIndex + 1]; // Q component
            } else {
                fftIn[iq][0] = 0.0f;
                fftIn[iq][1] = 0.0f;
            }
        }
        fftwf_execute(plan);
        for (int iqq = 0; iqq < fftLength; ++iqq) {
            magnitude[iqq] = std::sqrt(fftOut[iqq][0] * fftOut[iqq][0] + fftOut[iqq][1] * fftOut[iqq][1]);
            fftMagnitudes[iqq] = std::log(1 + magnitude[iqq]);
            fftFrequencies[iqq] = (iqq - fftLength / 2) * (globalSampleRate / fftLength) + globalFrequency;
        }
	 
    } else if (globalMode == 2 || globalMode == 3) {
        int startIdx = static_cast<int>((minFrequency  - globalFrequency) / (globalSampleRate / 2 / fftLength));
        int endIdx = static_cast<int>((maxFrequency - globalFrequency) / (globalSampleRate / 2 / fftLength));

           for (int ie = 0; ie < fftLength; ++ie) {
            if (globalMode == 2) {
                fftIn[ie][0] = (ie >= startIdx && ie <= endIdx) ? iqData[2 * ie] : 0.0f;
                fftIn[ie][1] = 0.0f;
            } else if (globalMode == 3) {
                fftIn[ie][0] = 0.0f;
                fftIn[ie][1] = (ie >= startIdx && ie <= endIdx) ? iqData[2 * ie + 1] : 0.0f;
            }
        }
        fftwf_execute(plan);
        for (int iee = 0; iee < fftLength; ++iee) {
            magnitude[iee] = std::sqrt(fftOut[iee][0] * fftOut[iee][0] + fftOut[iee][1] * fftOut[iee][1]);
            fftMagnitudes[iee] = std::log(1 + magnitude[iee]);
            fftFrequencies[iee] = (iee - fftLength / 2) * (globalSampleRate / fftLength) + globalFrequency;
        }
    }
}


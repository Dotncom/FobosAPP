#ifndef MAIN_H
#define MAIN_H

#include "fft.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QTimer>
#include <QComboBox>
#include <QPushButton>
#include <QLineEdit>
#include <QSlider>
#include <QLabel>
#include "dataprocessor.h"
#include "scalewidget.h"
#include <QScrollArea>
#include "MyGraphWidget.h"
#include "MyWaterfallWidget.h"
#include <QCheckBox>
#include <QMainWindow>
#include <fftw3.h> 
#include "audioprocessor.h"
#include <QAudioOutput>
#include <QAudioDeviceInfo>
#include <QAudio>
#include <QWheelEvent>
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QCloseEvent>
#include <QMessageBox>
#include <QVector>
 
extern fobos_dev_t *device;
extern float* iqData;

extern double globalFrequency; 
extern double listeningFrequency;
extern double globalSampleRate;
extern double globalBandwidth;
extern int globalModulationType;
extern int globalMode;
extern int fftLength;
extern int currentScale;
extern double minFrequency;
extern double maxFrequency;

class YourClassName : public QMainWindow {
    Q_OBJECT

public:
   explicit YourClassName(QWidget *parent = nullptr);
    ~YourClassName();
   
     std::unique_ptr<FFTResult> fftResult;
    void onFrequencyEntered();
    void onListeningFrequencyEntered();
    void onScaleChanged(int value);
    void onfftLengthEntered();
    void updateSpectrum();
    void onLnaGainChanged(int value);
    void onVgaGainChanged(int value);
private slots:
	void settingRange();
    void onDirectSamplingChanged(int index);
    void listFobosDevices();
    void startFobosProcessing();
    void stopFobosProcessing();
    void onSampleRateChanged(int index);
    void populateSampleRates();
    void onClkChanged(int index);
    void onCheckboxStateChanged(int state);
    void onAudioDeviceChanged(int index);
	void onModulationChanged(int id);
    void onBandwidthChanged();
    void updateFrequency(double newFrequency);
    void updateCentralFrequency(double newCentralFrequency);
    void doubleGraphEnable(bool checked);
protected:
	void onWaterfallScaleChanged(int delta);
    void wheelEvent(QWheelEvent *event) override;
        void closeEvent(QCloseEvent *event) override {
        int reply = QMessageBox::question(this, "Acception", 
                                          "Close this program?",
                                          QMessageBox::Yes | QMessageBox::No);
        
        if (reply == QMessageBox::Yes) { 
            event->accept();
        } else {
            event->ignore();
        }
};

private:
    QWidget *centralWidget;
    QStringList getFobosDevices();
    QComboBox *clkBox;
    QComboBox *comboBox;
    QComboBox *modeBox;
    QComboBox *sampleBox;
    QCheckBox *checkBoxes[8]; 
    QPushButton *refreshButton;
    QPushButton *fobosButton;
    QPushButton *startButton;
    QPushButton *stopButton;
    QCheckBox *spectrumCheckbox;
    QCheckBox *audioCheckbox;
    QCheckBox *graphCheckbox;
    MyGraphWidget *graphWidget = nullptr;
    MyWaterfallWidget *waterfallWidget = nullptr;
    ScaleWidget *scaleWidget;
    QSlider *lnaGainSlider;
    QSlider *vgaGainSlider;
    QLabel *lnaGainLabel;
    QLabel *centralFrequencyLabel;
    QLabel *listeningFrequencyLabel;
    QLabel *fftLabel;
    QLineEdit *fftEdit;
    QLineEdit *frequencyLineEdit;
    QLineEdit *listeningFrequencyLineEdit;
    QLineEdit *bandwidthLineEdit;
    QLabel *vgaGainLabel;
    QLabel *scaleLabel;
    QSlider *scaleSlider;
    QTimer *updateTimer;
    QVBoxLayout *layout;
    QHBoxLayout *hboxLayout;
    bool deviceOpened;
    double samplingFrequency;
    DataProcessor *processor;
    
    //FFTResult *fftResult;
    //AudioProcessor *audioProcessor = nullptr;
    QComboBox *audioDeviceComboBox;
    AudioProcessor *audioProcessor;
    void populateAudioDevices();
    int minScale = 1;  
    int maxScale = 100;
};

#endif // MAIN_H

#ifndef TRANSMITDIALOG_H
#define TRANSMITDIALOG_H

#include "transmitterbackend.h"
#include "hackrftransmitterbackend.h"
#include "transmitwaveformgenerator.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QImage>
#include <QVector>

#include <complex>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;
class QToolButton;
class QIODevice;
class TxWaveformPreviewWidget;

#ifdef FOBOSAPP_HAS_QT_MULTIMEDIA
class QAudioInput;
#endif

class TransmitDialog final : public QDialog {
public:
    explicit TransmitDialog(const QString &language, QWidget *parent = nullptr);
    ~TransmitDialog() override;

    void setLanguage(const QString &language);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QString text(const QString &english, const QString &ukrainian) const;
    TxConfiguration configurationFromUi() const;
    TxModulation selectedModulation() const;
    double signalBandwidthFromUi() const;
    bool validateSignalBandwidth(const TxConfiguration &configuration);
    bool writeActiveIq(const std::complex<float> *samples, int count, QString *error);
    void applyLiveSignalShape();
    QString signalShapeSummary(const TxConfiguration &configuration) const;
    void buildUi();
    void populateAudioInputs();
    void populateHackRfDevices();
    void updateModeUi();
    void updateTexts();
    void loadSettings();
    void saveSettings() const;
    void startTransmission();
    void stopTransmission(const QString &reason = QString());
    void startTextSimulation(const TxConfiguration &configuration);
    void startMicrophoneSimulation(TxConfiguration configuration);
    void startManualCw(TxConfiguration configuration);
    void startAudioFile(TxConfiguration configuration);
    void startImageTransmission(TxConfiguration configuration);
    void readMicrophone();
    void processLiveSource();
    void setLiveKey(bool pressed);
    void chooseMediaFile();
    void editMediaImage();
    void updateProgress();
    void exportIq();
    void setStatus(const QString &message, bool error = false);
    bool hackRfSelected() const;
    TransmitterBackend *selectedBackend();

    QString language;
    SimulatorTransmitterBackend simulator;
    HackRfTransmitterBackend hackRf;
    TransmitterBackend *activeBackend = nullptr;
    TxConfiguration activeConfiguration;
    TxModulatorState modulatorState;
    TxBandwidthLimiterState bandwidthLimiterState;
    QVector<std::complex<float>> bandwidthLimitedIq;

    QComboBox *backendCombo = nullptr;
    QComboBox *deviceCombo = nullptr;
    QComboBox *modulationCombo = nullptr;
    QComboBox *sourceCombo = nullptr;
    QComboBox *mediaModeCombo = nullptr;
    QComboBox *audioInputCombo = nullptr;
    QToolButton *audioRefreshButton = nullptr;
    QComboBox *sampleRateCombo = nullptr;
    QComboBox *deviceSampleRateCombo = nullptr;
    QComboBox *bandwidthCombo = nullptr;
    QComboBox *signalBandwidthCombo = nullptr;
    QDoubleSpinBox *frequencySpin = nullptr;
    QSpinBox *levelSpin = nullptr;
    QSpinBox *txGainSpin = nullptr;
    QSpinBox *watchdogSpin = nullptr;
    QCheckBox *rfAmpCheck = nullptr;
    QCheckBox *armCheck = nullptr;
    QDoubleSpinBox *deviationSpin = nullptr;
    QDoubleSpinBox *toneSpin = nullptr;
    QSpinBox *cwWpmSpin = nullptr;
    QCheckBox *ft8UtcCheck = nullptr;
    QPlainTextEdit *messageEdit = nullptr;
    QPushButton *startButton = nullptr;
    QPushButton *stopButton = nullptr;
    QPushButton *exportButton = nullptr;
    QPushButton *mediaBrowseButton = nullptr;
    QPushButton *mediaEditButton = nullptr;
    QPushButton *mediaPlayButton = nullptr;
    QPushButton *mediaPauseButton = nullptr;
    QPushButton *mediaStopButton = nullptr;
    QPushButton *liveKeyButton = nullptr;
    QLabel *rfLockLabel = nullptr;
    QLabel *backendLabel = nullptr;
    QLabel *rfLabel = nullptr;
    QLabel *deviceLabel = nullptr;
    QLabel *basebandLabel = nullptr;
    QLabel *deviceRateLabel = nullptr;
    QLabel *bandwidthLabel = nullptr;
    QLabel *levelLabel = nullptr;
    QLabel *txGainLabel = nullptr;
    QLabel *watchdogLabel = nullptr;
    QLabel *modeLabel = nullptr;
    QLabel *sourceLabel = nullptr;
    QLabel *mediaPathLabel = nullptr;
    QLabel *mediaModeLabel = nullptr;
    QLabel *audioInputLabel = nullptr;
    QLabel *deviationLabel = nullptr;
    QLabel *signalBandwidthLabel = nullptr;
    QLabel *toneLabel = nullptr;
    QLabel *cwSpeedLabel = nullptr;
    QLabel *statusLabel = nullptr;
    QLabel *statsLabel = nullptr;
    QProgressBar *progressBar = nullptr;
    TxWaveformPreviewWidget *preview = nullptr;
    QTimer *progressTimer = nullptr;
    QTimer *sourceTimer = nullptr;
    QElapsedTimer elapsedTimer;
    double plannedDurationSeconds = 0.0;
    bool textSimulation = false;
    bool pttMode = false;
    bool liveKeyPressed = false;
    bool mediaPaused = false;
    QString mediaPath;
    QImage preparedMediaImage;
    QVector<float> mediaAudio;
    QVector<std::complex<float>> atvFrame;
    int mediaPosition = 0;
    double manualCwPhase = 0.0;

#ifdef FOBOSAPP_HAS_QT_MULTIMEDIA
    QAudioInput *audioInput = nullptr;
    QIODevice *audioDevice = nullptr;
#endif
};

#endif // TRANSMITDIALOG_H

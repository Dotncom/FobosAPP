#ifndef TRANSMITDIALOG_H
#define TRANSMITDIALOG_H

#include "transmitterbackend.h"
#include "transmitwaveformgenerator.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QVector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;
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

private:
    QString text(const QString &english, const QString &ukrainian) const;
    TxConfiguration configurationFromUi() const;
    TxModulation selectedModulation() const;
    void buildUi();
    void populateAudioInputs();
    void updateModeUi();
    void updateTexts();
    void loadSettings();
    void saveSettings() const;
    void startTransmission();
    void stopTransmission(const QString &reason = QString());
    void startTextSimulation(const TxConfiguration &configuration);
    void startMicrophoneSimulation(TxConfiguration configuration);
    void readMicrophone();
    void updateProgress();
    void exportIq();
    void setStatus(const QString &message, bool error = false);

    QString language;
    SimulatorTransmitterBackend simulator;
    TxModulatorState modulatorState;

    QComboBox *backendCombo = nullptr;
    QComboBox *modulationCombo = nullptr;
    QComboBox *sourceCombo = nullptr;
    QComboBox *audioInputCombo = nullptr;
    QComboBox *sampleRateCombo = nullptr;
    QDoubleSpinBox *frequencySpin = nullptr;
    QSpinBox *levelSpin = nullptr;
    QDoubleSpinBox *deviationSpin = nullptr;
    QDoubleSpinBox *toneSpin = nullptr;
    QSpinBox *cwWpmSpin = nullptr;
    QCheckBox *ft8UtcCheck = nullptr;
    QPlainTextEdit *messageEdit = nullptr;
    QPushButton *startButton = nullptr;
    QPushButton *stopButton = nullptr;
    QPushButton *exportButton = nullptr;
    QLabel *rfLockLabel = nullptr;
    QLabel *backendLabel = nullptr;
    QLabel *rfLabel = nullptr;
    QLabel *basebandLabel = nullptr;
    QLabel *levelLabel = nullptr;
    QLabel *modeLabel = nullptr;
    QLabel *sourceLabel = nullptr;
    QLabel *audioInputLabel = nullptr;
    QLabel *deviationLabel = nullptr;
    QLabel *toneLabel = nullptr;
    QLabel *cwSpeedLabel = nullptr;
    QLabel *statusLabel = nullptr;
    QLabel *statsLabel = nullptr;
    QProgressBar *progressBar = nullptr;
    TxWaveformPreviewWidget *preview = nullptr;
    QTimer *progressTimer = nullptr;
    QElapsedTimer elapsedTimer;
    double plannedDurationSeconds = 0.0;
    bool textSimulation = false;

#ifdef FOBOSAPP_HAS_QT_MULTIMEDIA
    QAudioInput *audioInput = nullptr;
    QIODevice *audioDevice = nullptr;
#endif
};

#endif // TRANSMITDIALOG_H

#include "transmitdialog.h"

#include "appsettingsutils.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QTimer>
#include <QVBoxLayout>

#ifdef FOBOSAPP_HAS_QT_MULTIMEDIA
#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QAudioInput>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

class TxWaveformPreviewWidget final : public QWidget {
public:
    explicit TxWaveformPreviewWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(170);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setIq(const QVector<std::complex<float>> &samples) {
        points.clear();
        if (!samples.isEmpty()) {
            const int target = (std::min)(samples.size(), 1800);
            points.reserve(target);
            for (int x = 0; x < target; ++x) {
                const int begin = static_cast<int>((static_cast<qint64>(x) * samples.size()) / target);
                const int end = (std::max)(begin + 1,
                    static_cast<int>((static_cast<qint64>(x + 1) * samples.size()) / target));
                float peak = 0.0f;
                for (int i = begin; i < (std::min)(end, samples.size()); ++i) {
                    const float value = samples[i].real();
                    if (std::abs(value) > std::abs(peak)) peak = value;
                }
                points.append(peak);
            }
        }
        update();
    }

    void clear() {
        points.clear();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(15, 18, 22));
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QColor(55, 63, 72));
        painter.drawLine(0, height() / 2, width(), height() / 2);
        for (int x = 0; x < width(); x += (std::max)(1, width() / 10)) {
            painter.drawLine(x, 0, x, height());
        }
        if (points.size() < 2) return;
        QPolygonF path;
        path.reserve(points.size());
        const double half = height() * 0.46;
        for (int i = 0; i < points.size(); ++i) {
            const double x = static_cast<double>(i) * (width() - 1) /
                             static_cast<double>(points.size() - 1);
            const double y = height() * 0.5 - (std::clamp)(points[i], -1.0f, 1.0f) * half;
            path.append(QPointF(x, y));
        }
        painter.setPen(QPen(QColor(42, 210, 154), 1.2));
        painter.drawPolyline(path);
    }

private:
    QVector<float> points;
};

TransmitDialog::TransmitDialog(const QString &language, QWidget *parent)
    : QDialog(parent), language(language) {
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(760, 650);
    buildUi();
    populateAudioInputs();
    loadSettings();
    updateTexts();
    updateModeUi();
}

TransmitDialog::~TransmitDialog() {
    stopTransmission();
}

QString TransmitDialog::text(const QString &english, const QString &ukrainian) const {
    return language.toLower().startsWith(QStringLiteral("uk")) ? ukrainian : english;
}

void TransmitDialog::buildUi() {
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    QGroupBox *hardwareBox = new QGroupBox(this);
    hardwareBox->setObjectName(QStringLiteral("txHardwareBox"));
    QGridLayout *hardware = new QGridLayout(hardwareBox);
    backendCombo = new QComboBox(hardwareBox);
    backendCombo->addItem(QStringLiteral("Simulator / IQ file"), QStringLiteral("simulator-iq-file"));
    backendCombo->addItem(QStringLiteral("HackRF native (RF locked)"), QStringLiteral("hackrf-native-locked"));
    if (auto *model = qobject_cast<QStandardItemModel *>(backendCombo->model())) {
        if (QStandardItem *item = model->item(1)) item->setEnabled(false);
    }
    frequencySpin = new QDoubleSpinBox(hardwareBox);
    frequencySpin->setRange(1.0, 7250.0);
    frequencySpin->setDecimals(6);
    frequencySpin->setSingleStep(0.0125);
    frequencySpin->setSuffix(QStringLiteral(" MHz"));
    sampleRateCombo = new QComboBox(hardwareBox);
    sampleRateCombo->setEditable(true);
    for (int rate : {12000, 24000, 48000, 96000, 192000, 1000000, 2000000}) {
        sampleRateCombo->addItem(QString::number(rate), rate);
    }
    levelSpin = new QSpinBox(hardwareBox);
    levelSpin->setRange(1, 90);
    levelSpin->setSuffix(QStringLiteral(" %"));
    rfLockLabel = new QLabel(hardwareBox);
    rfLockLabel->setWordWrap(true);
    rfLockLabel->setStyleSheet(QStringLiteral("color: #d0a43b; font-weight: 600;"));
    backendLabel = new QLabel(hardwareBox);
    rfLabel = new QLabel(hardwareBox);
    basebandLabel = new QLabel(hardwareBox);
    levelLabel = new QLabel(hardwareBox);
    hardware->addWidget(backendLabel, 0, 0);
    hardware->addWidget(backendCombo, 0, 1);
    hardware->addWidget(rfLabel, 0, 2);
    hardware->addWidget(frequencySpin, 0, 3);
    hardware->addWidget(basebandLabel, 1, 0);
    hardware->addWidget(sampleRateCombo, 1, 1);
    hardware->addWidget(levelLabel, 1, 2);
    hardware->addWidget(levelSpin, 1, 3);
    hardware->addWidget(rfLockLabel, 2, 0, 1, 4);
    hardware->setColumnStretch(1, 1);
    hardware->setColumnStretch(3, 1);

    QGroupBox *signalBox = new QGroupBox(this);
    signalBox->setObjectName(QStringLiteral("txSignalBox"));
    QGridLayout *signal = new QGridLayout(signalBox);
    modulationCombo = new QComboBox(signalBox);
    const QVector<TxModulation> modes = {
        TxModulation::Am, TxModulation::Nfm, TxModulation::Wfm, TxModulation::Dsb,
        TxModulation::Usb, TxModulation::Lsb, TxModulation::Cw, TxModulation::Ft8
    };
    for (TxModulation mode : modes) {
        modulationCombo->addItem(TransmitWaveformGenerator::modulationName(mode), static_cast<int>(mode));
    }
    sourceCombo = new QComboBox(signalBox);
    sourceCombo->addItem(QStringLiteral("Microphone"), QStringLiteral("microphone"));
    sourceCombo->addItem(QStringLiteral("Text"), QStringLiteral("text"));
    audioInputCombo = new QComboBox(signalBox);
    deviationSpin = new QDoubleSpinBox(signalBox);
    deviationSpin->setRange(100.0, 100000.0);
    deviationSpin->setDecimals(0);
    deviationSpin->setSingleStep(100.0);
    deviationSpin->setSuffix(QStringLiteral(" Hz"));
    toneSpin = new QDoubleSpinBox(signalBox);
    toneSpin->setRange(50.0, 5000.0);
    toneSpin->setDecimals(1);
    toneSpin->setSingleStep(10.0);
    toneSpin->setSuffix(QStringLiteral(" Hz"));
    cwWpmSpin = new QSpinBox(signalBox);
    cwWpmSpin->setRange(5, 60);
    cwWpmSpin->setSuffix(QStringLiteral(" WPM"));
    ft8UtcCheck = new QCheckBox(signalBox);
    ft8UtcCheck->setChecked(true);
    modeLabel = new QLabel(signalBox);
    sourceLabel = new QLabel(signalBox);
    audioInputLabel = new QLabel(signalBox);
    deviationLabel = new QLabel(signalBox);
    toneLabel = new QLabel(signalBox);
    cwSpeedLabel = new QLabel(signalBox);
    signal->addWidget(modeLabel, 0, 0);
    signal->addWidget(modulationCombo, 0, 1);
    signal->addWidget(sourceLabel, 0, 2);
    signal->addWidget(sourceCombo, 0, 3);
    signal->addWidget(audioInputLabel, 1, 0);
    signal->addWidget(audioInputCombo, 1, 1, 1, 3);
    signal->addWidget(deviationLabel, 2, 0);
    signal->addWidget(deviationSpin, 2, 1);
    signal->addWidget(toneLabel, 2, 2);
    signal->addWidget(toneSpin, 2, 3);
    signal->addWidget(cwSpeedLabel, 3, 0);
    signal->addWidget(cwWpmSpin, 3, 1);
    signal->addWidget(ft8UtcCheck, 3, 2, 1, 2);
    signal->setColumnStretch(1, 1);
    signal->setColumnStretch(3, 1);

    QGroupBox *messageBox = new QGroupBox(this);
    messageBox->setObjectName(QStringLiteral("txMessageBox"));
    QVBoxLayout *messageLayout = new QVBoxLayout(messageBox);
    messageEdit = new QPlainTextEdit(messageBox);
    messageEdit->setPlaceholderText(QStringLiteral("CQ TEST KN34 / HELLO WORLD"));
    messageEdit->setMaximumBlockCount(20);
    messageLayout->addWidget(messageEdit);

    preview = new TxWaveformPreviewWidget(this);
    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 1000);
    progressBar->setValue(0);
    statusLabel = new QLabel(this);
    statusLabel->setWordWrap(true);
    statsLabel = new QLabel(this);
    statsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QHBoxLayout *actions = new QHBoxLayout();
    startButton = new QPushButton(this);
    stopButton = new QPushButton(this);
    exportButton = new QPushButton(this);
    stopButton->setEnabled(false);
    exportButton->setEnabled(false);
    actions->addWidget(startButton);
    actions->addWidget(stopButton);
    actions->addStretch(1);
    actions->addWidget(exportButton);

    root->addWidget(hardwareBox);
    root->addWidget(signalBox);
    root->addWidget(messageBox);
    root->addWidget(preview, 1);
    root->addWidget(progressBar);
    root->addWidget(statusLabel);
    root->addWidget(statsLabel);
    root->addLayout(actions);

    progressTimer = new QTimer(this);
    progressTimer->setInterval(40);
    connect(progressTimer, &QTimer::timeout, this, [this]() { updateProgress(); });
    connect(modulationCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this]() { updateModeUi(); });
    connect(startButton, &QPushButton::clicked, this, [this]() { startTransmission(); });
    connect(stopButton, &QPushButton::clicked, this, [this]() {
        stopTransmission(text(QStringLiteral("Stopped by user"), QString::fromUtf8(u8"Зупинено користувачем")));
    });
    connect(exportButton, &QPushButton::clicked, this, [this]() { exportIq(); });
}

void TransmitDialog::populateAudioInputs() {
    audioInputCombo->clear();
#ifdef FOBOSAPP_HAS_QT_MULTIMEDIA
    const QList<QAudioDeviceInfo> devices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    for (const QAudioDeviceInfo &device : devices) {
        audioInputCombo->addItem(device.deviceName());
    }
    if (audioInputCombo->count() == 0) {
        audioInputCombo->addItem(QStringLiteral("No microphone devices"));
        audioInputCombo->setEnabled(false);
    }
#else
    audioInputCombo->addItem(QStringLiteral("Qt Multimedia is unavailable"));
    audioInputCombo->setEnabled(false);
#endif
}

void TransmitDialog::setLanguage(const QString &nextLanguage) {
    language = nextLanguage;
    updateTexts();
    updateModeUi();
}

void TransmitDialog::updateTexts() {
    setWindowTitle(text(QStringLiteral("Transmitter laboratory"), QString::fromUtf8(u8"Лабораторія передавача")));
    if (auto *box = findChild<QGroupBox *>(QStringLiteral("txHardwareBox")))
        box->setTitle(text(QStringLiteral("Output and safety"), QString::fromUtf8(u8"Вихід і безпека")));
    if (auto *box = findChild<QGroupBox *>(QStringLiteral("txSignalBox")))
        box->setTitle(text(QStringLiteral("Signal"), QString::fromUtf8(u8"Сигнал")));
    if (auto *box = findChild<QGroupBox *>(QStringLiteral("txMessageBox")))
        box->setTitle(text(QStringLiteral("Text message"), QString::fromUtf8(u8"Текстове повідомлення")));
    backendLabel->setText(text(QStringLiteral("Backend:"), QString::fromUtf8(u8"Backend:")));
    rfLabel->setText(QStringLiteral("RF:"));
    basebandLabel->setText(text(QStringLiteral("Baseband:"), QString::fromUtf8(u8"Основна смуга:")));
    levelLabel->setText(text(QStringLiteral("Level:"), QString::fromUtf8(u8"Рівень:")));
    modeLabel->setText(text(QStringLiteral("Mode:"), QString::fromUtf8(u8"Режим:")));
    sourceLabel->setText(text(QStringLiteral("Source:"), QString::fromUtf8(u8"Джерело:")));
    audioInputLabel->setText(text(QStringLiteral("Audio input:"), QString::fromUtf8(u8"Аудіовхід:")));
    deviationLabel->setText(text(QStringLiteral("Deviation:"), QString::fromUtf8(u8"Девіація:")));
    toneLabel->setText(text(QStringLiteral("Tone:"), QString::fromUtf8(u8"Тон:")));
    cwSpeedLabel->setText(text(QStringLiteral("CW speed:"), QString::fromUtf8(u8"Швидкість CW:")));
    backendCombo->setItemText(0, text(QStringLiteral("Simulator / IQ file"),
                                      QString::fromUtf8(u8"Симулятор / файл IQ")));
    backendCombo->setItemText(1, text(QStringLiteral("HackRF native (RF locked)"),
                                      QString::fromUtf8(u8"HackRF native (RF заблоковано)")));
    sourceCombo->setItemText(0, text(QStringLiteral("Microphone"), QString::fromUtf8(u8"Мікрофон")));
    sourceCombo->setItemText(1, text(QStringLiteral("Text"), QString::fromUtf8(u8"Текст")));
    startButton->setText(text(QStringLiteral("Start simulation"), QString::fromUtf8(u8"Почати симуляцію")));
    stopButton->setText(text(QStringLiteral("Stop"), QString::fromUtf8(u8"Зупинити")));
    exportButton->setText(text(QStringLiteral("Export IQ..."), QString::fromUtf8(u8"Експорт IQ...")));
    ft8UtcCheck->setText(text(QStringLiteral("Align FT8 to UTC 15 s slot"),
                              QString::fromUtf8(u8"Прив'язати FT8 до 15-секундного UTC-слота")));
    rfLockLabel->setText(text(
        QStringLiteral("RF output is physically disabled. This window currently generates and validates baseband IQ only."),
        QString::fromUtf8(u8"RF-вихід фізично вимкнений. Зараз це вікно лише генерує та перевіряє baseband IQ.")));
    if (!simulator.isRunning()) {
        setStatus(text(QStringLiteral("Ready; no RF can be emitted"),
                       QString::fromUtf8(u8"Готово; випромінювання RF неможливе")));
    }
}

TxModulation TransmitDialog::selectedModulation() const {
    return static_cast<TxModulation>(modulationCombo->currentData().toInt());
}

TxConfiguration TransmitDialog::configurationFromUi() const {
    TxConfiguration configuration;
    configuration.modulation = selectedModulation();
    configuration.frequencyHz = frequencySpin->value() * 1e6;
    configuration.sampleRate = sampleRateCombo->currentData().toInt();
    if (configuration.sampleRate <= 0) configuration.sampleRate = sampleRateCombo->currentText().toInt();
    configuration.sampleRate = (std::clamp)(configuration.sampleRate, 8000, 20000000);
    configuration.level = static_cast<float>(levelSpin->value()) / 100.0f;
    configuration.deviationHz = deviationSpin->value();
    configuration.toneHz = toneSpin->value();
    configuration.cwWpm = cwWpmSpin->value();
    configuration.alignFt8ToUtcSlot = ft8UtcCheck->isChecked();
    return configuration;
}

void TransmitDialog::updateModeUi() {
    const TxModulation modulation = selectedModulation();
    const bool textMode = TransmitWaveformGenerator::isTextMode(modulation);
    sourceCombo->setCurrentIndex(textMode ? 1 : 0);
    sourceCombo->setEnabled(false);
    messageEdit->setEnabled(textMode);
    audioInputCombo->setEnabled(!textMode && audioInputCombo->count() > 0 &&
                                !audioInputCombo->itemText(0).contains(QStringLiteral("unavailable"), Qt::CaseInsensitive) &&
                                !audioInputCombo->itemText(0).contains(QStringLiteral("No microphone"), Qt::CaseInsensitive));
    deviationSpin->setEnabled(modulation == TxModulation::Nfm || modulation == TxModulation::Wfm);
    toneSpin->setEnabled(textMode);
    cwWpmSpin->setEnabled(modulation == TxModulation::Cw);
    ft8UtcCheck->setEnabled(modulation == TxModulation::Ft8);
}

void TransmitDialog::startTransmission() {
    if (simulator.isRunning()) return;
    saveSettings();
    preview->clear();
    progressBar->setValue(0);
    statsLabel->clear();
    modulatorState = TxModulatorState{};
    const TxConfiguration configuration = configurationFromUi();
    if (TransmitWaveformGenerator::isTextMode(configuration.modulation)) {
        startTextSimulation(configuration);
    } else {
        startMicrophoneSimulation(configuration);
    }
}

void TransmitDialog::startTextSimulation(const TxConfiguration &configuration) {
    const TxGenerationResult generated =
        TransmitWaveformGenerator::generateText(configuration, messageEdit->toPlainText());
    if (!generated.ok()) {
        setStatus(generated.error, true);
        return;
    }
    QString error;
    if (!simulator.start(configuration, &error) ||
        !simulator.writeIq(generated.iq.constData(), generated.iq.size(), &error)) {
        simulator.stop();
        setStatus(error, true);
        return;
    }
    preview->setIq(generated.iq);
    plannedDurationSeconds = generated.durationSeconds;
    textSimulation = true;
    elapsedTimer.restart();
    progressTimer->start();
    startButton->setEnabled(false);
    stopButton->setEnabled(true);
    exportButton->setEnabled(false);
    statsLabel->setText(generated.summary);
    QString slotNote;
    if (configuration.modulation == TxModulation::Ft8 && configuration.alignFt8ToUtcSlot) {
        slotNote = text(QStringLiteral(" UTC slot alignment is stored for the future RF backend; simulator starts now."),
                        QString::fromUtf8(u8" Прив'язку до UTC-слота збережено для майбутнього RF backend; симулятор стартує одразу."));
    }
    setStatus(text(QStringLiteral("Text baseband simulation is running."),
                   QString::fromUtf8(u8"Симуляція текстового baseband запущена.")) + slotNote);
}

void TransmitDialog::startMicrophoneSimulation(TxConfiguration configuration) {
#ifdef FOBOSAPP_HAS_QT_MULTIMEDIA
    const QList<QAudioDeviceInfo> devices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    QAudioDeviceInfo selected = QAudioDeviceInfo::defaultInputDevice();
    for (const QAudioDeviceInfo &device : devices) {
        if (device.deviceName() == audioInputCombo->currentText()) {
            selected = device;
            break;
        }
    }
    QAudioFormat format;
    format.setCodec(QStringLiteral("audio/pcm"));
    format.setSampleRate(configuration.sampleRate);
    format.setChannelCount(1);
    format.setSampleSize(16);
    format.setSampleType(QAudioFormat::SignedInt);
    format.setByteOrder(QAudioFormat::LittleEndian);
    if (!selected.isFormatSupported(format)) {
        format = selected.nearestFormat(format);
    }
    if (format.channelCount() != 1 || format.sampleSize() != 16 ||
        format.sampleType() != QAudioFormat::SignedInt) {
        setStatus(text(QStringLiteral("The selected microphone does not provide supported mono 16-bit PCM"),
                       QString::fromUtf8(u8"Обраний мікрофон не підтримує потрібний mono 16-bit PCM")), true);
        return;
    }
    configuration.sampleRate = format.sampleRate();
    QString error;
    if (!simulator.start(configuration, &error)) {
        setStatus(error, true);
        return;
    }
    audioInput = new QAudioInput(selected, format, this);
    audioInput->setBufferSize((std::max)(4096, format.sampleRate() / 5 * 2));
    audioDevice = audioInput->start();
    if (!audioDevice) {
        simulator.stop();
        audioInput->deleteLater();
        audioInput = nullptr;
        setStatus(text(QStringLiteral("Could not start microphone capture"),
                       QString::fromUtf8(u8"Не вдалося запустити захоплення мікрофона")), true);
        return;
    }
    connect(audioDevice, &QIODevice::readyRead, this, [this]() { readMicrophone(); });
    plannedDurationSeconds = 180.0;
    textSimulation = false;
    elapsedTimer.restart();
    progressTimer->start();
    startButton->setEnabled(false);
    stopButton->setEnabled(true);
    exportButton->setEnabled(false);
    setStatus(text(QStringLiteral("Microphone baseband simulation is running; RF remains disabled."),
                   QString::fromUtf8(u8"Симуляція baseband з мікрофона працює; RF залишається вимкненим.")));
#else
    Q_UNUSED(configuration)
    setStatus(text(QStringLiteral("This build has no Qt Multimedia microphone support"),
                   QString::fromUtf8(u8"Ця збірка не має підтримки мікрофона Qt Multimedia")), true);
#endif
}

void TransmitDialog::readMicrophone() {
#ifdef FOBOSAPP_HAS_QT_MULTIMEDIA
    if (!audioDevice || !simulator.isRunning()) return;
    QByteArray bytes = audioDevice->readAll();
    const int samples = bytes.size() / static_cast<int>(sizeof(qint16));
    if (samples <= 0) return;
    QVector<float> audio(samples);
    const auto *pcm = reinterpret_cast<const qint16 *>(bytes.constData());
    for (int i = 0; i < samples; ++i) {
        audio[i] = static_cast<float>(pcm[i]) / 32768.0f;
    }
    const QVector<std::complex<float>> iq =
        TransmitWaveformGenerator::modulateAudio(simulator.configuration(), audio, &modulatorState);
    QString error;
    if (!iq.isEmpty() && !simulator.writeIq(iq.constData(), iq.size(), &error)) {
        stopTransmission(error);
        return;
    }
    preview->setIq(iq);
    statsLabel->setText(text(QStringLiteral("Captured %1 IQ samples (%2 s)"),
                             QString::fromUtf8(u8"Захоплено %1 IQ-семплів (%2 с)"))
                            .arg(simulator.capturedIq().size())
                            .arg(static_cast<double>(simulator.capturedIq().size()) /
                                     simulator.configuration().sampleRate,
                                 0, 'f', 2));
#endif
}

void TransmitDialog::updateProgress() {
    if (!simulator.isRunning()) {
        progressTimer->stop();
        return;
    }
    const double elapsed = elapsedTimer.elapsed() / 1000.0;
    const double denominator = (std::max)(0.001, plannedDurationSeconds);
    progressBar->setValue((std::min)(1000, qRound(1000.0 * elapsed / denominator)));
    if ((textSimulation && elapsed >= plannedDurationSeconds) || elapsed >= 180.0) {
        stopTransmission(textSimulation
                             ? text(QStringLiteral("Simulation complete"), QString::fromUtf8(u8"Симуляцію завершено"))
                             : text(QStringLiteral("180 second safety limit reached"),
                                    QString::fromUtf8(u8"Досягнуто безпечний ліміт 180 секунд")));
    }
}

void TransmitDialog::stopTransmission(const QString &reason) {
    progressTimer->stop();
#ifdef FOBOSAPP_HAS_QT_MULTIMEDIA
    if (audioInput) {
        audioInput->stop();
        audioInput->deleteLater();
        audioInput = nullptr;
        audioDevice = nullptr;
    }
#endif
    const bool hadSamples = !simulator.capturedIq().isEmpty();
    simulator.stop();
    startButton->setEnabled(true);
    stopButton->setEnabled(false);
    exportButton->setEnabled(hadSamples);
    if (!reason.isEmpty()) setStatus(reason);
}

void TransmitDialog::exportIq() {
    const QVector<std::complex<float>> &iq = simulator.capturedIq();
    if (iq.isEmpty()) return;
    QSettings settings(persistentSettingsFilePath(), QSettings::IniFormat);
    const QString initial = settings.value(QStringLiteral("transmit/lastExportDirectory"),
                                           QDir::homePath()).toString();
    const QString suggested = QStringLiteral("tx_%1_%2.cf32")
                                  .arg(TransmitWaveformGenerator::modulationName(simulator.configuration().modulation)
                                           .toLower().replace('/', '_').replace(' ', '_'))
                                  .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this,
        text(QStringLiteral("Export complex float IQ"), QString::fromUtf8(u8"Експорт комплексного float IQ")),
        QDir(initial).filePath(suggested),
        QStringLiteral("Complex float IQ (*.cf32)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setStatus(file.errorString(), true);
        return;
    }
    QByteArray buffer;
    buffer.resize(iq.size() * static_cast<int>(sizeof(float)) * 2);
    float *output = reinterpret_cast<float *>(buffer.data());
    for (int i = 0; i < iq.size(); ++i) {
        output[2 * i] = iq[i].real();
        output[2 * i + 1] = iq[i].imag();
    }
    if (file.write(buffer) != buffer.size() || !file.commit()) {
        setStatus(file.errorString(), true);
        return;
    }
    const QFileInfo info(path);
    settings.setValue(QStringLiteral("transmit/lastExportDirectory"), info.absolutePath());
    QJsonObject metadata;
    metadata.insert(QStringLiteral("format"), QStringLiteral("cf32_le"));
    metadata.insert(QStringLiteral("sample_rate"), simulator.configuration().sampleRate);
    metadata.insert(QStringLiteral("frequency_hz"), simulator.configuration().frequencyHz);
    metadata.insert(QStringLiteral("modulation"),
                    TransmitWaveformGenerator::modulationName(simulator.configuration().modulation));
    metadata.insert(QStringLiteral("sample_count"), iq.size());
    metadata.insert(QStringLiteral("duration_seconds"),
                    static_cast<double>(iq.size()) / simulator.configuration().sampleRate);
    metadata.insert(QStringLiteral("rf_output"), false);
    QSaveFile sidecar(info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".json"));
    if (sidecar.open(QIODevice::WriteOnly)) {
        sidecar.write(QJsonDocument(metadata).toJson(QJsonDocument::Indented));
        sidecar.commit();
    }
    setStatus(text(QStringLiteral("IQ exported: %1"), QString::fromUtf8(u8"IQ експортовано: %1")).arg(path));
}

void TransmitDialog::setStatus(const QString &message, bool error) {
    statusLabel->setText(message);
    statusLabel->setStyleSheet(error ? QStringLiteral("color: #d9534f;") : QString());
}

void TransmitDialog::loadSettings() {
    QSettings settings(persistentSettingsFilePath(), QSettings::IniFormat);
    frequencySpin->setValue(settings.value(QStringLiteral("transmit/frequencyMhz"), 144.300).toDouble());
    const int mode = settings.value(QStringLiteral("transmit/modulation"),
                                    static_cast<int>(TxModulation::Nfm)).toInt();
    const int modeIndex = modulationCombo->findData(mode);
    if (modeIndex >= 0) modulationCombo->setCurrentIndex(modeIndex);
    const int sampleRate = settings.value(QStringLiteral("transmit/sampleRate"), 48000).toInt();
    int rateIndex = sampleRateCombo->findData(sampleRate);
    if (rateIndex < 0) {
        sampleRateCombo->addItem(QString::number(sampleRate), sampleRate);
        rateIndex = sampleRateCombo->count() - 1;
    }
    sampleRateCombo->setCurrentIndex(rateIndex);
    levelSpin->setValue(settings.value(QStringLiteral("transmit/levelPercent"), 50).toInt());
    deviationSpin->setValue(settings.value(QStringLiteral("transmit/deviationHz"), 2500.0).toDouble());
    toneSpin->setValue(settings.value(QStringLiteral("transmit/toneHz"), 700.0).toDouble());
    cwWpmSpin->setValue(settings.value(QStringLiteral("transmit/cwWpm"), 18).toInt());
    ft8UtcCheck->setChecked(settings.value(QStringLiteral("transmit/alignFt8Utc"), true).toBool());
    messageEdit->setPlainText(settings.value(QStringLiteral("transmit/message"),
                                             QStringLiteral("CQ TEST KN34")).toString());
    restoreGeometry(settings.value(QStringLiteral("transmit/dialogGeometry")).toByteArray());
}

void TransmitDialog::saveSettings() const {
    QSettings settings(persistentSettingsFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("transmit/frequencyMhz"), frequencySpin->value());
    settings.setValue(QStringLiteral("transmit/modulation"), static_cast<int>(selectedModulation()));
    settings.setValue(QStringLiteral("transmit/sampleRate"), configurationFromUi().sampleRate);
    settings.setValue(QStringLiteral("transmit/levelPercent"), levelSpin->value());
    settings.setValue(QStringLiteral("transmit/deviationHz"), deviationSpin->value());
    settings.setValue(QStringLiteral("transmit/toneHz"), toneSpin->value());
    settings.setValue(QStringLiteral("transmit/cwWpm"), cwWpmSpin->value());
    settings.setValue(QStringLiteral("transmit/alignFt8Utc"), ft8UtcCheck->isChecked());
    settings.setValue(QStringLiteral("transmit/message"), messageEdit->toPlainText());
    settings.setValue(QStringLiteral("transmit/dialogGeometry"), saveGeometry());
}

void TransmitDialog::closeEvent(QCloseEvent *event) {
    stopTransmission();
    saveSettings();
    QDialog::closeEvent(event);
}

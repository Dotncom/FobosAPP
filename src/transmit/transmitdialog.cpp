#include "transmitdialog.h"

#include "appsettingsutils.h"
#include "hackrfbackend.h"
#include "sstvimageeditordialog.h"
#include "transmitmediagenerator.h"

#include <QApplication>
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
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QTimer>
#include <QToolButton>
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
    populateHackRfDevices();
    loadSettings();
    updateTexts();
    updateModeUi();
    QTimer::singleShot(750, this, [this]() {
        if (audioInputCombo->currentData().toString().isEmpty()) {
            populateAudioInputs();
            updateModeUi();
        }
    });
    qApp->installEventFilter(this);
}

TransmitDialog::~TransmitDialog() {
    qApp->removeEventFilter(this);
    stopTransmission();
}

bool TransmitDialog::eventFilter(QObject *watched, QEvent *event) {
    auto *watchedWidget = qobject_cast<QWidget *>(watched);
    const bool insideDialog = watchedWidget &&
                              (watchedWidget == this || isAncestorOf(watchedWidget));
    const QString source = sourceCombo->currentData().toString();
    const bool liveSource = source == QStringLiteral("manual-cw") ||
                            source == QStringLiteral("microphone-ptt");
    if (!insideDialog || !isVisible() || !liveSource) {
        return QDialog::eventFilter(watched, event);
    }
    if (event->type() == QEvent::WindowDeactivate && liveKeyPressed) {
        setLiveKey(false);
        return QDialog::eventFilter(watched, event);
    }
    auto isLiveHotkey = [](const QKeyEvent *key) {
        return key->key() == Qt::Key_F12;
    };
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (isLiveHotkey(key)) {
            key->accept();
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (isLiveHotkey(key)) {
            if (!key->isAutoRepeat() && activeBackend && activeBackend->isRunning()) {
                setLiveKey(true);
            }
            return true;
        }
    } else if (event->type() == QEvent::KeyRelease) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_F12) {
            if (!key->isAutoRepeat() && liveKeyPressed) setLiveKey(false);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
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
    backendCombo->addItem(QStringLiteral("HackRF native"), QStringLiteral("hackrf-native"));
    deviceCombo = new QComboBox(hardwareBox);
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
    deviceSampleRateCombo = new QComboBox(hardwareBox);
    for (int rate : {2000000, 4000000, 8000000, 10000000, 12000000, 16000000, 20000000}) {
        deviceSampleRateCombo->addItem(QStringLiteral("%1 Msps").arg(rate / 1000000), rate);
    }
    bandwidthCombo = new QComboBox(hardwareBox);
    bandwidthCombo->addItem(QStringLiteral("Auto"), 0);
    for (int bandwidth : {1750000, 2500000, 3500000, 5000000, 7000000, 10000000,
                          12000000, 14000000, 15000000, 20000000}) {
        bandwidthCombo->addItem(QStringLiteral("%1 MHz").arg(bandwidth / 1e6, 0, 'f', 2), bandwidth);
    }
    txGainSpin = new QSpinBox(hardwareBox);
    txGainSpin->setRange(0, 47);
    txGainSpin->setSuffix(QStringLiteral(" dB"));
    watchdogSpin = new QSpinBox(hardwareBox);
    watchdogSpin->setRange(1, 180);
    watchdogSpin->setValue(30);
    watchdogSpin->setSuffix(QStringLiteral(" s"));
    rfAmpCheck = new QCheckBox(hardwareBox);
    armCheck = new QCheckBox(hardwareBox);
    armCheck->setChecked(false);
    rfLockLabel = new QLabel(hardwareBox);
    rfLockLabel->setWordWrap(true);
    rfLockLabel->setStyleSheet(QStringLiteral("color: #d0a43b; font-weight: 600;"));
    backendLabel = new QLabel(hardwareBox);
    deviceLabel = new QLabel(hardwareBox);
    rfLabel = new QLabel(hardwareBox);
    basebandLabel = new QLabel(hardwareBox);
    deviceRateLabel = new QLabel(hardwareBox);
    bandwidthLabel = new QLabel(hardwareBox);
    levelLabel = new QLabel(hardwareBox);
    txGainLabel = new QLabel(hardwareBox);
    watchdogLabel = new QLabel(hardwareBox);
    hardware->addWidget(backendLabel, 0, 0);
    hardware->addWidget(backendCombo, 0, 1);
    hardware->addWidget(deviceLabel, 0, 2);
    hardware->addWidget(deviceCombo, 0, 3);
    hardware->addWidget(rfLabel, 1, 0);
    hardware->addWidget(frequencySpin, 1, 1);
    hardware->addWidget(basebandLabel, 1, 2);
    hardware->addWidget(sampleRateCombo, 1, 3);
    hardware->addWidget(deviceRateLabel, 2, 0);
    hardware->addWidget(deviceSampleRateCombo, 2, 1);
    hardware->addWidget(bandwidthLabel, 2, 2);
    hardware->addWidget(bandwidthCombo, 2, 3);
    hardware->addWidget(levelLabel, 3, 0);
    hardware->addWidget(levelSpin, 3, 1);
    hardware->addWidget(txGainLabel, 3, 2);
    hardware->addWidget(txGainSpin, 3, 3);
    hardware->addWidget(watchdogLabel, 4, 0);
    hardware->addWidget(watchdogSpin, 4, 1);
    hardware->addWidget(rfAmpCheck, 4, 2);
    hardware->addWidget(armCheck, 4, 3);
    hardware->addWidget(rfLockLabel, 5, 0, 1, 4);
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
    sourceCombo->addItem(QStringLiteral("Microphone PTT"), QStringLiteral("microphone-ptt"));
    sourceCombo->addItem(QStringLiteral("Text"), QStringLiteral("text"));
    sourceCombo->addItem(QStringLiteral("Manual CW"), QStringLiteral("manual-cw"));
    sourceCombo->addItem(QStringLiteral("Audio file"), QStringLiteral("audio-file"));
    sourceCombo->addItem(QStringLiteral("SSTV image"), QStringLiteral("image-sstv"));
    sourceCombo->addItem(QStringLiteral("Analog video frame"), QStringLiteral("image-atv"));
    audioInputCombo = new QComboBox(signalBox);
    audioRefreshButton = new QToolButton(signalBox);
    audioRefreshButton->setAutoRaise(true);
    audioRefreshButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    signalBandwidthCombo = new QComboBox(signalBox);
    signalBandwidthCombo->setEditable(true);
    signalBandwidthCombo->setInsertPolicy(QComboBox::NoInsert);
    signalBandwidthCombo->addItem(QStringLiteral("Auto / unlimited"), 0.0);
    signalBandwidthCombo->addItem(QStringLiteral("100 Hz - FT8"), 100.0);
    signalBandwidthCombo->addItem(QStringLiteral("250 Hz - narrow CW"), 250.0);
    signalBandwidthCombo->addItem(QStringLiteral("500 Hz - CW"), 500.0);
    signalBandwidthCombo->addItem(QStringLiteral("1 kHz - wide CW"), 1000.0);
    signalBandwidthCombo->addItem(QStringLiteral("2.7 kHz - SSB voice"), 2700.0);
    signalBandwidthCombo->addItem(QStringLiteral("6 kHz - AM voice"), 6000.0);
    signalBandwidthCombo->addItem(QStringLiteral("8.33 kHz"), 8330.0);
    signalBandwidthCombo->addItem(QStringLiteral("12.5 kHz - NFM / SSTV"), 12500.0);
    signalBandwidthCombo->addItem(QStringLiteral("25 kHz - NFM / SSTV"), 25000.0);
    signalBandwidthCombo->addItem(QStringLiteral("50 kHz"), 50000.0);
    signalBandwidthCombo->addItem(QStringLiteral("100 kHz"), 100000.0);
    signalBandwidthCombo->addItem(QStringLiteral("200 kHz - WFM"), 200000.0);
    signalBandwidthCombo->addItem(QStringLiteral("500 kHz - narrow ATV"), 500000.0);
    signalBandwidthCombo->addItem(QStringLiteral("1 MHz - ATV"), 1000000.0);
    signalBandwidthCombo->addItem(QStringLiteral("1.5 MHz - ATV"), 1500000.0);
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
    liveKeyButton = new QPushButton(signalBox);
    liveKeyButton->setEnabled(false);
    liveKeyButton->setAutoRepeat(false);
    liveKeyButton->setAutoDefault(false);
    liveKeyButton->setDefault(false);
    liveKeyButton->setFocusPolicy(Qt::StrongFocus);
    liveKeyButton->setMinimumHeight(42);
    modeLabel = new QLabel(signalBox);
    sourceLabel = new QLabel(signalBox);
    audioInputLabel = new QLabel(signalBox);
    deviationLabel = new QLabel(signalBox);
    signalBandwidthLabel = new QLabel(signalBox);
    toneLabel = new QLabel(signalBox);
    cwSpeedLabel = new QLabel(signalBox);
    signal->addWidget(modeLabel, 0, 0);
    signal->addWidget(modulationCombo, 0, 1);
    signal->addWidget(sourceLabel, 0, 2);
    signal->addWidget(sourceCombo, 0, 3);
    signal->addWidget(audioInputLabel, 1, 0);
    signal->addWidget(audioInputCombo, 1, 1, 1, 2);
    signal->addWidget(audioRefreshButton, 1, 3);
    signal->addWidget(deviationLabel, 2, 0);
    signal->addWidget(deviationSpin, 2, 1);
    signal->addWidget(toneLabel, 2, 2);
    signal->addWidget(toneSpin, 2, 3);
    signal->addWidget(signalBandwidthLabel, 3, 0);
    signal->addWidget(signalBandwidthCombo, 3, 1, 1, 3);
    signal->addWidget(cwSpeedLabel, 4, 0);
    signal->addWidget(cwWpmSpin, 4, 1);
    signal->addWidget(ft8UtcCheck, 4, 2, 1, 2);
    signal->addWidget(liveKeyButton, 5, 0, 1, 4);
    signal->setColumnStretch(1, 1);
    signal->setColumnStretch(3, 1);

    QGroupBox *messageBox = new QGroupBox(this);
    messageBox->setObjectName(QStringLiteral("txMessageBox"));
    QVBoxLayout *messageLayout = new QVBoxLayout(messageBox);
    messageEdit = new QPlainTextEdit(messageBox);
    messageEdit->setPlaceholderText(QStringLiteral("CQ TEST KN34 / HELLO WORLD"));
    messageEdit->setMaximumBlockCount(20);
    messageLayout->addWidget(messageEdit);

    QGroupBox *mediaBox = new QGroupBox(this);
    mediaBox->setObjectName(QStringLiteral("txMediaBox"));
    QGridLayout *mediaLayout = new QGridLayout(mediaBox);
    mediaPathLabel = new QLabel(mediaBox);
    mediaPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mediaPathLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    mediaBrowseButton = new QPushButton(mediaBox);
    mediaEditButton = new QPushButton(mediaBox);
    mediaModeLabel = new QLabel(mediaBox);
    mediaModeCombo = new QComboBox(mediaBox);
    mediaModeCombo->addItem(QStringLiteral("Robot 36"), QStringLiteral("robot36"));
    mediaModeCombo->addItem(QStringLiteral("Martin M1"), QStringLiteral("martin-m1"));
    mediaModeCombo->addItem(QStringLiteral("Martin M2"), QStringLiteral("martin-m2"));
    mediaModeCombo->addItem(QStringLiteral("Analog TV / AM"), QStringLiteral("atv-am"));
    mediaModeCombo->addItem(QStringLiteral("Analog TV / FM"), QStringLiteral("atv-fm"));
    mediaPlayButton = new QPushButton(mediaBox);
    mediaPauseButton = new QPushButton(mediaBox);
    mediaStopButton = new QPushButton(mediaBox);
    mediaLayout->addWidget(mediaPathLabel, 0, 0, 1, 3);
    mediaLayout->addWidget(mediaBrowseButton, 0, 3);
    mediaLayout->addWidget(mediaEditButton, 0, 4);
    mediaLayout->addWidget(mediaModeLabel, 1, 0);
    mediaLayout->addWidget(mediaModeCombo, 1, 1);
    mediaLayout->addWidget(mediaPlayButton, 1, 2);
    mediaLayout->addWidget(mediaPauseButton, 1, 3);
    mediaLayout->addWidget(mediaStopButton, 1, 4);
    mediaLayout->setColumnStretch(1, 1);

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
    root->addWidget(mediaBox);
    root->addWidget(preview, 1);
    root->addWidget(progressBar);
    root->addWidget(statusLabel);
    root->addWidget(statsLabel);
    root->addLayout(actions);

    progressTimer = new QTimer(this);
    progressTimer->setInterval(40);
    sourceTimer = new QTimer(this);
    sourceTimer->setInterval(20);
    connect(progressTimer, &QTimer::timeout, this, [this]() { updateProgress(); });
    connect(sourceTimer, &QTimer::timeout, this, [this]() { processLiveSource(); });
    connect(modulationCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this]() { updateModeUi(); });
    connect(backendCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this]() {
                armCheck->setChecked(false);
                updateModeUi();
            });
    connect(armCheck, &QCheckBox::toggled, this, [this]() {
        if (!(activeBackend && activeBackend->isRunning())) updateTexts();
    });
    connect(sourceCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this]() { updateModeUi(); });
    connect(audioRefreshButton, &QToolButton::clicked, this, [this]() {
        populateAudioInputs();
        updateModeUi();
        const bool found = !audioInputCombo->currentData().toString().isEmpty();
        setStatus(found
                      ? text(QStringLiteral("Microphone list refreshed: %1 device(s)"),
                             QString::fromUtf8(u8"Список мікрофонів оновлено: %1 пристр."))
                            .arg(audioInputCombo->count())
                      : text(QStringLiteral("No microphone input is currently available"),
                             QString::fromUtf8(u8"Зараз немає доступного мікрофонного входу")),
                  !found);
    });
    connect(signalBandwidthCombo, &QComboBox::currentTextChanged,
            this, [this]() { applyLiveSignalShape(); });
    connect(deviationSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this]() { applyLiveSignalShape(); });
    connect(liveKeyButton, &QPushButton::pressed, this, [this]() { setLiveKey(true); });
    connect(liveKeyButton, &QPushButton::released, this, [this]() { setLiveKey(false); });
    connect(liveKeyButton, &QPushButton::clicked, this, [this]() {
        if (!(activeBackend && activeBackend->isRunning())) {
            startTransmission();
        }
        if (activeBackend && activeBackend->isRunning()) {
            liveKeyButton->setFocus(Qt::MouseFocusReason);
            updateTexts();
        }
    });
    connect(startButton, &QPushButton::clicked, this, [this]() { startTransmission(); });
    connect(stopButton, &QPushButton::clicked, this, [this]() {
        stopTransmission(text(QStringLiteral("Stopped by user"), QString::fromUtf8(u8"Зупинено користувачем")));
    });
    connect(exportButton, &QPushButton::clicked, this, [this]() { exportIq(); });
    connect(mediaBrowseButton, &QPushButton::clicked, this, [this]() { chooseMediaFile(); });
    connect(mediaEditButton, &QPushButton::clicked, this, [this]() { editMediaImage(); });
    connect(mediaModeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        preparedMediaImage = QImage();
    });
    connect(mediaPlayButton, &QPushButton::clicked, this, [this]() {
        if (activeBackend && activeBackend->isRunning()) {
            mediaPaused = false;
            sourceTimer->start();
        } else {
            startTransmission();
        }
    });
    connect(mediaPauseButton, &QPushButton::clicked, this, [this]() {
        mediaPaused = true;
        sourceTimer->stop();
    });
    connect(mediaStopButton, &QPushButton::clicked, this, [this]() {
        stopTransmission(text(QStringLiteral("Media transmission stopped"),
                              QString::fromUtf8(u8"Передачу медіа зупинено")));
    });
}

void TransmitDialog::populateAudioInputs() {
    const QString previousDevice = audioInputCombo->currentData().toString();
    audioInputCombo->clear();
    audioInputCombo->setEnabled(true);
#ifdef FOBOSAPP_HAS_QT_MULTIMEDIA
    QList<QAudioDeviceInfo> devices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    const QAudioDeviceInfo defaultDevice = QAudioDeviceInfo::defaultInputDevice();
    if (!defaultDevice.isNull()) {
        bool alreadyListed = false;
        for (const QAudioDeviceInfo &device : devices) {
            if (device.deviceName() == defaultDevice.deviceName()) {
                alreadyListed = true;
                break;
            }
        }
        if (!alreadyListed) devices.prepend(defaultDevice);
    }
    QStringList names;
    for (const QAudioDeviceInfo &device : devices) {
        const QString name = device.deviceName().trimmed();
        if (name.isEmpty() || names.contains(name)) continue;
        names.append(name);
        audioInputCombo->addItem(name, name);
    }
    int selectedIndex = previousDevice.isEmpty() ? -1 : audioInputCombo->findData(previousDevice);
    if (selectedIndex < 0 && !defaultDevice.isNull()) {
        selectedIndex = audioInputCombo->findData(defaultDevice.deviceName());
    }
    if (selectedIndex >= 0) audioInputCombo->setCurrentIndex(selectedIndex);
    if (audioInputCombo->count() == 0) {
        audioInputCombo->addItem(
            text(QStringLiteral("No microphone devices"),
                 QString::fromUtf8(u8"Мікрофонні пристрої не знайдено")),
            QString());
        audioInputCombo->setEnabled(false);
    }
    qInfo().noquote() << "[TX audio] microphone inputs" << names.size()
                      << (names.isEmpty() ? QStringLiteral("none") : names.join(QStringLiteral(" | ")));
#else
    audioInputCombo->addItem(
        text(QStringLiteral("Qt Multimedia is unavailable"),
             QString::fromUtf8(u8"Qt Multimedia недоступна")),
        QString());
    audioInputCombo->setEnabled(false);
    qWarning().noquote() << "[TX audio] Qt Multimedia microphone support is unavailable";
#endif
}
void TransmitDialog::populateHackRfDevices() {
    deviceCombo->clear();
    HackRfRuntimeStatus status;
    const QVector<HackRfDeviceInfo> devices = enumerateHackRfDevices(&status);
    for (const HackRfDeviceInfo &device : devices) {
        deviceCombo->addItem(device.label, device.nativeIndex);
    }
    const bool available = status.libraryAvailable && !devices.isEmpty();
    if (!available) {
        deviceCombo->addItem(text(QStringLiteral("No HackRF detected"),
                                  QString::fromUtf8(u8"HackRF не знайдено")), -1);
    }
    if (auto *model = qobject_cast<QStandardItemModel *>(backendCombo->model())) {
        if (QStandardItem *item = model->item(1)) item->setEnabled(available);
    }
    if (!available && hackRfSelected()) {
        backendCombo->setCurrentIndex(0);
    }
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
    if (auto *box = findChild<QGroupBox *>(QStringLiteral("txMediaBox")))
        box->setTitle(text(QStringLiteral("Media source"), QString::fromUtf8(u8"Медіаджерело")));
    backendLabel->setText(text(QStringLiteral("Output:"), QString::fromUtf8(u8"Вихід:")));
    deviceLabel->setText(text(QStringLiteral("Device:"), QString::fromUtf8(u8"Пристрій:")));
    rfLabel->setText(text(QStringLiteral("RF carrier:"), QString::fromUtf8(u8"Несуча RF:")));
    basebandLabel->setText(text(QStringLiteral("Generator rate:"),
                                QString::fromUtf8(u8"Частота генератора:")));
    deviceRateLabel->setText(text(QStringLiteral("HackRF IQ rate:"),
                                  QString::fromUtf8(u8"Частота передавання IQ:")));
    bandwidthLabel->setText(text(QStringLiteral("HackRF RF filter:"),
                                 QString::fromUtf8(u8"Апаратний RF-фільтр:")));
    levelLabel->setText(text(QStringLiteral("IQ amplitude:"), QString::fromUtf8(u8"Амплітуда IQ:")));
    txGainLabel->setText(text(QStringLiteral("HackRF TX VGA:"), QString::fromUtf8(u8"HackRF TX VGA:")));
    watchdogLabel->setText(text(QStringLiteral("RF watchdog:"), QString::fromUtf8(u8"Таймер RF:")));
    modeLabel->setText(text(QStringLiteral("Mode:"), QString::fromUtf8(u8"Режим:")));
    sourceLabel->setText(text(QStringLiteral("Source:"), QString::fromUtf8(u8"Джерело:")));
    audioInputLabel->setText(text(QStringLiteral("Microphone input:"), QString::fromUtf8(u8"Мікрофонний вхід:")));
    audioRefreshButton->setToolTip(text(QStringLiteral("Refresh microphone devices"),
                                        QString::fromUtf8(u8"Оновити список мікрофонів")));
    deviationLabel->setText(text(QStringLiteral("FM deviation:"), QString::fromUtf8(u8"Девіація FM:")));
    signalBandwidthLabel->setText(text(QStringLiteral("TX signal bandwidth:"),
                                       QString::fromUtf8(u8"Смуга TX-сигналу:")));
    const auto setSignalBandwidthText = [this](double bandwidth, const QString &english,
                                                const QString &ukrainian) {
        const int index = signalBandwidthCombo->findData(bandwidth);
        if (index >= 0) signalBandwidthCombo->setItemText(index, text(english, ukrainian));
    };
    setSignalBandwidthText(0.0, QStringLiteral("Auto / unlimited"),
                           QString::fromUtf8(u8"Авто / без обмеження"));
    setSignalBandwidthText(100.0, QStringLiteral("100 Hz - FT8"), QString::fromUtf8(u8"100 Гц - FT8"));
    setSignalBandwidthText(250.0, QStringLiteral("250 Hz - narrow CW"), QString::fromUtf8(u8"250 Гц - вузький CW"));
    setSignalBandwidthText(500.0, QStringLiteral("500 Hz - CW"), QString::fromUtf8(u8"500 Гц - CW"));
    setSignalBandwidthText(1000.0, QStringLiteral("1 kHz - wide CW"), QString::fromUtf8(u8"1 кГц - широкий CW"));
    setSignalBandwidthText(2700.0, QStringLiteral("2.7 kHz - SSB voice"), QString::fromUtf8(u8"2,7 кГц - голос SSB"));
    setSignalBandwidthText(6000.0, QStringLiteral("6 kHz - AM voice"), QString::fromUtf8(u8"6 кГц - голос AM"));
    setSignalBandwidthText(8330.0, QStringLiteral("8.33 kHz - channel"), QString::fromUtf8(u8"8,33 кГц - канал"));
    setSignalBandwidthText(12500.0, QStringLiteral("12.5 kHz - NFM / SSTV"), QString::fromUtf8(u8"12,5 кГц - NFM / SSTV"));
    setSignalBandwidthText(25000.0, QStringLiteral("25 kHz - NFM / SSTV"), QString::fromUtf8(u8"25 кГц - NFM / SSTV"));
    setSignalBandwidthText(50000.0, QStringLiteral("50 kHz - wide FM"), QString::fromUtf8(u8"50 кГц - широка FM"));
    setSignalBandwidthText(100000.0, QStringLiteral("100 kHz - wide FM"), QString::fromUtf8(u8"100 кГц - широка FM"));
    setSignalBandwidthText(200000.0, QStringLiteral("200 kHz - WFM"), QString::fromUtf8(u8"200 кГц - WFM"));
    setSignalBandwidthText(500000.0, QStringLiteral("500 kHz - narrow ATV"), QString::fromUtf8(u8"500 кГц - вузьке ATV"));
    setSignalBandwidthText(1000000.0, QStringLiteral("1 MHz - ATV"), QString::fromUtf8(u8"1 МГц - ATV"));
    setSignalBandwidthText(1500000.0, QStringLiteral("1.5 MHz - ATV"), QString::fromUtf8(u8"1,5 МГц - ATV"));
    signalBandwidthCombo->setToolTip(text(
        QStringLiteral("Full two-sided occupied IQ bandwidth. For example, 12.5 kHz passes approximately +/-6.25 kHz around the carrier. Type a custom value in Hz or choose a preset."),
        QString::fromUtf8(u8"Повна двостороння зайнята смуга IQ. Наприклад, 12,5 кГц пропускає приблизно +/-6,25 кГц навколо несучої. Впишіть власне значення у Гц або оберіть пресет.")));
    toneLabel->setText(text(QStringLiteral("CW / FT8 tone:"), QString::fromUtf8(u8"Тон CW / FT8:")));
    basebandLabel->setToolTip(text(
        QStringLiteral("Rate at which Obrii SDR generates the useful complex-IQ signal before hardware resampling."),
        QString::fromUtf8(u8"Частота, з якою Obrii SDR формує корисний комплексний IQ до апаратної передискретизації.")));
    deviceRateLabel->setToolTip(text(
        QStringLiteral("Actual IQ sample rate sent to the HackRF DAC. It may be higher than the generated signal rate."),
        QString::fromUtf8(u8"Фактична частота IQ, що подається на DAC HackRF. Вона може бути вищою за частоту формування сигналу.")));
    bandwidthLabel->setToolTip(text(
        QStringLiteral("Hardware HackRF baseband filter. Keep it wider than the transmitted signal bandwidth."),
        QString::fromUtf8(u8"Апаратний фільтр HackRF. Його смуга має бути ширшою за смугу переданого сигналу.")));
    cwSpeedLabel->setText(text(QStringLiteral("CW speed:"), QString::fromUtf8(u8"Швидкість CW:")));
    backendCombo->setItemText(0, text(QStringLiteral("Simulator / IQ file"),
                                      QString::fromUtf8(u8"Симулятор / файл IQ")));
    backendCombo->setItemText(1, QStringLiteral("HackRF native"));
    const auto setSourceText = [this](const QString &id, const QString &english, const QString &ukrainian) {
        const int index = sourceCombo->findData(id);
        if (index >= 0) sourceCombo->setItemText(index, text(english, ukrainian));
    };
    setSourceText(QStringLiteral("microphone"), QStringLiteral("Microphone"), QString::fromUtf8(u8"Мікрофон"));
    setSourceText(QStringLiteral("microphone-ptt"), QStringLiteral("Microphone PTT"),
                  QString::fromUtf8(u8"Мікрофон PTT"));
    setSourceText(QStringLiteral("text"), QStringLiteral("Text"), QString::fromUtf8(u8"Текст"));
    setSourceText(QStringLiteral("manual-cw"), QStringLiteral("Manual CW key"),
                  QString::fromUtf8(u8"Ручний ключ CW"));
    setSourceText(QStringLiteral("audio-file"), QStringLiteral("Audio file"), QString::fromUtf8(u8"Аудіофайл"));
    setSourceText(QStringLiteral("image-sstv"), QStringLiteral("SSTV image"), QString::fromUtf8(u8"Зображення SSTV"));
    setSourceText(QStringLiteral("image-atv"), QStringLiteral("Analog video test frame"),
                  QString::fromUtf8(u8"Тестовий кадр аналогового відео"));
    mediaBrowseButton->setText(text(QStringLiteral("Browse..."), QString::fromUtf8(u8"Обрати...")));
    mediaEditButton->setText(text(QStringLiteral("Prepare image..."),
                                  QString::fromUtf8(u8"Підготувати фото...")));
    mediaPlayButton->setText(text(QStringLiteral("Play / transmit"), QString::fromUtf8(u8"Відтворити / передати")));
    mediaPauseButton->setText(text(QStringLiteral("Pause"), QString::fromUtf8(u8"Пауза")));
    mediaStopButton->setText(text(QStringLiteral("Stop media"), QString::fromUtf8(u8"Стоп медіа")));
    if (!liveKeyPressed) {
        const bool liveReady = activeBackend && activeBackend->isRunning();
        liveKeyButton->setText(
            liveReady
                ? text(QStringLiteral("PTT / CW KEY — hold Space"),
                       QString::fromUtf8(u8"PTT / КЛЮЧ CW — утримуйте Пробіл"))
                : text(QStringLiteral("Start transmission"),
                       QString::fromUtf8(u8"Почати передачу")));
        liveKeyButton->setStyleSheet(
            liveReady
                ? QStringLiteral("background:#218c4c;color:white;font-weight:600;")
                : QStringLiteral("background:#6b7078;color:white;font-weight:600;"));
    }
    liveKeyButton->setToolTip(
        text(QStringLiteral("Click the button once to focus it. While focused, hold Space to transmit; "
                            "release Space to stop. F12 works anywhere in this window."),
             QString::fromUtf8(u8"Клацніть кнопку один раз, щоб передати їй фокус. Після цього утримуйте "
                               u8"Пробіл для передачі та відпустіть для зупинки. F12 працює в усьому вікні.")));
    mediaModeLabel->setText(text(QStringLiteral("Image/video mode:"), QString::fromUtf8(u8"Режим зображення/відео:")));
    mediaPathLabel->setText(mediaPath.isEmpty()
                                ? text(QStringLiteral("No media file selected"),
                                       QString::fromUtf8(u8"Медіафайл не обрано"))
                                : QFileInfo(mediaPath).fileName());
    startButton->setText(hackRfSelected()
                             ? text(QStringLiteral("Start RF transmission"), QString::fromUtf8(u8"Почати RF-передачу"))
                             : text(QStringLiteral("Start simulation"), QString::fromUtf8(u8"Почати симуляцію")));
    stopButton->setText(text(QStringLiteral("Stop"), QString::fromUtf8(u8"Зупинити")));
    exportButton->setText(text(QStringLiteral("Export IQ..."), QString::fromUtf8(u8"Експорт IQ...")));
    ft8UtcCheck->setText(text(QStringLiteral("Align FT8 to UTC 15 s slot"),
                              QString::fromUtf8(u8"Прив'язати FT8 до 15-секундного UTC-слота")));
    rfAmpCheck->setText(text(QStringLiteral("RF amp (+~11 dB)"),
                             QString::fromUtf8(u8"RF-підсилювач (+~11 дБ)")));
    armCheck->setText(text(QStringLiteral("ARM RF output"),
                           QString::fromUtf8(u8"ОЗБРОЇТИ RF-вихід")));
    rfLockLabel->setText(hackRfSelected()
        ? text(QStringLiteral("Connect a suitable antenna or dummy load. Verify frequency, local rules and gain. Stop reception on this HackRF before transmitting."),
               QString::fromUtf8(u8"Підключіть відповідну антену або еквівалент навантаження. Перевірте частоту, місцеві правила та підсилення. Перед передачею зупиніть прийом на цьому HackRF."))
        : text(QStringLiteral("Simulation and IQ export do not emit RF."),
               QString::fromUtf8(u8"Симуляція та експорт IQ не випромінюють RF.")));
    if (!(activeBackend && activeBackend->isRunning())) {
        setStatus(hackRfSelected()
                      ? (armCheck->isChecked()
                             ? text(QStringLiteral("Ready; RF output is armed for this dialog session"),
                                    QString::fromUtf8(u8"Готово; RF-вихід озброєно на цю сесію вікна"))
                             : text(QStringLiteral("Ready; RF output is disarmed"),
                                    QString::fromUtf8(u8"Готово; RF-вихід не озброєний")))
                      : text(QStringLiteral("Ready; no RF can be emitted"),
                             QString::fromUtf8(u8"Готово; випромінювання RF неможливе")));
    }
}

bool TransmitDialog::hackRfSelected() const {
    return backendCombo && backendCombo->currentData().toString() == QStringLiteral("hackrf-native");
}

TransmitterBackend *TransmitDialog::selectedBackend() {
    return hackRfSelected() ? static_cast<TransmitterBackend *>(&hackRf)
                            : static_cast<TransmitterBackend *>(&simulator);
}

TxModulation TransmitDialog::selectedModulation() const {
    return static_cast<TxModulation>(modulationCombo->currentData().toInt());
}

double TransmitDialog::signalBandwidthFromUi() const {
    if (!signalBandwidthCombo) return 0.0;
    const QVariant preset = signalBandwidthCombo->currentData();
    if (signalBandwidthCombo->currentIndex() >= 0 && preset.isValid()) {
        return (std::clamp)(preset.toDouble(), 0.0, 20000000.0);
    }
    QString value = signalBandwidthCombo->currentText().trimmed().toLower();
    if (value.isEmpty() || value.contains(QStringLiteral("auto")) ||
        value.contains(QString::fromUtf8(u8"авто"))) return 0.0;
    double multiplier = 1.0;
    if (value.contains(QStringLiteral("mhz")) || value.contains(QString::fromUtf8(u8"мгц"))) {
        multiplier = 1.0e6;
    } else if (value.contains(QStringLiteral("khz")) || value.contains(QString::fromUtf8(u8"кгц"))) {
        multiplier = 1.0e3;
    }
    value.replace(QLatin1Char(','), QLatin1Char('.'));
    int end = 0;
    while (end < value.size() && (value.at(end).isDigit() || value.at(end) == QLatin1Char('.') ||
                                  value.at(end).isSpace())) ++end;
    bool ok = false;
    const double numeric = value.left(end).trimmed().toDouble(&ok);
    return ok ? (std::clamp)(numeric * multiplier, 0.0, 20000000.0) : 0.0;
}

bool TransmitDialog::validateSignalBandwidth(const TxConfiguration &configuration) {
    if (configuration.signalBandwidthHz <= 0.0) return true;
    const double maximum = configuration.sampleRate * 0.90;
    if (configuration.signalBandwidthHz > maximum) {
        setStatus(text(
            QStringLiteral("Signal bandwidth %1 Hz does not fit the %2 Hz generator rate. Increase the generator rate or choose a narrower preset."),
            QString::fromUtf8(u8"Смуга сигналу %1 Гц не вміщується у частоту генератора %2 Гц. Збільште частоту генератора або оберіть вужчий пресет."))
                      .arg(configuration.signalBandwidthHz, 0, 'f', 0)
                      .arg(configuration.sampleRate), true);
        return false;
    }
    if (configuration.modulation == TxModulation::Nfm ||
        configuration.modulation == TxModulation::Wfm) {
        const double deviation =
            TransmitWaveformGenerator::effectiveFmDeviationHz(configuration);
        const double audioCutoff = configuration.signalBandwidthHz * 0.5 - deviation;
        if (audioCutoff < 100.0) {
            const double minimumBandwidth = 2.0 * (deviation + 100.0);
            setStatus(text(
                QStringLiteral("The selected FM bandwidth is incompatible with %1 Hz deviation. Use at least %2 Hz bandwidth or reduce deviation."),
                QString::fromUtf8(u8"Обрана смуга FM несумісна з девіацією %1 Гц. Задайте смугу не менше %2 Гц або зменште девіацію."))
                          .arg(deviation, 0, 'f', 0)
                          .arg(minimumBandwidth, 0, 'f', 0), true);
            return false;
        }
    }
    double signalCenter = 0.0;
    if (configuration.modulation == TxModulation::Cw) {
        signalCenter = configuration.toneHz;
    } else if (configuration.modulation == TxModulation::Ft8) {
        signalCenter = configuration.toneHz + 21.875;
    }
    if (signalCenter + configuration.signalBandwidthHz * 0.5 >=
        configuration.sampleRate * 0.49) {
        setStatus(text(
            QStringLiteral("The selected tone and bandwidth do not fit the generator rate."),
            QString::fromUtf8(u8"Обраний тон і смуга не вміщуються у частоту генератора.")), true);
        return false;
    }
    return true;
}

QString TransmitDialog::signalShapeSummary(const TxConfiguration &configuration) const {
    const auto frequencyText = [](double value) {
        if (value >= 1.0e6) return QStringLiteral("%1 MHz").arg(value / 1.0e6, 0, 'f', 3);
        if (value >= 1.0e3) return QStringLiteral("%1 kHz").arg(value / 1.0e3, 0, 'f', 2);
        return QStringLiteral("%1 Hz").arg(value, 0, 'f', 0);
    };
    if (configuration.signalBandwidthHz <= 0.0) {
        return text(QStringLiteral("TX bandwidth: Auto"),
                    QString::fromUtf8(u8"Смуга TX: Авто"));
    }
    QString summary = text(QStringLiteral("TX bandwidth: %1"),
                           QString::fromUtf8(u8"Смуга TX: %1"))
                          .arg(frequencyText(configuration.signalBandwidthHz));
    const double audioCutoff =
        TransmitWaveformGenerator::audioLowPassCutoffHz(configuration);
    if (audioCutoff > 0.0) {
        summary += text(QStringLiteral(" | audio LPF: %1"),
                        QString::fromUtf8(u8" | ФНЧ аудіо: %1"))
                       .arg(frequencyText(audioCutoff));
    }
    if (configuration.modulation == TxModulation::Nfm ||
        configuration.modulation == TxModulation::Wfm) {
        summary += text(QStringLiteral(" | deviation: %1"),
                        QString::fromUtf8(u8" | девіація: %1"))
                       .arg(frequencyText(
                           TransmitWaveformGenerator::effectiveFmDeviationHz(configuration)));
    }
    return summary;
}

void TransmitDialog::applyLiveSignalShape() {
    if (!activeBackend || !activeBackend->isRunning()) return;
    TxConfiguration next = activeConfiguration;
    next.signalBandwidthHz = signalBandwidthFromUi();
    next.deviationHz = deviationSpin->value();
    if (!validateSignalBandwidth(next)) return;
    activeConfiguration = next;
    bandwidthLimiterState.reset();
    modulatorState.resetAudioFilter();
    const QString summary = signalShapeSummary(activeConfiguration);
    setStatus(text(QStringLiteral("Live TX shaping updated. %1"),
                   QString::fromUtf8(u8"Формування TX оновлено наживо. %1"))
                  .arg(summary));
    qInfo().noquote() << "[TX shaping] live update"
                      << "modulation" << TransmitWaveformGenerator::modulationName(activeConfiguration.modulation)
                      << "bandwidthHz" << activeConfiguration.signalBandwidthHz
                      << "deviationHz" << activeConfiguration.deviationHz
                      << "audioLowPassHz"
                      << TransmitWaveformGenerator::audioLowPassCutoffHz(activeConfiguration);
}

bool TransmitDialog::writeActiveIq(const std::complex<float> *samples, int count, QString *error) {
    if (!activeBackend) {
        if (error) *error = QStringLiteral("No active transmitter backend");
        return false;
    }
    if (TransmitWaveformGenerator::limitSignalBandwidth(activeConfiguration, samples, count,
                                                         &bandwidthLimitedIq,
                                                         &bandwidthLimiterState)) {
        return activeBackend->writeIq(bandwidthLimitedIq.constData(), bandwidthLimitedIq.size(), error);
    }
    return activeBackend->writeIq(samples, count, error);
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
    configuration.signalBandwidthHz = signalBandwidthFromUi();
    configuration.toneHz = toneSpin->value();
    configuration.cwWpm = cwWpmSpin->value();
    configuration.alignFt8ToUtcSlot = ft8UtcCheck->isChecked();
    configuration.deviceIndex = deviceCombo->currentData().toInt();
    configuration.deviceSampleRate = deviceSampleRateCombo->currentData().toInt();
    configuration.bandwidthHz = static_cast<std::uint32_t>(bandwidthCombo->currentData().toUInt());
    configuration.txVgaGainDb = txGainSpin->value();
    configuration.rfAmpEnabled = rfAmpCheck->isChecked();
    return configuration;
}

void TransmitDialog::updateModeUi() {
    const TxModulation modulation = selectedModulation();
    QString source = sourceCombo->currentData().toString();
    if (modulation == TxModulation::Ft8 && source != QStringLiteral("text")) {
        sourceCombo->setCurrentIndex(sourceCombo->findData(QStringLiteral("text")));
        source = QStringLiteral("text");
    } else if (modulation == TxModulation::Cw && source != QStringLiteral("text") &&
               source != QStringLiteral("manual-cw")) {
        sourceCombo->setCurrentIndex(sourceCombo->findData(QStringLiteral("text")));
        source = QStringLiteral("text");
    }
    sourceCombo->setEnabled(true);
    const bool textSource = source == QStringLiteral("text");
    const bool microphoneSource = source == QStringLiteral("microphone") ||
                                  source == QStringLiteral("microphone-ptt");
    const bool mediaSource = source == QStringLiteral("audio-file") ||
                             source == QStringLiteral("image-sstv") ||
                             source == QStringLiteral("image-atv");
    const bool imageSource = source.startsWith(QStringLiteral("image-"));
    const bool liveKeySource = source == QStringLiteral("manual-cw") ||
                               source == QStringLiteral("microphone-ptt");
    if (source == QStringLiteral("image-sstv") &&
        mediaModeCombo->currentData().toString().startsWith(QStringLiteral("atv-"))) {
        mediaModeCombo->setCurrentIndex(mediaModeCombo->findData(QStringLiteral("robot36")));
    } else if (source == QStringLiteral("image-atv") &&
               !mediaModeCombo->currentData().toString().startsWith(QStringLiteral("atv-"))) {
        mediaModeCombo->setCurrentIndex(mediaModeCombo->findData(QStringLiteral("atv-am")));
    }
    messageEdit->setEnabled(textSource);
    if (auto *box = findChild<QGroupBox *>(QStringLiteral("txMessageBox"))) box->setVisible(textSource);
    if (auto *box = findChild<QGroupBox *>(QStringLiteral("txMediaBox"))) box->setVisible(mediaSource);
    mediaModeCombo->setEnabled(imageSource);
    mediaEditButton->setEnabled(imageSource && !mediaPath.isEmpty());
    mediaPauseButton->setEnabled(source == QStringLiteral("audio-file") ||
                                 source == QStringLiteral("image-atv"));
    const bool microphoneAvailable = audioInputCombo->count() > 0 &&
                                     !audioInputCombo->currentData().toString().isEmpty();
    audioInputCombo->setEnabled(microphoneSource && microphoneAvailable);
    audioRefreshButton->setEnabled(microphoneSource && !(activeBackend && activeBackend->isRunning()));
    deviationSpin->setEnabled(modulation == TxModulation::Nfm || modulation == TxModulation::Wfm);
    toneSpin->setEnabled(TransmitWaveformGenerator::isTextMode(modulation) ||
                         source == QStringLiteral("manual-cw"));
    cwWpmSpin->setEnabled(modulation == TxModulation::Cw);
    ft8UtcCheck->setEnabled(modulation == TxModulation::Ft8);
    liveKeyButton->setVisible(liveKeySource);
    liveKeyButton->setEnabled(liveKeySource);
    const bool hardware = hackRfSelected();
    deviceCombo->setEnabled(hardware);
    deviceSampleRateCombo->setEnabled(hardware);
    bandwidthCombo->setEnabled(hardware);
    txGainSpin->setEnabled(hardware);
    watchdogSpin->setEnabled(hardware);
    rfAmpCheck->setEnabled(hardware);
    armCheck->setEnabled(hardware);
    frequencySpin->setMaximum(hardware ? 6000.0 : 7250.0);
    updateTexts();
}

void TransmitDialog::startTransmission() {
    if (activeBackend && activeBackend->isRunning()) return;
    activeBackend = selectedBackend();
    if (activeBackend->isRfCapable()) {
        if (!armCheck->isChecked()) {
            setStatus(text(QStringLiteral("Arm the RF output before starting"),
                           QString::fromUtf8(u8"Перед запуском озбройте RF-вихід")), true);
            return;
        }
        if (deviceCombo->currentData().toInt() < 0) {
            setStatus(text(QStringLiteral("No HackRF device is available"),
                           QString::fromUtf8(u8"Немає доступного пристрою HackRF")), true);
            return;
        }
    }
    saveSettings();
    preview->clear();
    progressBar->setValue(0);
    statsLabel->clear();
    modulatorState = TxModulatorState{};
    bandwidthLimiterState.reset();
    bandwidthLimitedIq.clear();
    TxConfiguration configuration = configurationFromUi();
    if (!validateSignalBandwidth(configuration)) return;
    const QString source = sourceCombo->currentData().toString();
    pttMode = source == QStringLiteral("microphone-ptt");
    if (source == QStringLiteral("text")) {
        startTextSimulation(configuration);
    } else if (source == QStringLiteral("microphone") || pttMode) {
        startMicrophoneSimulation(configuration);
    } else if (source == QStringLiteral("manual-cw")) {
        startManualCw(configuration);
    } else if (source == QStringLiteral("audio-file")) {
        startAudioFile(configuration);
    } else if (source == QStringLiteral("image-sstv") || source == QStringLiteral("image-atv")) {
        startImageTransmission(configuration);
    } else {
        setStatus(text(QStringLiteral("Unsupported transmission source"),
                       QString::fromUtf8(u8"Непідтримуване джерело передачі")), true);
    }
}

void TransmitDialog::startTextSimulation(const TxConfiguration &configuration) {
    const TxGenerationResult generated =
        TransmitWaveformGenerator::generateText(configuration, messageEdit->toPlainText());
    if (!generated.ok()) {
        armCheck->setChecked(false);
        setStatus(generated.error, true);
        return;
    }
    QString error;
    activeConfiguration = configuration;
    if (!activeBackend || !activeBackend->start(configuration, &error) ||
        !writeActiveIq(generated.iq.constData(), generated.iq.size(), &error)) {
        if (activeBackend) activeBackend->stop();
        armCheck->setChecked(false);
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
    sourceCombo->setEnabled(false);
    statsLabel->setText(generated.summary);
    QString slotNote;
    if (configuration.modulation == TxModulation::Ft8 && configuration.alignFt8ToUtcSlot) {
        slotNote = text(QStringLiteral(" UTC slot alignment is stored for the future RF backend; simulator starts now."),
                        QString::fromUtf8(u8" Прив'язку до UTC-слота збережено для майбутнього RF backend; симулятор стартує одразу."));
    }
    setStatus(activeBackend->isRfCapable()
                  ? text(QStringLiteral("Text waveform is being transmitted by HackRF."),
                         QString::fromUtf8(u8"Текстовий сигнал передається через HackRF."))
                  : text(QStringLiteral("Text baseband simulation is running."),
                         QString::fromUtf8(u8"Симуляція текстового baseband запущена.")) + slotNote);
}

void TransmitDialog::startMicrophoneSimulation(TxConfiguration configuration) {
#ifdef FOBOSAPP_HAS_QT_MULTIMEDIA
    populateAudioInputs();
    const QString requestedDevice = audioInputCombo->currentData().toString();
    const QList<QAudioDeviceInfo> devices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    const QAudioDeviceInfo defaultDevice = QAudioDeviceInfo::defaultInputDevice();
    QAudioDeviceInfo selected;
    for (const QAudioDeviceInfo &device : devices) {
        if (device.deviceName() == requestedDevice) {
            selected = device;
            break;
        }
    }
    if (selected.isNull() && !defaultDevice.isNull() &&
        (requestedDevice.isEmpty() || requestedDevice == defaultDevice.deviceName())) {
        selected = defaultDevice;
    }
    if (selected.isNull()) {
        setStatus(text(QStringLiteral("No microphone input is currently available. Refresh the device list and check Windows microphone privacy settings."),
                       QString::fromUtf8(u8"Зараз немає доступного мікрофонного входу. Оновіть список пристроїв і перевірте дозволи мікрофона у Windows.")), true);
        return;
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
    if (!validateSignalBandwidth(configuration)) return;
    configuration.liveSource = true;
    activeConfiguration = configuration;
    QString error;
    if (!activeBackend || !activeBackend->start(configuration, &error)) {
        armCheck->setChecked(false);
        setStatus(error, true);
        return;
    }
    if (pttMode && !activeBackend->setOutputEnabled(false, &error)) {
        activeBackend->stop();
        armCheck->setChecked(false);
        setStatus(error, true);
        return;
    }
    audioInput = new QAudioInput(selected, format, this);
    audioInput->setBufferSize((std::max)(4096, format.sampleRate() / 5 * 2));
    audioDevice = audioInput->start();
    if (!audioDevice) {
        activeBackend->stop();
        armCheck->setChecked(false);
        audioInput->deleteLater();
        audioInput = nullptr;
        setStatus(text(QStringLiteral("Could not start microphone capture"),
                       QString::fromUtf8(u8"Не вдалося запустити захоплення мікрофона")), true);
        return;
    }
    connect(audioDevice, &QIODevice::readyRead, this, [this]() { readMicrophone(); });
    plannedDurationSeconds = activeBackend->isRfCapable() ? watchdogSpin->value() : 180.0;
    textSimulation = false;
    elapsedTimer.restart();
    progressTimer->start();
    startButton->setEnabled(false);
    stopButton->setEnabled(true);
    exportButton->setEnabled(false);
    sourceCombo->setEnabled(false);
    if (pttMode) {
        liveKeyButton->setEnabled(true);
        liveKeyButton->setFocus(Qt::OtherFocusReason);
        updateTexts();
    }
    statsLabel->setText(signalShapeSummary(configuration));
    qInfo().noquote() << "[TX shaping] start"
                      << "microphone" << selected.deviceName()
                      << "sampleRate" << configuration.sampleRate
                      << "modulation" << TransmitWaveformGenerator::modulationName(configuration.modulation)
                      << "bandwidthHz" << configuration.signalBandwidthHz
                      << "deviationHz" << configuration.deviationHz
                      << "audioLowPassHz"
                      << TransmitWaveformGenerator::audioLowPassCutoffHz(configuration);
    setStatus(activeBackend->isRfCapable()
                  ? (pttMode
                         ? text(QStringLiteral("Microphone PTT is armed. Focus the PTT button and hold Space, "
                                               "hold it with the mouse, or use F12."),
                                QString::fromUtf8(u8"PTT мікрофона озброєно. Передайте фокус кнопці PTT й "
                                                  u8"утримуйте Пробіл, утримуйте її мишею або використовуйте F12."))
                         : text(QStringLiteral("Live microphone transmission is running."),
                                QString::fromUtf8(u8"Передача з мікрофона працює.")))
                  : text(QStringLiteral("Microphone baseband simulation is running; RF remains disabled."),
                         QString::fromUtf8(u8"Симуляція baseband з мікрофона працює; RF залишається вимкненим.")));
#else
    Q_UNUSED(configuration)
    setStatus(text(QStringLiteral("This build has no Qt Multimedia microphone support"),
                   QString::fromUtf8(u8"Ця збірка не має підтримки мікрофона Qt Multimedia")), true);
#endif
}

void TransmitDialog::startManualCw(TxConfiguration configuration) {
    configuration.modulation = TxModulation::Cw;
    configuration.liveSource = false;
    activeConfiguration = configuration;
    QString error;
    if (!activeBackend || !activeBackend->start(configuration, &error) ||
        !activeBackend->setOutputEnabled(false, &error)) {
        if (activeBackend) activeBackend->stop();
        armCheck->setChecked(false);
        setStatus(error, true);
        return;
    }
    manualCwPhase = 0.0;
    plannedDurationSeconds = activeBackend->isRfCapable() ? watchdogSpin->value() : 180.0;
    textSimulation = false;
    elapsedTimer.restart();
    progressTimer->start();
    startButton->setEnabled(false);
    stopButton->setEnabled(true);
    sourceCombo->setEnabled(false);
    liveKeyButton->setEnabled(true);
    liveKeyButton->setFocus(Qt::OtherFocusReason);
    updateTexts();
    setStatus(text(QStringLiteral("Manual CW is armed. Focus the key button and hold Space, "
                                  "hold it with the mouse, or use F12."),
                   QString::fromUtf8(u8"Ручний CW озброєно. Передайте фокус кнопці ключа й утримуйте "
                                     u8"Пробіл, утримуйте її мишею або використовуйте F12.")));
}

void TransmitDialog::startAudioFile(TxConfiguration configuration) {
    if (mediaPath.isEmpty()) chooseMediaFile();
    if (mediaPath.isEmpty()) return;
    configuration.sampleRate = (std::min)(configuration.sampleRate, 192000);
    const TxAudioMedia media = TransmitMediaGenerator::loadWav(mediaPath, configuration.sampleRate);
    if (!media.ok()) {
        setStatus(media.error, true);
        return;
    }
    configuration.sampleRate = media.sampleRate;
    if (!validateSignalBandwidth(configuration)) return;
    configuration.liveSource = true;
    activeConfiguration = configuration;
    QString error;
    if (!activeBackend || !activeBackend->start(configuration, &error)) {
        armCheck->setChecked(false);
        setStatus(error, true);
        return;
    }
    mediaAudio = media.samples;
    atvFrame.clear();
    mediaPosition = 0;
    mediaPaused = false;
    plannedDurationSeconds = static_cast<double>(mediaAudio.size()) / configuration.sampleRate;
    textSimulation = false;
    elapsedTimer.restart();
    progressTimer->start();
    sourceTimer->setInterval(20);
    for (int i = 0; i < 6 && activeBackend && activeBackend->isRunning(); ++i) processLiveSource();
    if (!activeBackend || !activeBackend->isRunning()) return;
    sourceTimer->start();
    startButton->setEnabled(false);
    stopButton->setEnabled(true);
    sourceCombo->setEnabled(false);
    statsLabel->setText(media.summary);
    setStatus(text(QStringLiteral("Audio file transmission is running."),
                   QString::fromUtf8(u8"Передача аудіофайлу працює.")));
}

void TransmitDialog::startImageTransmission(TxConfiguration configuration) {
    if (mediaPath.isEmpty() && sourceCombo->currentData().toString() == QStringLiteral("image-sstv")) {
        chooseMediaFile();
    }
    if (preparedMediaImage.isNull() && !mediaPath.isEmpty()) editMediaImage();
    const QImage image = preparedMediaImage.isNull() ? QImage(mediaPath) : preparedMediaImage;
    const QString requestedMode = mediaModeCombo->currentData().toString();
    const bool atv = sourceCombo->currentData().toString() == QStringLiteral("image-atv");
    if (!atv) {
        configuration.sampleRate = (std::clamp)(configuration.sampleRate, 12000, 48000);
        const QString mode = requestedMode.startsWith(QStringLiteral("atv-"))
                                 ? QStringLiteral("robot36") : requestedMode;
        const TxAudioMedia media = TransmitMediaGenerator::generateSstv(
            image, mode, configuration.sampleRate);
        if (!media.ok()) {
            setStatus(media.error, true);
            return;
        }
        if (configuration.modulation == TxModulation::Cw ||
            configuration.modulation == TxModulation::Ft8) {
            configuration.modulation = TxModulation::Nfm;
        }
        configuration.deviationHz = (std::max)(2500.0, configuration.deviationHz);
        if (!validateSignalBandwidth(configuration)) return;
        configuration.liveSource = false;
        const QVector<std::complex<float>> iq =
            TransmitWaveformGenerator::modulateAudio(configuration, media.samples);
        QString error;
        activeConfiguration = configuration;
        if (!activeBackend || !activeBackend->start(configuration, &error) ||
            !writeActiveIq(iq.constData(), iq.size(), &error)) {
            if (activeBackend) activeBackend->stop();
            armCheck->setChecked(false);
            setStatus(error, true);
            return;
        }
        preview->setIq(iq);
        plannedDurationSeconds = static_cast<double>(iq.size()) / configuration.sampleRate;
        textSimulation = true;
        elapsedTimer.restart();
        progressTimer->start();
        startButton->setEnabled(false);
        stopButton->setEnabled(true);
        sourceCombo->setEnabled(false);
        statsLabel->setText(media.summary);
        setStatus(text(QStringLiteral("SSTV image transmission is running."),
                       QString::fromUtf8(u8"Передача зображення SSTV працює.")));
        return;
    }

    configuration.sampleRate = activeBackend && activeBackend->isRfCapable()
                                   ? (std::min)(2000000, configuration.deviceSampleRate)
                                   : 500000;
    configuration.liveSource = true;
    const bool fmVideo = requestedMode == QStringLiteral("atv-fm");
    configuration.modulation = fmVideo ? TxModulation::Nfm : TxModulation::Am;
    if (!validateSignalBandwidth(configuration)) return;
    atvFrame = TransmitMediaGenerator::generateAtvFrame(image, configuration, fmVideo);
    if (atvFrame.isEmpty()) {
        setStatus(text(QStringLiteral("Could not generate the analog video frame"),
                       QString::fromUtf8(u8"Не вдалося сформувати кадр аналогового відео")), true);
        return;
    }
    activeConfiguration = configuration;
    QString error;
    if (!activeBackend || !activeBackend->start(configuration, &error)) {
        armCheck->setChecked(false);
        setStatus(error, true);
        return;
    }
    mediaAudio.clear();
    mediaPosition = 0;
    mediaPaused = false;
    plannedDurationSeconds = activeBackend->isRfCapable() ? watchdogSpin->value() : 180.0;
    textSimulation = false;
    elapsedTimer.restart();
    progressTimer->start();
    const int frameMs = (std::max)(1, qRound(1000.0 * atvFrame.size() / configuration.sampleRate));
    sourceTimer->setInterval(frameMs);
    for (int i = 0; i < 6 && activeBackend && activeBackend->isRunning(); ++i) processLiveSource();
    sourceTimer->start();
    startButton->setEnabled(false);
    stopButton->setEnabled(true);
    sourceCombo->setEnabled(false);
    preview->setIq(atvFrame);
    setStatus(text(QStringLiteral("Analog video test frame is being transmitted continuously."),
                   QString::fromUtf8(u8"Тестовий кадр аналогового відео передається циклічно.")));
}

void TransmitDialog::processLiveSource() {
    if (mediaPaused || !activeBackend || !activeBackend->isRunning()) return;
    const QString source = sourceCombo->currentData().toString();
    QString error;
    if (source == QStringLiteral("manual-cw")) {
        if (!liveKeyPressed) return;
        const int count = (std::max)(1, activeConfiguration.sampleRate / 50);
        QVector<std::complex<float>> iq(count);
        const double step = 2.0 * 3.14159265358979323846 * activeConfiguration.toneHz /
                            activeConfiguration.sampleRate;
        for (int i = 0; i < count; ++i) {
            iq[i] = std::polar(activeConfiguration.level, static_cast<float>(manualCwPhase));
            manualCwPhase = std::remainder(manualCwPhase + step, 2.0 * 3.14159265358979323846);
        }
        if (!writeActiveIq(iq.constData(), iq.size(), &error)) stopTransmission(error);
        else preview->setIq(iq);
        return;
    }
    if (source == QStringLiteral("audio-file")) {
        if (mediaPosition >= mediaAudio.size()) {
            stopTransmission(text(QStringLiteral("Audio file transmission complete"),
                                  QString::fromUtf8(u8"Передачу аудіофайлу завершено")));
            return;
        }
        const int count = (std::min)(activeConfiguration.sampleRate / 50,
                                     mediaAudio.size() - mediaPosition);
        QVector<float> chunk(count);
        std::copy_n(mediaAudio.constData() + mediaPosition, count, chunk.data());
        mediaPosition += count;
        const QVector<std::complex<float>> iq =
            TransmitWaveformGenerator::modulateAudio(activeConfiguration, chunk, &modulatorState);
        if (!iq.isEmpty() && !writeActiveIq(iq.constData(), iq.size(), &error)) stopTransmission(error);
        else preview->setIq(iq);
        return;
    }
    if (source == QStringLiteral("image-atv") && !atvFrame.isEmpty()) {
        if (!writeActiveIq(atvFrame.constData(), atvFrame.size(), &error)) stopTransmission(error);
    }
}

void TransmitDialog::setLiveKey(bool pressed) {
    if (pressed == liveKeyPressed || !activeBackend || !activeBackend->isRunning()) return;
    QString error;
    if (!activeBackend->setOutputEnabled(pressed, &error)) {
        stopTransmission(error);
        return;
    }
    liveKeyPressed = pressed;
    if (pressed) bandwidthLimiterState.reset();
    liveKeyButton->setText(pressed
                               ? text(QStringLiteral("TRANSMITTING - release to stop"),
                                      QString::fromUtf8(u8"ПЕРЕДАЧА - відпустіть для зупинки"))
                               : text(QStringLiteral("PTT / CW KEY — hold Space"),
                                      QString::fromUtf8(u8"PTT / КЛЮЧ CW — утримуйте Пробіл")));
    liveKeyButton->setStyleSheet(
        pressed ? QStringLiteral("background:#b3262e;color:white;font-weight:700;")
                : QStringLiteral("background:#218c4c;color:white;font-weight:600;"));
    if (sourceCombo->currentData().toString() == QStringLiteral("manual-cw")) {
        if (pressed) {
            sourceTimer->setInterval(20);
            for (int i = 0; i < 5 && activeBackend && activeBackend->isRunning(); ++i) {
                processLiveSource();
            }
            sourceTimer->start();
        } else {
            sourceTimer->stop();
        }
    }
    setStatus(pressed
                  ? text(QStringLiteral("PTT active"), QString::fromUtf8(u8"PTT активний"))
                  : text(QStringLiteral("PTT released; RF output muted"),
                         QString::fromUtf8(u8"PTT відпущено; RF-вихід заглушено")));
}

void TransmitDialog::chooseMediaFile() {
    const QString source = sourceCombo->currentData().toString();
    const QString filter = source == QStringLiteral("audio-file")
                               ? QStringLiteral("Wave audio (*.wav)")
                               : QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All files (*)");
    const QString path = QFileDialog::getOpenFileName(
        this,
        text(QStringLiteral("Select transmission media"), QString::fromUtf8(u8"Оберіть медіа для передачі")),
        mediaPath.isEmpty() ? QDir::homePath() : QFileInfo(mediaPath).absolutePath(),
        filter);
    if (path.isEmpty()) return;
    mediaPath = path;
    preparedMediaImage = QImage();
    mediaPathLabel->setText(QFileInfo(mediaPath).fileName());
    mediaEditButton->setEnabled(source != QStringLiteral("audio-file"));
    if (source != QStringLiteral("audio-file")) editMediaImage();
    saveSettings();
}

void TransmitDialog::editMediaImage() {
    const QImage sourceImage(mediaPath);
    if (sourceImage.isNull()) {
        setStatus(text(QStringLiteral("Could not open the selected image"),
                       QString::fromUtf8(u8"Не вдалося відкрити вибране зображення")), true);
        return;
    }
    const bool atv = sourceCombo->currentData().toString() == QStringLiteral("image-atv");
    const QString mode = mediaModeCombo->currentData().toString();
    const QSize target = atv ? QSize(384, 288)
                             : QSize(320, mode.startsWith(QStringLiteral("martin-")) ? 256 : 240);
    SstvImageEditorDialog editor(sourceImage, target, language, this);
    if (editor.exec() == QDialog::Accepted) {
        preparedMediaImage = editor.preparedImage();
        setStatus(text(QStringLiteral("Image prepared for %1 x %2 transmission frame"),
                       QString::fromUtf8(u8"Зображення підготовлено до кадру передачі %1 x %2"))
                      .arg(target.width())
                      .arg(target.height()));
    }
}

void TransmitDialog::readMicrophone() {
#ifdef FOBOSAPP_HAS_QT_MULTIMEDIA
    if (!audioDevice || !activeBackend || !activeBackend->isRunning()) return;
    QByteArray bytes = audioDevice->readAll();
    const int samples = bytes.size() / static_cast<int>(sizeof(qint16));
    if (samples <= 0) return;
    if (pttMode && !liveKeyPressed) return;
    QVector<float> audio(samples);
    const auto *pcm = reinterpret_cast<const qint16 *>(bytes.constData());
    for (int i = 0; i < samples; ++i) {
        audio[i] = static_cast<float>(pcm[i]) / 32768.0f;
    }
    const QVector<std::complex<float>> iq =
        TransmitWaveformGenerator::modulateAudio(activeConfiguration, audio, &modulatorState);
    QString error;
    if (!iq.isEmpty() && !writeActiveIq(iq.constData(), iq.size(), &error)) {
        stopTransmission(error);
        return;
    }
    preview->setIq(iq);
    if (activeBackend == &simulator) {
        statsLabel->setText(text(QStringLiteral("Captured %1 IQ samples (%2 s)"),
                                 QString::fromUtf8(u8"Захоплено %1 IQ-семплів (%2 с)"))
                                .arg(simulator.capturedIq().size())
                                .arg(static_cast<double>(simulator.capturedIq().size()) /
                                         simulator.configuration().sampleRate,
                                     0, 'f', 2));
    }
#endif
}

void TransmitDialog::updateProgress() {
    if (!activeBackend || !activeBackend->isRunning()) {
        progressTimer->stop();
        return;
    }
    const double elapsed = elapsedTimer.elapsed() / 1000.0;
    const double denominator = (std::max)(0.001, plannedDurationSeconds);
    const bool audioFile = sourceCombo->currentData().toString() == QStringLiteral("audio-file") &&
                           !mediaAudio.isEmpty();
    const double progress = audioFile
                                ? static_cast<double>(mediaPosition) / mediaAudio.size()
                                : elapsed / denominator;
    progressBar->setValue((std::min)(1000, qRound(1000.0 * progress)));
    if (activeBackend == &hackRf) {
        const double seconds = hackRf.configuration().deviceSampleRate > 0
                                   ? static_cast<double>(hackRf.outputSamples()) /
                                         hackRf.configuration().deviceSampleRate
                                   : 0.0;
        statsLabel->setText(
            text(QStringLiteral("RF: %1 M samples | source buffer: %2 | zero-fill: %3"),
                 QString::fromUtf8(u8"RF: %1 млн семплів | буфер джерела: %2 | нульове заповнення: %3"))
                .arg(hackRf.outputSamples() / 1000000.0, 0, 'f', 2)
                .arg(hackRf.bufferedSourceSamples())
                .arg(hackRf.sourceUnderrunSamples()) +
            QStringLiteral(" | ") + signalShapeSummary(activeConfiguration));
        Q_UNUSED(seconds)
    }
    double safetyLimit = activeBackend->isRfCapable() ? watchdogSpin->value() : 180.0;
    if (textSimulation) {
        // Finite generated signals (notably SSTV) must not be truncated by a
        // shorter generic RF watchdog. Keep a small drain margin after the
        // waveform's measured duration while retaining the watchdog for live TX.
        safetyLimit = (std::max)(safetyLimit, plannedDurationSeconds + 5.0);
    }
    if ((textSimulation && elapsed >= plannedDurationSeconds) || elapsed >= safetyLimit) {
        stopTransmission(textSimulation
                             ? text(activeBackend->isRfCapable() ? QStringLiteral("Transmission complete")
                                                                : QStringLiteral("Simulation complete"),
                                    activeBackend->isRfCapable() ? QString::fromUtf8(u8"Передачу завершено")
                                                                : QString::fromUtf8(u8"Симуляцію завершено"))
                             : text(QStringLiteral("RF safety timer reached"),
                                    QString::fromUtf8(u8"Спрацював таймер безпеки RF")));
    }
}

void TransmitDialog::stopTransmission(const QString &reason) {
    progressTimer->stop();
    sourceTimer->stop();
#ifdef FOBOSAPP_HAS_QT_MULTIMEDIA
    if (audioInput) {
        audioInput->stop();
        audioInput->deleteLater();
        audioInput = nullptr;
        audioDevice = nullptr;
    }
#endif
    const bool hadSamples = activeBackend == &simulator && !simulator.capturedIq().isEmpty();
    if (activeBackend) activeBackend->stop();
    activeBackend = nullptr;
    liveKeyPressed = false;
    pttMode = false;
    bandwidthLimiterState.reset();
    bandwidthLimitedIq.clear();
    mediaPaused = false;
    mediaAudio.clear();
    atvFrame.clear();
    mediaPosition = 0;
    startButton->setEnabled(true);
    stopButton->setEnabled(false);
    sourceCombo->setEnabled(true);
    liveKeyButton->setEnabled(false);
    liveKeyButton->setText(text(QStringLiteral("Start transmission"),
                                QString::fromUtf8(u8"Почати передачу")));
    liveKeyButton->setStyleSheet(
        QStringLiteral("background:#6b7078;color:white;font-weight:600;"));
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
    metadata.insert(QStringLiteral("signal_bandwidth_hz"),
                    simulator.configuration().signalBandwidthHz);
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
    const QString savedAudioInput = settings.value(QStringLiteral("transmit/audioInputDevice")).toString();
    const int savedAudioInputIndex = audioInputCombo->findData(savedAudioInput);
    if (savedAudioInputIndex >= 0) audioInputCombo->setCurrentIndex(savedAudioInputIndex);
    const int sampleRate = settings.value(QStringLiteral("transmit/sampleRate"), 48000).toInt();
    int rateIndex = sampleRateCombo->findData(sampleRate);
    if (rateIndex < 0) {
        sampleRateCombo->addItem(QString::number(sampleRate), sampleRate);
        rateIndex = sampleRateCombo->count() - 1;
    }
    sampleRateCombo->setCurrentIndex(rateIndex);
    levelSpin->setValue(settings.value(QStringLiteral("transmit/levelPercent"), 50).toInt());
    deviationSpin->setValue(settings.value(QStringLiteral("transmit/deviationHz"), 2500.0).toDouble());
    const double savedSignalBandwidth =
        settings.value(QStringLiteral("transmit/signalBandwidthHz"), 0.0).toDouble();
    int signalBandwidthIndex = -1;
    for (int index = 0; index < signalBandwidthCombo->count(); ++index) {
        if (std::abs(signalBandwidthCombo->itemData(index).toDouble() - savedSignalBandwidth) < 0.5) {
            signalBandwidthIndex = index;
            break;
        }
    }
    if (signalBandwidthIndex >= 0) {
        signalBandwidthCombo->setCurrentIndex(signalBandwidthIndex);
    } else {
        signalBandwidthCombo->setCurrentIndex(-1);
        signalBandwidthCombo->setEditText(QStringLiteral("%1 Hz").arg(savedSignalBandwidth, 0, 'f', 0));
    }
    toneSpin->setValue(settings.value(QStringLiteral("transmit/toneHz"), 700.0).toDouble());
    cwWpmSpin->setValue(settings.value(QStringLiteral("transmit/cwWpm"), 18).toInt());
    ft8UtcCheck->setChecked(settings.value(QStringLiteral("transmit/alignFt8Utc"), true).toBool());
    const QString backend = settings.value(QStringLiteral("transmit/backend"),
                                           QStringLiteral("simulator-iq-file")).toString();
    const int backendIndex = backendCombo->findData(backend);
    if (backendIndex >= 0) backendCombo->setCurrentIndex(backendIndex);
    const int deviceIndex = settings.value(QStringLiteral("transmit/hackRfDeviceIndex"), 0).toInt();
    const int deviceComboIndex = deviceCombo->findData(deviceIndex);
    if (deviceComboIndex >= 0) deviceCombo->setCurrentIndex(deviceComboIndex);
    const int deviceRate = settings.value(QStringLiteral("transmit/deviceSampleRate"), 2000000).toInt();
    const int deviceRateIndex = deviceSampleRateCombo->findData(deviceRate);
    if (deviceRateIndex >= 0) deviceSampleRateCombo->setCurrentIndex(deviceRateIndex);
    const uint bandwidth = settings.value(QStringLiteral("transmit/bandwidthHz"), 0).toUInt();
    const int bandwidthIndex = bandwidthCombo->findData(bandwidth);
    if (bandwidthIndex >= 0) bandwidthCombo->setCurrentIndex(bandwidthIndex);
    txGainSpin->setValue(settings.value(QStringLiteral("transmit/txVgaGainDb"), 0).toInt());
    rfAmpCheck->setChecked(settings.value(QStringLiteral("transmit/rfAmp"), false).toBool());
    watchdogSpin->setValue(settings.value(QStringLiteral("transmit/watchdogSeconds"), 30).toInt());
    armCheck->setChecked(false);
    messageEdit->setPlainText(settings.value(QStringLiteral("transmit/message"),
                                             QStringLiteral("CQ TEST KN34")).toString());
    const QString source = settings.value(QStringLiteral("transmit/source"),
                                          QStringLiteral("microphone")).toString();
    const int sourceIndex = sourceCombo->findData(source);
    if (sourceIndex >= 0) sourceCombo->setCurrentIndex(sourceIndex);
    mediaPath = settings.value(QStringLiteral("transmit/mediaPath")).toString();
    const QString mediaMode = settings.value(QStringLiteral("transmit/mediaMode"),
                                             QStringLiteral("robot36")).toString();
    const int mediaModeIndex = mediaModeCombo->findData(mediaMode);
    if (mediaModeIndex >= 0) mediaModeCombo->setCurrentIndex(mediaModeIndex);
    restoreGeometry(settings.value(QStringLiteral("transmit/dialogGeometry")).toByteArray());
}

void TransmitDialog::saveSettings() const {
    QSettings settings(persistentSettingsFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("transmit/frequencyMhz"), frequencySpin->value());
    settings.setValue(QStringLiteral("transmit/modulation"), static_cast<int>(selectedModulation()));
    settings.setValue(QStringLiteral("transmit/sampleRate"), configurationFromUi().sampleRate);
    settings.setValue(QStringLiteral("transmit/audioInputDevice"),
                      audioInputCombo->currentData().toString());
    settings.setValue(QStringLiteral("transmit/levelPercent"), levelSpin->value());
    settings.setValue(QStringLiteral("transmit/deviationHz"), deviationSpin->value());
    settings.setValue(QStringLiteral("transmit/signalBandwidthHz"), signalBandwidthFromUi());
    settings.setValue(QStringLiteral("transmit/toneHz"), toneSpin->value());
    settings.setValue(QStringLiteral("transmit/cwWpm"), cwWpmSpin->value());
    settings.setValue(QStringLiteral("transmit/alignFt8Utc"), ft8UtcCheck->isChecked());
    settings.setValue(QStringLiteral("transmit/backend"), backendCombo->currentData().toString());
    settings.setValue(QStringLiteral("transmit/hackRfDeviceIndex"), deviceCombo->currentData().toInt());
    settings.setValue(QStringLiteral("transmit/deviceSampleRate"), deviceSampleRateCombo->currentData().toInt());
    settings.setValue(QStringLiteral("transmit/bandwidthHz"), bandwidthCombo->currentData().toUInt());
    settings.setValue(QStringLiteral("transmit/txVgaGainDb"), txGainSpin->value());
    settings.setValue(QStringLiteral("transmit/rfAmp"), rfAmpCheck->isChecked());
    settings.setValue(QStringLiteral("transmit/watchdogSeconds"), watchdogSpin->value());
    settings.setValue(QStringLiteral("transmit/message"), messageEdit->toPlainText());
    settings.setValue(QStringLiteral("transmit/source"), sourceCombo->currentData().toString());
    settings.setValue(QStringLiteral("transmit/mediaPath"), mediaPath);
    settings.setValue(QStringLiteral("transmit/mediaMode"), mediaModeCombo->currentData().toString());
    settings.setValue(QStringLiteral("transmit/dialogGeometry"), saveGeometry());
}

void TransmitDialog::closeEvent(QCloseEvent *event) {
    armCheck->setChecked(false);
    stopTransmission();
    saveSettings();
    QDialog::closeEvent(event);
}

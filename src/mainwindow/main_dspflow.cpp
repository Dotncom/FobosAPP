#include "main.h"
#include "multivfowidget.h"

#include "audiofilterchainwidget.h"
#include "dspflowpanel.h"
#include "frequencycontrol.h"
#include "researchanalysisdialog.h"
#include "videowidget.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSerialPort>
#include <QTimer>
#include <QTextCursor>
#include <QVBoxLayout>

#include <functional>
#include <memory>

namespace {

bool focusInside(const QWidget *widget) {
    QWidget *focus = QApplication::focusWidget();
    return widget && focus && (focus == widget || widget->isAncestorOf(focus));
}

bool comboContentsMatch(const QComboBox *source, const QComboBox *copy) {
    if (!source || !copy || source->count() != copy->count()) {
        return false;
    }
    for (int i = 0; i < source->count(); ++i) {
        if (source->itemText(i) != copy->itemText(i) || source->itemData(i) != copy->itemData(i)) {
            return false;
        }
    }
    return true;
}

void copyComboContents(QComboBox *source, QComboBox *copy) {
    if (!source || !copy) {
        return;
    }
    const QSignalBlocker blocker(copy);
    copy->clear();
    for (int i = 0; i < source->count(); ++i) {
        copy->addItem(source->itemIcon(i), source->itemText(i), source->itemData(i));
    }
    copy->setCurrentIndex(source->currentIndex());
}

QComboBox *linkedCombo(QComboBox *source, QWidget *parent, QTimer *syncTimer) {
    auto *copy = new QComboBox(parent);
    copy->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    copyComboContents(source, copy);
    QObject::connect(copy, QOverload<int>::of(&QComboBox::currentIndexChanged), source,
                     [source](int index) {
                         if (source && source->currentIndex() != index) {
                             source->setCurrentIndex(index);
                         }
                     });
    QObject::connect(source, QOverload<int>::of(&QComboBox::currentIndexChanged), copy,
                     [copy](int index) {
                         if (copy && copy->currentIndex() != index) {
                             const QSignalBlocker blocker(copy);
                             copy->setCurrentIndex(index);
                         }
                     });
    QObject::connect(syncTimer, &QTimer::timeout, copy, [source, copy]() {
        if (!source || !copy || focusInside(copy)) {
            return;
        }
        if (!comboContentsMatch(source, copy)) {
            copyComboContents(source, copy);
        } else if (copy->currentIndex() != source->currentIndex()) {
            const QSignalBlocker blocker(copy);
            copy->setCurrentIndex(source->currentIndex());
        }
        copy->setEnabled(source->isEnabled());
    });
    return copy;
}

QWidget *linkedSlider(QSlider *source, QWidget *parent, QTimer *syncTimer,
                      const QString &suffix = QString(), double divisor = 1.0) {
    auto *container = new QWidget(parent);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    auto *copy = new QSlider(source->orientation(), container);
    auto *valueLabel = new QLabel(container);
    valueLabel->setMinimumWidth(54);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    copy->setRange(source->minimum(), source->maximum());
    copy->setSingleStep(source->singleStep());
    copy->setPageStep(source->pageStep());
    copy->setValue(source->value());
    const auto updateLabel = [valueLabel, suffix, divisor](int value) {
        valueLabel->setText(QStringLiteral("%1%2").arg(value / divisor, 0, 'f', divisor == 1.0 ? 0 : 1).arg(suffix));
    };
    updateLabel(copy->value());
    layout->addWidget(copy, 1);
    layout->addWidget(valueLabel);
    QObject::connect(copy, &QSlider::valueChanged, source, [source, updateLabel](int value) {
        updateLabel(value);
        if (source && source->value() != value) {
            source->setValue(value);
        }
    });
    QObject::connect(source, &QSlider::valueChanged, copy, [copy, updateLabel](int value) {
        updateLabel(value);
        if (copy && copy->value() != value) {
            const QSignalBlocker blocker(copy);
            copy->setValue(value);
        }
    });
    QObject::connect(syncTimer, &QTimer::timeout, copy, [source, copy, updateLabel]() {
        if (!source || !copy || focusInside(copy)) {
            return;
        }
        if (copy->minimum() != source->minimum() || copy->maximum() != source->maximum()) {
            const QSignalBlocker blocker(copy);
            copy->setRange(source->minimum(), source->maximum());
        }
        if (copy->value() != source->value()) {
            const QSignalBlocker blocker(copy);
            copy->setValue(source->value());
            updateLabel(source->value());
        }
        copy->setEnabled(source->isEnabled());
    });
    return container;
}

QCheckBox *linkedCheckBox(QCheckBox *source, const QString &text, QWidget *parent, QTimer *syncTimer) {
    auto *copy = new QCheckBox(text, parent);
    copy->setChecked(source->isChecked());
    QObject::connect(copy, &QCheckBox::toggled, source, [source](bool checked) {
        if (source && source->isChecked() != checked) {
            source->setChecked(checked);
        }
    });
    QObject::connect(source, &QCheckBox::toggled, copy, [copy](bool checked) {
        if (copy && copy->isChecked() != checked) {
            const QSignalBlocker blocker(copy);
            copy->setChecked(checked);
        }
    });
    QObject::connect(syncTimer, &QTimer::timeout, copy, [source, copy]() {
        if (!source || !copy || focusInside(copy)) return;
        if (copy->isChecked() != source->isChecked()) {
            const QSignalBlocker blocker(copy);
            copy->setChecked(source->isChecked());
        }
        copy->setEnabled(source->isEnabled());
    });
    return copy;
}

QDoubleSpinBox *linkedDoubleSpin(QDoubleSpinBox *source, QWidget *parent, QTimer *syncTimer) {
    auto *copy = new QDoubleSpinBox(parent);
    copy->setRange(source->minimum(), source->maximum());
    copy->setDecimals(source->decimals());
    copy->setSingleStep(source->singleStep());
    copy->setSuffix(source->suffix());
    copy->setKeyboardTracking(false);
    copy->setValue(source->value());
    QObject::connect(copy, QOverload<double>::of(&QDoubleSpinBox::valueChanged), source,
                     [source](double value) { if (source && source->value() != value) source->setValue(value); });
    QObject::connect(source, QOverload<double>::of(&QDoubleSpinBox::valueChanged), copy,
                     [copy](double value) {
                         if (copy && copy->value() != value) {
                             const QSignalBlocker blocker(copy); copy->setValue(value);
                         }
                     });
    QObject::connect(syncTimer, &QTimer::timeout, copy, [source, copy]() {
        if (!source || !copy || focusInside(copy)) return;
        if (copy->value() != source->value()) {
            const QSignalBlocker blocker(copy); copy->setValue(source->value());
        }
        copy->setEnabled(source->isEnabled());
    });
    return copy;
}

QSpinBox *linkedSpin(QSpinBox *source, QWidget *parent, QTimer *syncTimer) {
    auto *copy = new QSpinBox(parent);
    copy->setRange(source->minimum(), source->maximum());
    copy->setSingleStep(source->singleStep());
    copy->setSuffix(source->suffix());
    copy->setValue(source->value());
    QObject::connect(copy, QOverload<int>::of(&QSpinBox::valueChanged), source,
                     [source](int value) { if (source && source->value() != value) source->setValue(value); });
    QObject::connect(source, QOverload<int>::of(&QSpinBox::valueChanged), copy,
                     [copy](int value) {
                         if (copy && copy->value() != value) {
                             const QSignalBlocker blocker(copy); copy->setValue(value);
                         }
                     });
    QObject::connect(syncTimer, &QTimer::timeout, copy, [source, copy]() {
        if (!source || !copy || focusInside(copy)) return;
        if (copy->value() != source->value()) {
            const QSignalBlocker blocker(copy); copy->setValue(source->value());
        }
        copy->setEnabled(source->isEnabled());
    });
    return copy;
}

QLineEdit *linkedLineEdit(QLineEdit *source, QWidget *parent, QTimer *syncTimer) {
    auto *copy = new QLineEdit(parent);
    copy->setText(source->text());
    copy->setPlaceholderText(source->placeholderText());
    QObject::connect(copy, &QLineEdit::editingFinished, source, [source, copy]() {
        if (source && source->text() != copy->text()) {
            source->setText(copy->text());
            emit source->editingFinished();
        }
    });
    QObject::connect(source, &QLineEdit::textChanged, copy, [copy](const QString &value) {
        if (!focusInside(copy) && copy->text() != value) {
            const QSignalBlocker blocker(copy);
            copy->setText(value);
        }
    });
    QObject::connect(syncTimer, &QTimer::timeout, copy, [source, copy]() {
        if (!source || !copy || focusInside(copy)) return;
        if (copy->text() != source->text()) {
            const QSignalBlocker blocker(copy);
            copy->setText(source->text());
        }
        copy->setEnabled(source->isEnabled());
    });
    return copy;
}

AudioFilterKind filterKindForBlock(const QString &type, bool *ok = nullptr) {
    static const QHash<QString, AudioFilterKind> kinds = {
        {QStringLiteral("filter_low_pass"), AudioFilterKind::LowPass},
        {QStringLiteral("filter_high_pass"), AudioFilterKind::HighPass},
        {QStringLiteral("filter_band_pass"), AudioFilterKind::BandPass},
        {QStringLiteral("filter_notch"), AudioFilterKind::Notch},
        {QStringLiteral("filter_dc_blocker"), AudioFilterKind::DcBlocker},
        {QStringLiteral("filter_de_emphasis"), AudioFilterKind::DeEmphasis},
        {QStringLiteral("filter_parametric_eq"), AudioFilterKind::ParametricEq},
        {QStringLiteral("filter_low_shelf"), AudioFilterKind::LowShelf},
        {QStringLiteral("filter_high_shelf"), AudioFilterKind::HighShelf},
        {QStringLiteral("filter_adaptive_notch"), AudioFilterKind::AdaptiveNotch},
        {QStringLiteral("filter_noise_blanker"), AudioFilterKind::NoiseBlanker},
        {QStringLiteral("filter_cw"), AudioFilterKind::CwFilter},
        {QStringLiteral("filter_ctcss"), AudioFilterKind::CtcssSuppressor},
        {QStringLiteral("filter_spectral_denoise"), AudioFilterKind::SpectralDenoise},
        {QStringLiteral("filter_custom_fir"), AudioFilterKind::CustomFir},
        {QStringLiteral("filter_gain"), AudioFilterKind::Gain},
        {QStringLiteral("filter_compressor"), AudioFilterKind::Compressor},
        {QStringLiteral("filter_limiter"), AudioFilterKind::Limiter},
        {QStringLiteral("filter_noise_gate"), AudioFilterKind::NoiseGate}
    };
    const auto it = kinds.constFind(type);
    if (ok) *ok = it != kinds.constEnd();
    return it == kinds.constEnd() ? AudioFilterKind::LowPass : it.value();
}

} // namespace

void YourClassName::registerDspFilterBlock(const QString &type, const QString &id) {
    bool ok = false;
    const AudioFilterKind kind = filterKindForBlock(type, &ok);
    if (ok && audioFilterChainWidget) {
        audioFilterChainWidget->ensureStage(id, kind);
    }
}

void YourClassName::refreshDspControlStates() {
    if (!dspFlowPanel) return;
    const auto checked = [](const QCheckBox *box) { return box && box->isChecked(); };
    QHash<QString, bool> states;
    states.insert(QStringLiteral("spectrum_display"), checked(spectrumCheckbox));
    states.insert(QStringLiteral("second_spectrum"), checked(graphCheckbox));
    states.insert(QStringLiteral("waterfall_3d"), waterfallDisplayModeCombo &&
                  waterfallDisplayModeCombo->currentData().toInt() !=
                      static_cast<int>(MyWaterfallWidget::DisplayMode::Waterfall2D));
    states.insert(QStringLiteral("audio_output"), checked(audioCheckbox));
    states.insert(QStringLiteral("decoder"), checked(digitalDecodeCheckbox));
    states.insert(QStringLiteral("digital_audio_settings"), checked(digitalDecodeCheckbox));
    states.insert(QStringLiteral("dmr_decoder"), checked(digitalDecodeCheckbox));
    states.insert(QStringLiteral("cw_decoder"), checked(digitalDecodeCheckbox));
    states.insert(QStringLiteral("sstv_decoder"), checked(videoDecodeCheckbox));
    states.insert(QStringLiteral("digital_video"), checked(videoDecodeCheckbox));
    states.insert(QStringLiteral("digital_video_settings"), checked(videoDecodeCheckbox));
    states.insert(QStringLiteral("agile_scan"), checked(agileScanCheckbox));
    states.insert(QStringLiteral("standard_scan"), checked(standardScanCheckbox));
    states.insert(QStringLiteral("listening_scan"), checked(listeningScanCheckbox));
    states.insert(QStringLiteral("spectrum_measurement"), checked(scanMeasurementCheckbox));
    states.insert(QStringLiteral("spur_suppression"), checked(spurSuppressionCheckbox));
    states.insert(QStringLiteral("hf_interference"), checked(hfInterferenceBaselineCheckbox));
    states.insert(QStringLiteral("gnss_sdr"), checked(gnssMonitorCheckbox));
    states.insert(QStringLiteral("gnss_serial"), gnssSerialPort && gnssSerialPort->isOpen());
    dspFlowPanel->setControlStates(runState != RadioRunState::Idle, states);
    dspFlowPanel->setFineTuneRangeHz(fineTuneRangeHz());
}

void YourClassName::triggerDspControlBlock(const QString &controlType,
                                           const QString &controlId,
                                           const QString &targetType,
                                           const QString &targetId) {
    Q_UNUSED(controlId);
    Q_UNUSED(targetId);
    if (controlType == QStringLiteral("run_control")) {
        if (!targetType.isEmpty() && targetType != QStringLiteral("receiver_source") &&
            targetType != QStringLiteral("iq_source") && targetType != QStringLiteral("network_input") &&
            targetType != QStringLiteral("playback")) return;
        if (runState == RadioRunState::Idle) {
            if (startButton && startButton->isEnabled()) startButton->click();
        } else if (runState != RadioRunState::Stopping) {
            if (stopButton && stopButton->isEnabled()) stopButton->click();
        }
        refreshDspControlStates();
        return;
    }
    if (controlType != QStringLiteral("enable_control") || targetType.isEmpty()) return;

    QCheckBox *toggle = nullptr;
    if (targetType == QStringLiteral("spectrum_display")) toggle = spectrumCheckbox;
    else if (targetType == QStringLiteral("second_spectrum")) toggle = graphCheckbox;
    else if (targetType == QStringLiteral("audio_output")) toggle = audioCheckbox;
    else if (targetType == QStringLiteral("decoder") || targetType == QStringLiteral("digital_audio_settings") ||
             targetType == QStringLiteral("dmr_decoder") ||
             targetType == QStringLiteral("cw_decoder")) toggle = digitalDecodeCheckbox;
    else if (targetType == QStringLiteral("sstv_decoder") || targetType == QStringLiteral("digital_video") ||
             targetType == QStringLiteral("digital_video_settings"))
        toggle = videoDecodeCheckbox;
    else if (targetType == QStringLiteral("agile_scan")) toggle = agileScanCheckbox;
    else if (targetType == QStringLiteral("standard_scan")) toggle = standardScanCheckbox;
    else if (targetType == QStringLiteral("listening_scan")) toggle = listeningScanCheckbox;
    else if (targetType == QStringLiteral("spectrum_measurement")) toggle = scanMeasurementCheckbox;
    else if (targetType == QStringLiteral("spur_suppression")) toggle = spurSuppressionCheckbox;
    else if (targetType == QStringLiteral("hf_interference")) toggle = hfInterferenceBaselineCheckbox;
    else if (targetType == QStringLiteral("gnss_sdr")) toggle = gnssMonitorCheckbox;

    if (toggle) {
        if (toggle->isEnabled()) toggle->click();
    } else if (targetType == QStringLiteral("waterfall_3d") && waterfallDisplayModeCombo) {
        const bool active = waterfallDisplayModeCombo->currentData().toInt() !=
                            static_cast<int>(MyWaterfallWidget::DisplayMode::Waterfall2D);
        const int mode = static_cast<int>(active ? MyWaterfallWidget::DisplayMode::Waterfall2D
                                                : MyWaterfallWidget::DisplayMode::Waterfall3D);
        const int index = waterfallDisplayModeCombo->findData(mode);
        if (index >= 0) waterfallDisplayModeCombo->setCurrentIndex(index);
    } else if (targetType == QStringLiteral("gnss_serial") && gnssSerialButton &&
               gnssSerialButton->isEnabled()) {
        gnssSerialButton->click();
    }
    refreshDspControlStates();
}

void YourClassName::openDspBlockReference(const QString &type, const QString &id) {
    const bool ukrainian = normalizedUiLanguage(uiLanguage) == QStringLiteral("uk");
    const auto text = [ukrainian](const char *uk, const char *en) {
        return QString::fromUtf8(ukrainian ? uk : en);
    };

    bool filterBlock = false;
    const AudioFilterKind filterKind = filterKindForBlock(type, &filterBlock);
    if (filterBlock) {
        if (audioFilterChainWidget) {
            audioFilterChainWidget->ensureStage(id, filterKind);
            audioFilterChainWidget->openStageEditor(id);
        }
        return;
    }

    if (type == QStringLiteral("network_output") || type == QStringLiteral("network_input")) {
        openNetworkSettingsDialog();
        return;
    }
    if (type == QStringLiteral("zoom_spectrum")) { openZoomSpectrum(); return; }
    if (type == QStringLiteral("zero_span")) { openZeroSpanDialog(); return; }
    if (type == QStringLiteral("research_analysis")) { openResearchAnalysis(0); return; }
    if (type == QStringLiteral("oscilloscope_view") ||
        type == QStringLiteral("constellation_view") ||
        type == QStringLiteral("eye_diagram_view") ||
        type == QStringLiteral("digital_sync_view")) return;
    if (type == QStringLiteral("spectrum_replay")) { openSpectrumFrameReplay(); return; }
    if (type == QStringLiteral("qth_map")) { openQthMapWindow(); return; }
    if (type == QStringLiteral("transmitter")) { openTransmitDialog(); return; }
    if (type == QStringLiteral("presets")) { openPresetManager(); return; }
    if (type == QStringLiteral("calibration")) { openPresetManager(); return; }
    if (type == QStringLiteral("application_settings")) { openApplicationSettings(); return; }
    const QString editorKey = id.isEmpty() ? type : QStringLiteral("%1:%2").arg(type, id);
    if (QDialog *existing = dspBlockEditors.value(editorKey).data()) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }

    if (type == QStringLiteral("fine_tune_control")) return;
    if (type == QStringLiteral("digital_text_output")) {
        auto *dialog = new QDialog(this, Qt::Tool);
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        dialog->setModal(false);
        dialog->setWindowTitle(text("Цифровий текст", "Digital text"));
        dialog->resize(520, 300);
        auto *layout = new QVBoxLayout(dialog);
        auto *output = new QPlainTextEdit(dialog);
        output->setObjectName(QStringLiteral("dspDigitalTextOutput"));
        output->setReadOnly(true);
        output->setMaximumBlockCount(2000);
        if (digitalTextEdit) output->setPlainText(digitalTextEdit->toPlainText());
        auto *clearButton = new QPushButton(text("Очистити", "Clear"), dialog);
        layout->addWidget(output, 1);
        layout->addWidget(clearButton, 0, Qt::AlignRight);
        connect(clearButton, &QPushButton::clicked, output, &QPlainTextEdit::clear);
        if (digitalDecoder) {
            connect(digitalDecoder, &DigitalDecoder::textDecoded, output,
                    [output](const QString &decoded) {
                        if (decoded.isEmpty()) return;
                        QTextCursor cursor = output->textCursor();
                        cursor.movePosition(QTextCursor::End);
                        cursor.insertText(decoded);
                        output->setTextCursor(cursor);
                        output->ensureCursorVisible();
                    },
                    Qt::QueuedConnection);
        }
        dspBlockEditors.insert(editorKey, dialog);
        connect(dialog, &QObject::destroyed, this, [this, editorKey]() {
            dspBlockEditors.remove(editorKey);
        });
        dialog->show();
        dialog->raise();
        return;
    }
    if (type == QStringLiteral("digital_image_output")) {
        auto *dialog = new QDialog(this, Qt::Tool);
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        dialog->setModal(false);
        dialog->setWindowTitle(text("Цифрове зображення", "Digital image"));
        dialog->resize(560, 430);
        auto *layout = new QVBoxLayout(dialog);
        auto *status = new QLabel(text("Очікування кадру", "Waiting for frame"), dialog);
        status->setWordWrap(true);
        auto *output = new VideoWidget(dialog);
        output->setObjectName(QStringLiteral("dspDigitalImageOutput"));
        layout->addWidget(status);
        layout->addWidget(output, 1);
        if (videoProcessor) {
            connect(videoProcessor, &VideoProcessor::frameReady,
                    output, &VideoWidget::setFrame, Qt::QueuedConnection);
            connect(videoProcessor, &VideoProcessor::statusChanged,
                    status, &QLabel::setText, Qt::QueuedConnection);
        }
        dspBlockEditors.insert(editorKey, dialog);
        connect(dialog, &QObject::destroyed, this, [this, editorKey]() {
            dspBlockEditors.remove(editorKey);
            QTimer::singleShot(0, this, [this]() { updateVideoProcessorMode(); });
        });
        dialog->show();
        dialog->raise();
        updateVideoProcessorMode();
        return;
    }

    auto *dialog = new QDialog(this, Qt::Tool);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setModal(false);
    dialog->setMinimumWidth(370);
    auto *root = new QVBoxLayout(dialog);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);
    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(7);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    root->addLayout(form);
    auto *syncTimer = new QTimer(dialog);
    syncTimer->setInterval(180);
    syncTimer->start();

    const auto addFrequencyControl = [this, dialog, syncTimer](FrequencyControl *source,
                                                               double minimum,
                                                               double maximum,
                                                               const std::function<void()> &apply) {
        auto *copy = new FrequencyControl(dialog);
        copy->setRangeHz(minimum, maximum);
        copy->setValueHz(source->valueHz());
        copy->setSelectedUnitIndex(source->selectedUnitIndex());
        copy->setSelectedStepName(source->selectedStepName());
        connect(copy, &FrequencyControl::valueCommitted, dialog,
                [source, copy, apply](double value) {
                    source->setSelectedUnitIndex(copy->selectedUnitIndex());
                    source->setSelectedStepName(copy->selectedStepName());
                    source->setValueHz(value);
                    apply();
                });
        connect(source, &FrequencyControl::valueCommitted, copy,
                [copy](double value) { copy->setValueHz(value); });
        connect(syncTimer, &QTimer::timeout, copy, [source, copy]() {
            if (!focusInside(copy) && !qFuzzyCompare(copy->valueHz() + 1.0, source->valueHz() + 1.0)) {
                copy->setValueHz(source->valueHz());
            }
            copy->setEnabled(source->isEnabled());
        });
        return copy;
    };

    const auto actionButton = [this, dialog](QPushButton *source, const QString &caption) {
        auto *button = new QPushButton(caption, dialog);
        if (source) QObject::connect(button, &QPushButton::clicked, source, &QPushButton::click);
        else button->setEnabled(false);
        return button;
    };
    const auto commandButton = [this, dialog](const QString &caption, const std::function<void()> &command) {
        auto *button = new QPushButton(caption, dialog);
        QObject::connect(button, &QPushButton::clicked, dialog, command);
        return button;
    };
    const auto showDockButton = [this, dialog](QDockWidget *dock, const QString &caption) {
        auto *button = new QPushButton(caption, dialog);
        QObject::connect(button, &QPushButton::clicked, dialog, [dock]() {
            if (!dock) return;
            dock->show();
            dock->raise();
        });
        return button;
    };
    const auto buttonRow = [dialog](std::initializer_list<QPushButton*> buttons) {
        auto *container = new QWidget(dialog);
        auto *layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(5);
        for (QPushButton *button : buttons) layout->addWidget(button);
        return container;
    };

    QString title;
    if (type == QStringLiteral("oscilloscope") || type == QStringLiteral("constellation") ||
        type == QStringLiteral("eye_diagram") || type == QStringLiteral("digital_sync_lab")) {
        title = type == QStringLiteral("oscilloscope")
                    ? text("Осцилограф: параметри", "Oscilloscope settings")
                    : (type == QStringLiteral("constellation")
                           ? text("Сузір'я: параметри", "Constellation settings")
                           : (type == QStringLiteral("eye_diagram")
                                  ? text("Окова діаграма: параметри", "Eye diagram settings")
                                  : text("Синхронізація: параметри", "Synchronization settings")));
        QJsonObject settings = dspFlowPanel ? dspFlowPanel->blockSettings(id) : QJsonObject();
        auto *samples = new QSpinBox(dialog);
        samples->setRange(256, 262144);
        samples->setSingleStep(256);
        samples->setValue(settings.value(QStringLiteral("samples")).toInt(4096));
        auto *refresh = new QSpinBox(dialog);
        refresh->setRange(10, 1000);
        refresh->setSuffix(QStringLiteral(" ms"));
        refresh->setValue(settings.value(QStringLiteral("refreshMs")).toInt(40));
        auto *gain = new QDoubleSpinBox(dialog);
        gain->setRange(0.1, 20.0);
        gain->setDecimals(2);
        gain->setSingleStep(0.1);
        gain->setSuffix(QStringLiteral(" x"));
        gain->setValue(settings.value(QStringLiteral("gain")).toDouble(1.0));
        form->addRow(text("Відліки:", "Samples:"), samples);
        form->addRow(text("Оновлення:", "Refresh:"), refresh);
        form->addRow(text("Масштаб Y:", "Y scale:"), gain);

        QDoubleSpinBox *symbolRate = nullptr;
        QDoubleSpinBox *phase = nullptr;
        QSpinBox *traces = nullptr;
        if (type == QStringLiteral("eye_diagram") || type == QStringLiteral("digital_sync_lab")) {
            symbolRate = new QDoubleSpinBox(dialog);
            symbolRate->setRange(1.0, 5000000.0);
            symbolRate->setDecimals(1);
            symbolRate->setSuffix(QStringLiteral(" Bd"));
            symbolRate->setValue(settings.value(QStringLiteral("symbolRate")).toDouble(4800.0));
            form->addRow(text("Символьна швидкість:", "Symbol rate:"), symbolRate);
        }
        if (type == QStringLiteral("eye_diagram")) {
            phase = new QDoubleSpinBox(dialog);
            phase->setRange(0.0, 100.0);
            phase->setDecimals(1);
            phase->setSuffix(QStringLiteral(" %"));
            phase->setValue(settings.value(QStringLiteral("phasePercent")).toDouble(50.0));
            traces = new QSpinBox(dialog);
            traces->setRange(1, 256);
            traces->setValue(settings.value(QStringLiteral("traces")).toInt(32));
            form->addRow(text("Фаза:", "Phase:"), phase);
            form->addRow(text("Траси:", "Traces:"), traces);
        }

        const auto applyResearchSettings = [this, id, samples, refresh, gain, symbolRate, phase, traces]() {
            if (!dspFlowPanel) return;
            QJsonObject value;
            value.insert(QStringLiteral("samples"), samples->value());
            value.insert(QStringLiteral("refreshMs"), refresh->value());
            value.insert(QStringLiteral("gain"), gain->value());
            if (symbolRate) value.insert(QStringLiteral("symbolRate"), symbolRate->value());
            if (phase) value.insert(QStringLiteral("phasePercent"), phase->value());
            if (traces) value.insert(QStringLiteral("traces"), traces->value());
            dspFlowPanel->setBlockSettings(id, value);
        };
        connect(samples, QOverload<int>::of(&QSpinBox::valueChanged), dialog,
                [applyResearchSettings](int) { applyResearchSettings(); });
        connect(refresh, QOverload<int>::of(&QSpinBox::valueChanged), dialog,
                [applyResearchSettings](int) { applyResearchSettings(); });
        connect(gain, QOverload<double>::of(&QDoubleSpinBox::valueChanged), dialog,
                [applyResearchSettings](double) { applyResearchSettings(); });
        if (symbolRate) connect(symbolRate, QOverload<double>::of(&QDoubleSpinBox::valueChanged), dialog,
                                [applyResearchSettings](double) { applyResearchSettings(); });
        if (phase) connect(phase, QOverload<double>::of(&QDoubleSpinBox::valueChanged), dialog,
                           [applyResearchSettings](double) { applyResearchSettings(); });
        if (traces) connect(traces, QOverload<int>::of(&QSpinBox::valueChanged), dialog,
                            [applyResearchSettings](int) { applyResearchSettings(); });

        auto *fullAnalysis = new QPushButton(text("Повне вікно аналізу", "Open full analysis"), dialog);
        connect(fullAnalysis, &QPushButton::clicked, dialog, [this, type]() {
            const bool synchronization = type == QStringLiteral("digital_sync_lab");
            openResearchAnalysis(static_cast<int>(synchronization
                ? ResearchAnalysisDialog::SynchronizationTab : ResearchAnalysisDialog::IqTab));
            if (!synchronization && researchAnalysisDialog) {
                const int viewMode = type == QStringLiteral("oscilloscope")
                                         ? 1
                                         : (type == QStringLiteral("constellation") ? 2 : 3);
                researchAnalysisDialog->setIqView(viewMode);
            }
        });
        root->addWidget(fullAnalysis);
    } else if (type == QStringLiteral("receiver_source") || type == QStringLiteral("iq_source")) {
        title = text("Вхід приймача", "Receiver input");
        form->addRow(text("Приймач:", "Receiver:"), linkedCombo(comboBox, dialog, syncTimer));
        form->addRow(text("Вхід / режим:", "Input / mode:"), linkedCombo(modeBox, dialog, syncTimer));
        form->addRow(text("Джерело такту:", "Clock source:"), linkedCombo(clkBox, dialog, syncTimer));
        form->addRow(text("Sample rate:", "Sample rate:"), linkedCombo(sampleBox, dialog, syncTimer));
        form->addRow(QStringLiteral("FFT:"), linkedCombo(fftComboBox, dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(fftBinWidthModeCheckbox, QStringLiteral("Hz/point"), dialog, syncTimer));
        form->addRow(QStringLiteral("Hz/point:"), linkedDoubleSpin(fftBinWidthSpin, dialog, syncTimer));
        form->addRow(QStringLiteral("LNA:"), linkedSlider(lnaGainSlider, dialog, syncTimer));
        form->addRow(QStringLiteral("VGA:"), linkedSlider(vgaGainSlider, dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(rtlAgcCheckbox, QStringLiteral("RTL AGC"), dialog, syncTimer));
        form->addRow(QStringLiteral("RTL gain:"), linkedSlider(rtlGainSlider, dialog, syncTimer, QStringLiteral(" dB"), 10.0));
        auto *refresh = new QPushButton(text("Оновити список пристроїв", "Refresh device list"), dialog);
        connect(refresh, &QPushButton::clicked, refreshButton, &QPushButton::click);
        form->addRow(QString(), refresh);
        form->addRow(QString(), buttonRow({actionButton(startButton, text("Старт", "Start")),
                                          actionButton(stopButton, text("Стоп", "Stop"))}));
    } else if (type == QStringLiteral("frequency_control")) {
        title = text("Частоти", "Frequencies");
        form->addRow(text("Центральна:", "Center:"),
                     addFrequencyControl(frequencyControl, 0.0, RF_EXPERIMENTAL_MAX_FREQUENCY,
                                         [this]() { onFrequencyEntered(); }));
        form->addRow(text("Прослуховування:", "Listening:"),
                     addFrequencyControl(listeningFrequencyControl, 0.0, RF_EXPERIMENTAL_MAX_FREQUENCY,
                                         [this]() { onListeningFrequencyEntered(); }));
    } else if (type == QStringLiteral("fft")) {
        title = text("FFT і роздільність", "FFT and resolution");
        form->addRow(QStringLiteral("FFT:"), linkedCombo(fftComboBox, dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(fftBinWidthModeCheckbox, QStringLiteral("Hz/point"), dialog, syncTimer));
        form->addRow(QStringLiteral("Hz/point:"), linkedDoubleSpin(fftBinWidthSpin, dialog, syncTimer));
    } else if (type == QStringLiteral("hf_interference")) {
        title = text("HF лабораторія завад", "HF interference lab");
        form->addRow(QString(), linkedCheckBox(hfNoiseCancelFreezeCheckbox,
                                               text("Заморозити оцінку", "Freeze estimate"), dialog, syncTimer));
        form->addRow(text("Глибина придушення:", "Cancellation depth:"),
                     linkedSlider(hfNoiseCancelDepthSlider, dialog, syncTimer));
        form->addRow(text("Підсилення опори:", "Reference gain:"),
                     linkedSlider(hfNoiseCancelRefGainSlider, dialog, syncTimer));
        form->addRow(text("Затримка опори:", "Reference delay:"),
                     linkedSlider(hfNoiseCancelRefDelaySlider, dialog, syncTimer));
        form->addRow(text("Нахил опори:", "Reference tilt:"),
                     linkedSlider(hfNoiseCancelRefTiltSlider, dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(hfAudioBlankerCheckbox,
                                               text("Аудіо blanker", "Audio blanker"), dialog, syncTimer));
        form->addRow(text("Поріг blanker:", "Blanker threshold:"),
                     linkedSlider(hfAudioBlankerThresholdSlider, dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(hfInterferenceBaselineCheckbox,
                                               text("Віднімати baseline", "Subtract baseline"), dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(hfInterferenceRawOverlayCheckbox,
                                               text("Показати сирий спектр", "Show raw spectrum"), dialog, syncTimer));
        form->addRow(text("Глибина baseline:", "Baseline depth:"),
                     linkedSlider(hfInterferenceBaselineDepthSlider, dialog, syncTimer));
        form->addRow(text("Згладжування:", "Smoothing:"),
                     linkedSlider(hfInterferenceBaselineSmoothSlider, dialog, syncTimer));
        form->addRow(QString(), buttonRow({actionButton(hfInterferenceBaselineLearnButton, text("Навчити", "Learn")),
                                          actionButton(hfInterferenceBaselineClearButton, text("Очистити", "Clear")),
                                          actionButton(hfInterferenceDefaultsButton, text("Типові", "Defaults"))}));
        form->addRow(QString(), actionButton(hfInterferenceAnalyzeButton,
                                             text("Відкрити аналіз", "Open analysis")));
    } else if (type == QStringLiteral("resampler")) {
        title = text("Sample rate / ресемплер", "Sample rate / resampler");
        form->addRow(text("Sample rate:", "Sample rate:"), linkedCombo(sampleBox, dialog, syncTimer));
    } else if (type == QStringLiteral("vfo_spectrum") || type == QStringLiteral("vfo_waterfall")) {
        title = type == QStringLiteral("vfo_spectrum")
                    ? text("Спектр VFO", "VFO spectrum")
                    : text("Водоспад VFO", "VFO waterfall");
        auto *vfoSelector = new QComboBox(dialog);
        const auto rebuildSelector = [this, vfoSelector, id, ukrainian]() {
            const int selectedIndex = dspFlowPanel ? dspFlowPanel->vfoIndexForBlock(id) : -1;
            const QSignalBlocker blocker(vfoSelector);
            vfoSelector->clear();
            const QJsonDocument document = QJsonDocument::fromJson(
                multiVfoWidget ? multiVfoWidget->configurationJson().toUtf8() : QByteArray());
            const QJsonArray channels = document.object().value(QStringLiteral("channels")).toArray();
            for (int channelIndex = 0; channelIndex < channels.size(); ++channelIndex) {
                const QJsonObject channel = channels.at(channelIndex).toObject();
                const QString name = channel.value(QStringLiteral("name"))
                                         .toString(QStringLiteral("VFO %1").arg(channelIndex + 1));
                const double frequencyMHz =
                    channel.value(QStringLiteral("frequencyHz")).toDouble() / 1.0e6;
                vfoSelector->addItem(
                    QStringLiteral("%1 - %2 - %3 MHz")
                        .arg(channelIndex + 1)
                        .arg(name)
                        .arg(frequencyMHz, 0, 'f', 6),
                    channelIndex);
            }
            if (vfoSelector->count() == 0) {
                vfoSelector->addItem(
                    ukrainian ? QStringLiteral("Немає каналів Multi-VFO")
                              : QStringLiteral("No Multi-VFO channels"),
                    -1);
                vfoSelector->setEnabled(false);
                return;
            }
            vfoSelector->setEnabled(true);
            const int comboIndex = vfoSelector->findData(selectedIndex);
            vfoSelector->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
        };
        rebuildSelector();
        form->addRow(text("Канал Multi-VFO:", "Multi-VFO channel:"), vfoSelector);
        connect(vfoSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog,
                [this, vfoSelector, id](int) {
                    if (!dspFlowPanel) return;
                    const int channelIndex = vfoSelector->currentData().toInt();
                    if (channelIndex >= 0) dspFlowPanel->assignVfoIndexToBlock(id, channelIndex);
                });
        if (multiVfoWidget) {
            connect(multiVfoWidget, &MultiVfoWidget::configurationChanged, dialog,
                    [rebuildSelector](const QString &) { rebuildSelector(); });
        }

        const QJsonObject blockSettings =
            dspFlowPanel ? dspFlowPanel->blockSettings(id) : QJsonObject();
        auto *minimumSlider = new QSlider(Qt::Horizontal, dialog);
        auto *maximumSlider = new QSlider(Qt::Horizontal, dialog);
        auto *minimumLabel = new QLabel(dialog);
        auto *maximumLabel = new QLabel(dialog);
        minimumSlider->setRange(-200, 19);
        maximumSlider->setRange(-199, 20);
        minimumSlider->setValue(blockSettings.value(QStringLiteral("minimumDbfs")).toInt(-140));
        maximumSlider->setValue(blockSettings.value(QStringLiteral("maximumDbfs")).toInt(-40));
        if (minimumSlider->value() >= maximumSlider->value()) {
            minimumSlider->setValue((std::max)(minimumSlider->minimum(),
                                               maximumSlider->value() - 1));
        }
        const auto sliderRow = [dialog](QSlider *slider, QLabel *label) {
            auto *container = new QWidget(dialog);
            auto *layout = new QHBoxLayout(container);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(6);
            label->setMinimumWidth(62);
            label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            layout->addWidget(slider, 1);
            layout->addWidget(label);
            return container;
        };
        const auto updateLabels = [minimumSlider, maximumSlider, minimumLabel, maximumLabel]() {
            minimumLabel->setText(QStringLiteral("%1 dBFS").arg(minimumSlider->value()));
            maximumLabel->setText(QStringLiteral("%1 dBFS").arg(maximumSlider->value()));
        };
        const auto applyRange = [this, id, minimumSlider, maximumSlider]() {
            if (!dspFlowPanel) return;
            QJsonObject settings = dspFlowPanel->blockSettings(id);
            settings.insert(QStringLiteral("minimumDbfs"), minimumSlider->value());
            settings.insert(QStringLiteral("maximumDbfs"), maximumSlider->value());
            dspFlowPanel->setBlockSettings(id, settings);
        };
        form->addRow(text("Мінімум:", "Minimum:"), sliderRow(minimumSlider, minimumLabel));
        form->addRow(text("Максимум:", "Maximum:"), sliderRow(maximumSlider, maximumLabel));
        connect(minimumSlider, &QSlider::valueChanged, dialog,
                [minimumSlider, maximumSlider, updateLabels, applyRange](int value) {
                    if (value >= maximumSlider->value()) {
                        maximumSlider->setValue((std::min)(maximumSlider->maximum(), value + 1));
                    }
                    updateLabels();
                    applyRange();
                });
        connect(maximumSlider, &QSlider::valueChanged, dialog,
                [minimumSlider, maximumSlider, updateLabels, applyRange](int value) {
                    if (value <= minimumSlider->value()) {
                        minimumSlider->setValue((std::max)(minimumSlider->minimum(), value - 1));
                    }
                    updateLabels();
                    applyRange();
                });
        updateLabels();
        dialog->setMinimumWidth(390);
    } else if (type == QStringLiteral("multi_vfo_channelizer") ||
               type == QStringLiteral("vfo_channel")) {
        title = type == QStringLiteral("multi_vfo_channelizer")
                    ? text("Multi-VFO каналайзер", "Multi-VFO channelizer")
                    : text("Канал VFO", "VFO channel");
        QComboBox *vfoSelector = nullptr;
        if (type != QStringLiteral("multi_vfo_channelizer")) {
            vfoSelector = new QComboBox(dialog);
            const auto rebuildSelector = [this, vfoSelector, id, ukrainian]() {
                const int selectedIndex = dspFlowPanel ? dspFlowPanel->vfoIndexForBlock(id) : -1;
                const QSignalBlocker blocker(vfoSelector);
                vfoSelector->clear();
                const QJsonDocument document = QJsonDocument::fromJson(
                    multiVfoWidget ? multiVfoWidget->configurationJson().toUtf8() : QByteArray());
                const QJsonArray channels = document.object().value(QStringLiteral("channels")).toArray();
                for (int channelIndex = 0; channelIndex < channels.size(); ++channelIndex) {
                    const QJsonObject channel = channels.at(channelIndex).toObject();
                    const QString name = channel.value(QStringLiteral("name"))
                                             .toString(QStringLiteral("VFO %1").arg(channelIndex + 1));
                    const double frequencyMHz =
                        channel.value(QStringLiteral("frequencyHz")).toDouble() / 1.0e6;
                    vfoSelector->addItem(
                        QStringLiteral("%1 - %2 - %3 MHz")
                            .arg(channelIndex + 1)
                            .arg(name)
                            .arg(frequencyMHz, 0, 'f', 6),
                        channelIndex);
                }
                if (vfoSelector->count() == 0) {
                    vfoSelector->addItem(
                        ukrainian ? QStringLiteral("Немає каналів Multi-VFO")
                                  : QStringLiteral("No Multi-VFO channels"),
                        -1);
                    vfoSelector->setEnabled(false);
                    return;
                }
                vfoSelector->setEnabled(true);
                const int comboIndex = vfoSelector->findData(selectedIndex);
                vfoSelector->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
            };
            rebuildSelector();
            form->addRow(text("Канал Multi-VFO:", "Multi-VFO channel:"), vfoSelector);
            connect(vfoSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog,
                    [this, vfoSelector, id](int) {
                        if (!dspFlowPanel) return;
                        const int channelIndex = vfoSelector->currentData().toInt();
                        if (channelIndex >= 0) dspFlowPanel->assignVfoIndexToBlock(id, channelIndex);
                    });
            if (multiVfoWidget) {
                connect(multiVfoWidget, &MultiVfoWidget::configurationChanged, dialog,
                        [rebuildSelector](const QString &) { rebuildSelector(); });
            }
        }
        auto *multiVfo = new MultiVfoWidget(dialog);
        multiVfo->setLanguage(ukrainian);
        multiVfo->setReceiverContext(pendingSettings.centerFrequency,
                                     pendingSettings.sampleRate,
                                     pendingSettings.listeningFrequency,
                                     pendingSettings.bandwidth,
                                     pendingSettings.modulationType);
        if (multiVfoWidget) multiVfo->setConfigurationJson(multiVfoWidget->configurationJson());
        root->addWidget(multiVfo, 1);
        dialog->resize(type == QStringLiteral("vfo_channel") ? 760 : 940, 360);
        const auto syncing = std::make_shared<bool>(false);
        connect(multiVfo, &MultiVfoWidget::configurationChanged, dialog,
                [this, multiVfo, syncing](const QString &json) {
                    if (*syncing || !multiVfoWidget) return;
                    *syncing = true;
                    multiVfoWidget->setConfigurationJson(json);
                    savePersistentSettings();
                    *syncing = false;
                });
        if (multiVfoWidget) {
            connect(multiVfoWidget, &MultiVfoWidget::configurationChanged, multiVfo,
                    [multiVfo, syncing](const QString &json) {
                        if (*syncing || multiVfo->configurationJson() == json) return;
                        *syncing = true;
                        multiVfo->setConfigurationJson(json);
                        *syncing = false;
                    });
        }
        connect(multiVfo, &MultiVfoWidget::monitorRequested, dialog,
                [this](double frequencyHz, double bandwidthHz, int modulationType) {
                    if (pendingSettings.modulationType != modulationType) onModulationChanged(modulationType);
                    pendingSettings.bandwidth = (std::clamp)(bandwidthHz, 10.0, 20000000.0);
                    if (bandwidthControl) bandwidthControl->setValueHz(pendingSettings.bandwidth);
                    updateTuningFromScale(frequencyHz, pendingSettings.centerFrequency);
                });
        dialog->setMinimumSize(700, 320);
    } else if (type == QStringLiteral("channel_filter")) {
        title = text("Канальний фільтр", "Channel filter");
        form->addRow(text("Смуга:", "Bandwidth:"),
                     addFrequencyControl(bandwidthControl, 10.0, 20000000.0,
                                         [this]() { onBandwidthChanged(); }));
    } else if (type == QStringLiteral("demodulator")) {
        title = text("Демодулятор", "Demodulator");
        auto *modulation = new QComboBox(dialog);
        const QStringList names = {QStringLiteral("AM"), QStringLiteral("NFM"), QStringLiteral("SAM"),
                                   QStringLiteral("USB"), QStringLiteral("LSB"), QStringLiteral("DSB"),
                                   QStringLiteral("CW"), QStringLiteral("WFM")};
        const QVector<int> ids = {MOD_AM, MOD_NFM, MOD_SAM, MOD_USB, MOD_LSB, MOD_DSB, MOD_CW, MOD_WFM};
        for (int i = 0; i < ids.size(); ++i) modulation->addItem(names.at(i), ids.at(i));
        modulation->setCurrentIndex((std::max)(0, modulation->findData(pendingSettings.modulationType)));
        connect(modulation, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog,
                [this, modulation](int index) {
                    const int mode = modulation->itemData(index).toInt();
                    if (modulationButtonGroup) {
                        if (QAbstractButton *button = modulationButtonGroup->button(mode)) {
                            const QSignalBlocker blocker(modulationButtonGroup);
                            button->setChecked(true);
                        }
                    }
                    onModulationChanged(mode);
                });
        connect(syncTimer, &QTimer::timeout, modulation, [this, modulation]() {
            if (focusInside(modulation)) return;
            const int index = modulation->findData(pendingSettings.modulationType);
            if (index >= 0 && index != modulation->currentIndex()) {
                const QSignalBlocker blocker(modulation); modulation->setCurrentIndex(index);
            }
        });
        form->addRow(text("Модуляція:", "Modulation:"), modulation);
        form->addRow(text("Смуга:", "Bandwidth:"),
                     addFrequencyControl(bandwidthControl, 10.0, 20000000.0,
                                         [this]() { onBandwidthChanged(); }));
    } else if (type == QStringLiteral("audio_filter")) {
        title = text("Ланцюг аудіофільтрів", "Audio filter chain");
        auto *chain = new AudioFilterChainWidget(dialog);
        chain->setLanguage(ukrainian);
        chain->setConfigurationJson(audioFilterChainWidget->configurationJson());
        root->addWidget(chain, 1);
        dialog->resize(620, 390);
        const auto syncing = std::make_shared<bool>(false);
        connect(chain, &AudioFilterChainWidget::configurationChanged, dialog,
                [this, chain, syncing](const QString &json) {
                    if (*syncing) return;
                    *syncing = true;
                    audioFilterChainWidget->setConfigurationJson(json);
                    pendingSettings.audioFilterChainJson = json;
                    if (audioProcessor) audioProcessor->configure(audioProcessorSettings());
                    savePersistentSettings();
                    if (isNetworkClientMode()) scheduleRemoteSettingsCommand();
                    *syncing = false;
                });
        connect(audioFilterChainWidget, &AudioFilterChainWidget::configurationChanged, chain,
                [chain, syncing](const QString &json) {
                    if (*syncing || chain->configurationJson() == json) return;
                    *syncing = true;
                    chain->setConfigurationJson(json);
                    *syncing = false;
                });
        connect(syncTimer, &QTimer::timeout, chain, [this, chain, syncing]() {
            if (*syncing || focusInside(chain)) return;
            const QString json = audioFilterChainWidget->configurationJson();
            if (chain->configurationJson() != json) {
                *syncing = true;
                chain->setConfigurationJson(json);
                *syncing = false;
            }
        });
        dialog->setMinimumSize(620, 390);
    } else if (type == QStringLiteral("spectrum_display")) {
        title = text("Відображення спектра", "Spectrum display");
        form->addRow(QString(), linkedCheckBox(spectrumCheckbox, text("Спектр", "Spectrum"), dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(colorCheckbox, text("Кольоровий спектр", "Colored spectrum"), dialog, syncTimer));
        form->addRow(text("Мінімум:", "Minimum:"), linkedSlider(levelMinSlider, dialog, syncTimer));
        form->addRow(text("Максимум:", "Maximum:"), linkedSlider(levelMaxSlider, dialog, syncTimer));
        form->addRow(text("Чутливість:", "Sensitivity:"), linkedSlider(sensitivitySlider, dialog, syncTimer));
        form->addRow(text("Контраст:", "Contrast:"), linkedSlider(contrastSlider, dialog, syncTimer));
        form->addRow(text("Масштаб:", "Scale:"), linkedSlider(scaleSlider, dialog, syncTimer));
        form->addRow(text("Дод. масштаб:", "Extra scale:"), linkedSlider(additionalScaleDivisorSlider, dialog, syncTimer));
    } else if (type == QStringLiteral("waterfall_2d")) {
        title = text("Водоспад 2D", "2D waterfall");
        form->addRow(text("Режим:", "Mode:"), linkedCombo(waterfallDisplayModeCombo, dialog, syncTimer));
        form->addRow(text("Мінімум:", "Minimum:"), linkedSlider(levelMinSlider, dialog, syncTimer));
        form->addRow(text("Максимум:", "Maximum:"), linkedSlider(levelMaxSlider, dialog, syncTimer));
        form->addRow(text("Чутливість:", "Sensitivity:"), linkedSlider(sensitivitySlider, dialog, syncTimer));
        form->addRow(text("Контраст:", "Contrast:"), linkedSlider(contrastSlider, dialog, syncTimer));
    } else if (type == QStringLiteral("waterfall_3d")) {
        title = text("Водоспад 3D", "3D waterfall");
        form->addRow(text("Режим:", "Mode:"), linkedCombo(waterfallDisplayModeCombo, dialog, syncTimer));
        form->addRow(text("Роздільність:", "Resolution:"), linkedCombo(waterfall3DResolutionCombo, dialog, syncTimer));
        form->addRow(text("Пам'ять:", "Memory:"), linkedSpin(waterfall3DHistoryRowsSpin, dialog, syncTimer));
        form->addRow(text("Крок частотного зрізу:", "Frequency slice step:"), linkedSpin(waterfall3DSliceStepSpin, dialog, syncTimer));
        form->addRow(text("Ширина зрізу:", "Slice width:"), linkedSpin(waterfall3DSliceWidthSpin, dialog, syncTimer));
        form->addRow(text("Крок часового зрізу:", "Time slice step:"), linkedSpin(waterfall3DSpectrumSliceStepSpin, dialog, syncTimer));
        form->addRow(text("Рядки зрізу:", "Slice rows:"), linkedSpin(waterfall3DSpectrumSliceRowsSpin, dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(waterfall3DSpectrumSliceCaptureCheckbox,
                                               text("Захват", "Capture"), dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(waterfall3DSpectrumSliceCaptureFixedCheckbox,
                                               text("Зафіксувати захват", "Fix capture"), dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(waterfall3DFixedPlaneCheckbox,
                                               text("Зафіксувати площину", "Fix plane"), dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(alternativeSpectrumGradientCheckbox,
                                               text("Градієнт спектра", "Spectrum gradient"), dialog, syncTimer));
        form->addRow(text("Прозорість:", "Opacity:"),
                     linkedSlider(alternativeSpectrumGradientOpacitySlider, dialog, syncTimer, QStringLiteral("%")));
    } else if (type == QStringLiteral("second_spectrum")) {
        title = text("Другий спектр", "Second spectrum");
        form->addRow(QString(), linkedCheckBox(graphCheckbox, text("Показати другий спектр", "Show second spectrum"),
                                               dialog, syncTimer));
    } else if (type == QStringLiteral("agile_scan")) {
        title = QStringLiteral("Agile scan");
        form->addRow(QString(), linkedCheckBox(agileScanCheckbox, text("Увімкнути", "Enable"), dialog, syncTimer));
        form->addRow(text("Пресет:", "Preset:"), linkedCombo(agileScanPresetCombo, dialog, syncTimer));
        form->addRow(text("Відображення:", "Display:"), linkedCombo(scanVisualModeCombo, dialog, syncTimer));
        form->addRow(text("Діапазони:", "Ranges:"), linkedLineEdit(agileScanRangesEdit, dialog, syncTimer));
        form->addRow(text("Крок, МГц:", "Step, MHz:"), linkedDoubleSpin(agileScanStepSpin, dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(agileScanAutoStepCheckbox, text("Крок = sample rate", "Step = sample rate"),
                                               dialog, syncTimer));
        form->addRow(QString(), buttonRow({actionButton(agileScanSavePresetButton, text("Зберегти", "Save")),
                                          actionButton(agileScanDeletePresetButton, text("Видалити", "Delete"))}));
    } else if (type == QStringLiteral("standard_scan")) {
        title = text("Стандартний скан", "Standard scan");
        form->addRow(QString(), linkedCheckBox(standardScanCheckbox, text("Увімкнути", "Enable"), dialog, syncTimer));
        form->addRow(text("Пресет:", "Preset:"), linkedCombo(standardScanPresetCombo, dialog, syncTimer));
        form->addRow(text("Центри, МГц:", "Centers, MHz:"), linkedLineEdit(standardScanCentersEdit, dialog, syncTimer));
        form->addRow(text("Початок:", "Range start:"), linkedLineEdit(standardScanRangeStartEdit, dialog, syncTimer));
        form->addRow(text("Кінець:", "Range end:"), linkedLineEdit(standardScanRangeEndEdit, dialog, syncTimer));
        form->addRow(text("Затримка:", "Dwell:"), linkedSpin(standardScanDwellSpin, dialog, syncTimer));
        form->addRow(text("Стабілізація:", "Settle:"), linkedSpin(standardScanSettleSpin, dialog, syncTimer));
        form->addRow(QString(), actionButton(standardScanFillRangeButton, text("Заповнити діапазон", "Fill range")));
        form->addRow(QString(), buttonRow({actionButton(standardScanSavePresetButton, text("Зберегти", "Save")),
                                          actionButton(standardScanDeletePresetButton, text("Видалити", "Delete"))}));
    } else if (type == QStringLiteral("listening_scan")) {
        title = text("Скан прослуховування", "Listening scan");
        form->addRow(QString(), linkedCheckBox(listeningScanCheckbox, text("Увімкнути", "Enable"), dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(scanListeningLockCheckbox,
                                               text("Фіксувати частоту прослуховування", "Lock listening frequency"),
                                               dialog, syncTimer));
        form->addRow(text("Пресет:", "Preset:"), linkedCombo(listeningScanPresetCombo, dialog, syncTimer));
        form->addRow(text("Частоти, МГц:", "Frequencies, MHz:"), linkedLineEdit(listeningScanTargetsEdit, dialog, syncTimer));
        form->addRow(text("Затримка:", "Dwell:"), linkedSpin(listeningScanDwellSpin, dialog, syncTimer));
        form->addRow(text("Стабілізація:", "Settle:"), linkedSpin(listeningScanSettleSpin, dialog, syncTimer));
        form->addRow(QString(), buttonRow({actionButton(listeningScanSavePresetButton, text("Зберегти", "Save")),
                                          actionButton(listeningScanDeletePresetButton, text("Видалити", "Delete"))}));
    } else if (type == QStringLiteral("spectrum_measurement")) {
        title = text("Вимірювання спектра", "Spectrum measurement");
        form->addRow(QString(), linkedCheckBox(scanMeasurementCheckbox, text("Накопичення вимірювань", "Measurement accumulation"),
                                               dialog, syncTimer));
        form->addRow(text("Крок біну:", "Bin width:"), linkedDoubleSpin(scanMeasurementBinSpin, dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(spectrumScienceMaxHoldCheckbox, QStringLiteral("Max hold"), dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(spectrumScienceMinHoldCheckbox, QStringLiteral("Min hold"), dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(spectrumScienceAverageCheckbox, text("Усереднення", "Average"), dialog, syncTimer));
        form->addRow(text("Час усереднення:", "Average time:"), linkedDoubleSpin(spectrumScienceAverageSpin, dialog, syncTimer));
        form->addRow(text("Маркер:", "Marker:"), linkedCombo(spectrumScienceMarkerCombo, dialog, syncTimer));
        form->addRow(QString(), buttonRow({actionButton(spectrumScienceSetButton, text("Встановити", "Set")),
                                          actionButton(spectrumSciencePeakButton, text("Пік", "Peak")),
                                          actionButton(spectrumSciencePreviousButton, text("Попередній", "Previous")),
                                          actionButton(spectrumScienceNextButton, text("Наступний", "Next"))}));
        form->addRow(QString(), buttonRow({actionButton(scanMeasurementBaselineButton, QStringLiteral("Baseline")),
                                          actionButton(scanMeasurementResetPeakButton, text("Скинути піки", "Reset peaks")),
                                          actionButton(spectrumScienceExportButton, text("Експорт", "Export"))}));
    } else if (type == QStringLiteral("spectrum_recorder")) {
        title = text("Запис кадрів спектра", "Spectrum frame recorder");
        form->addRow(QString(), linkedCheckBox(spectrumFrameBufferCheckbox, text("Постійний передбуфер", "Continuous prebuffer"),
                                               dialog, syncTimer));
        form->addRow(text("Режим:", "Mode:"), linkedCombo(spectrumEventModeCombo, dialog, syncTimer));
        form->addRow(text("Передбуфер:", "Prebuffer:"), linkedSpin(spectrumFramePrebufferSpin, dialog, syncTimer));
        form->addRow(text("Біни:", "Bins:"), linkedCombo(spectrumFrameBinsCombo, dialog, syncTimer));
        form->addRow(QString(), actionButton(spectrumFrameRecordButton, text("Запис / стоп", "Record / stop")));
    } else if (type == QStringLiteral("spur_suppression")) {
        title = text("Придушення spur", "Spur suppression");
        form->addRow(QString(), linkedCheckBox(spurSuppressionCheckbox, text("Увімкнути", "Enable"), dialog, syncTimer));
        form->addRow(QString(), buttonRow({actionButton(spurCalibrateButton, text("Калібрувати", "Calibrate")),
                                          actionButton(spurClearButton, text("Очистити", "Clear"))}));
    } else if (type == QStringLiteral("playback")) {
        title = text("Відтворення запису", "Recording playback");
        form->addRow(text("Файл:", "File:"), linkedCombo(playbackFileCombo, dialog, syncTimer));
        form->addRow(QString(), buttonRow({actionButton(playbackRefreshButton, text("Оновити", "Refresh")),
                                          actionButton(playbackButton, text("Відтворити / стоп", "Play / stop"))}));
    } else if (type == QStringLiteral("audio_output")) {
        title = text("Аудіовихід", "Audio output");
        form->addRow(QString(), linkedCheckBox(audioCheckbox, text("Аудіо увімкнено", "Audio enabled"),
                                               dialog, syncTimer));
        form->addRow(text("Пристрій:", "Device:"), linkedCombo(audioDeviceComboBox, dialog, syncTimer));
        form->addRow(text("Гучність:", "Volume:"), linkedSlider(volumeSlider, dialog, syncTimer, QStringLiteral("%")));
    } else if (type == QStringLiteral("decoder") || type == QStringLiteral("digital_audio_settings")) {
        title = type == QStringLiteral("digital_audio_settings")
                    ? text("Налаштування цифрового аудіо", "Digital audio settings")
                    : text("Цифровий декодер", "Digital decoder");
        form->addRow(QString(), linkedCheckBox(digitalDecodeCheckbox, text("Декодувати", "Decode"),
                                               dialog, syncTimer));
        form->addRow(text("DMR backend:", "DMR backend:"), linkedCombo(dmrBackendCombo, dialog, syncTimer));
        form->addRow(text("4FSK rate:", "4FSK rate:"), linkedCombo(dmrBasebandRateCombo, dialog, syncTimer));
        form->addRow(text("Канал:", "Channel:"), linkedCombo(dmrChannelRateCombo, dialog, syncTimer));
        form->addRow(QString(), commandButton(text("Відкрити текстовий вихід", "Open text output"),
                                              [this, id]() { openDspBlockReference(QStringLiteral("digital_text_output"), id + QStringLiteral(":text")); }));
    } else if (type == QStringLiteral("dmr_decoder")) {
        title = QStringLiteral("DMR");
        form->addRow(QString(), linkedCheckBox(digitalDecodeCheckbox, text("Декодувати", "Decode"), dialog, syncTimer));
        form->addRow(text("Backend:", "Backend:"), linkedCombo(dmrBackendCombo, dialog, syncTimer));
        form->addRow(text("Baseband:", "Baseband:"), linkedCombo(dmrBasebandRateCombo, dialog, syncTimer));
        form->addRow(text("Канал:", "Channel:"), linkedCombo(dmrChannelRateCombo, dialog, syncTimer));
        form->addRow(text("AMBE layout:", "AMBE layout:"), linkedCombo(dmrAmbeLayoutCombo, dialog, syncTimer));
        form->addRow(text("Privacy:", "Privacy:"), linkedCombo(dmrPrivacyModeCombo, dialog, syncTimer));
        form->addRow(text("Key ID:", "Key ID:"), linkedCombo(dmrPrivacyKeyIdCombo, dialog, syncTimer));
        form->addRow(text("Frame offset:", "Frame offset:"), linkedCombo(dmrPrivacyFrameOffsetCombo, dialog, syncTimer));
        form->addRow(text("Drop alignment:", "Drop alignment:"), linkedCombo(dmrPrivacyDropCombo, dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(dmrAdaptiveSlicerCheckbox, text("Адаптивний slicer", "Adaptive slicer"),
                                               dialog, syncTimer));
        form->addRow(QString(), actionButton(dmrPrivacyKeysButton, text("Редактор ключів", "Key editor")));
        form->addRow(QString(), commandButton(text("Відкрити текстовий вихід", "Open text output"),
                                              [this, id]() { openDspBlockReference(QStringLiteral("digital_text_output"), id + QStringLiteral(":text")); }));
    } else if (type == QStringLiteral("cw_decoder")) {
        title = text("Декодер CW", "CW decoder");
        form->addRow(QString(), linkedCheckBox(digitalDecodeCheckbox, text("Декодувати", "Decode"), dialog, syncTimer));
        form->addRow(text("Абетка:", "Alphabet:"), linkedCombo(cwDecoderAlphabetCombo, dialog, syncTimer));
        form->addRow(text("Тон:", "Tone:"), linkedSpin(cwDecoderToneSpin, dialog, syncTimer));
        form->addRow(text("Швидкість:", "Speed:"), linkedSpin(cwDecoderWpmSpin, dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(cwDecoderAdaptiveCheckbox, text("Авто швидкість", "Adaptive speed"),
                                               dialog, syncTimer));
        form->addRow(QString(), commandButton(text("Відкрити текстовий вихід", "Open text output"),
                                              [this, id]() { openDspBlockReference(QStringLiteral("digital_text_output"), id + QStringLiteral(":text")); }));
    } else if (type == QStringLiteral("sstv_decoder")) {
        title = QStringLiteral("SSTV");
        form->addRow(QString(), linkedCheckBox(videoDecodeCheckbox, text("Декодувати відео", "Decode video"), dialog, syncTimer));
        form->addRow(text("Демодуляція:", "Demodulation:"), linkedCombo(sstvDemodulationCombo, dialog, syncTimer));
        form->addRow(text("Стандарт:", "Standard:"), linkedCombo(videoStandardCombo, dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(videoInvertCheckbox, text("Інверсія", "Invert"), dialog, syncTimer));
        form->addRow(QString(), commandButton(text("Відкрити зображення", "Open image output"),
                                              [this, id]() { openDspBlockReference(QStringLiteral("digital_image_output"), id + QStringLiteral(":image")); }));
    } else if (type == QStringLiteral("digital_video") || type == QStringLiteral("digital_video_settings")) {
        title = type == QStringLiteral("digital_video_settings")
                    ? text("Налаштування цифрового відео", "Digital video settings")
                    : text("Цифрове відео", "Digital video");
        form->addRow(QString(), linkedCheckBox(videoDecodeCheckbox, text("Декодувати", "Decode"), dialog, syncTimer));
        form->addRow(text("Демодуляція:", "Demodulation:"), linkedCombo(videoDemodCombo, dialog, syncTimer));
        form->addRow(text("Стандарт:", "Standard:"), linkedCombo(videoStandardCombo, dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(videoHSyncCheckbox, QStringLiteral("H sync"), dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(videoVSyncCheckbox, QStringLiteral("V sync"), dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(videoTestPatternCheckbox, text("Тестовий шаблон", "Test pattern"), dialog, syncTimer));
        form->addRow(QString(), commandButton(text("Відкрити зображення", "Open image output"),
                                              [this, id]() { openDspBlockReference(QStringLiteral("digital_image_output"), id + QStringLiteral(":image")); }));
    } else if (type == QStringLiteral("dmr_hunter") || type == QStringLiteral("fpv_hunter") ||
               type == QStringLiteral("digital_video_hunter")) {
        const bool dmr = type == QStringLiteral("dmr_hunter");
        const bool fpv = type == QStringLiteral("fpv_hunter");
        title = dmr ? QStringLiteral("DMR Hunter") : (fpv ? QStringLiteral("FPV Hunter") : QStringLiteral("Digital Video Hunter"));
        SpectrumHunterControls *controls = dmr ? dmrHunterControls : (fpv ? fpvHunterControls : digitalVideoHunterControls);
        form->addRow(QString(), commandButton(text("Увімкнути / вимкнути пошук", "Toggle detection"), [this, type, controls]() {
            if (!controls) return;
            const bool enabled = type == QStringLiteral("dmr_hunter") ? dmrHunterSettings.enabled
                               : type == QStringLiteral("fpv_hunter") ? fpvHunterSettings.enabled
                                                                      : digitalVideoHunterSettings.enabled;
            controls->setDetectChecked(!enabled);
        }));
        form->addRow(QString(), commandButton(text("Застосувати пресет до скану", "Apply preset to scan"), [this, type]() {
            if (type == QStringLiteral("dmr_hunter")) applyDmrHunterPresetToScan();
            else if (type == QStringLiteral("fpv_hunter")) applyFpvHunterPresetToScan();
            else applyDigitalVideoHunterPresetToScan();
        }));
        form->addRow(QString(), commandButton(text("Перейти до кандидата", "Tune candidate"), [this, type]() {
            if (type == QStringLiteral("dmr_hunter")) tuneDmrHunterCandidate();
            else if (type == QStringLiteral("fpv_hunter")) tuneFpvHunterCandidate();
            else tuneDigitalVideoHunterCandidate();
        }));
        form->addRow(QString(), showDockButton(controlsDock, text("Відкрити бічну панель", "Open side panel")));
    } else if (type == QStringLiteral("gnss_sdr")) {
        title = text("GNSS через SDR", "GNSS via SDR");
        form->addRow(text("Система:", "System:"), linkedCombo(gnssSystemCombo, dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(gnssMonitorCheckbox, text("Безперервний аналіз", "Continuous analysis"),
                                               dialog, syncTimer));
        form->addRow(text("Інтеграція:", "Integration:"), linkedSpin(gnssIntegrationSpin, dialog, syncTimer));
        form->addRow(text("Фільтр каналу:", "Channel filter:"), linkedDoubleSpin(gnssChannelFilterSpin, dialog, syncTimer));
        form->addRow(text("Doppler span:", "Doppler span:"), linkedSpin(gnssDopplerSpanSpin, dialog, syncTimer));
        form->addRow(text("Doppler step:", "Doppler step:"), linkedSpin(gnssDopplerStepSpin, dialog, syncTimer));
        form->addRow(QString(), buttonRow({actionButton(gnssTuneButton, text("Налаштувати", "Tune")),
                                          actionButton(gnssScanButton, text("Скан", "Scan")),
                                          actionButton(gnssAcquireButton, text("Накопичення", "Acquire"))}));
        form->addRow(QString(), buttonRow({actionButton(gnssPlotButton, text("Графік", "Plot")),
                                          actionButton(gnssSatellitesButton, text("Супутники", "Satellites")),
                                          actionButton(gnssMonitorResetButton, text("Скинути", "Reset"))}));
    } else if (type == QStringLiteral("gnss_serial")) {
        title = text("GNSS модуль / NMEA / UBX", "GNSS module / NMEA / UBX");
        form->addRow(text("Порт:", "Port:"), linkedCombo(gnssSerialPortEdit, dialog, syncTimer));
        form->addRow(text("Baud:", "Baud:"), linkedSpin(gnssSerialBaudSpin, dialog, syncTimer));
        form->addRow(text("Політика позиції:", "Position policy:"), linkedCombo(gnssPositionPolicyCombo, dialog, syncTimer));
        form->addRow(text("Часовий пояс:", "Time zone:"), linkedCombo(gnssTimeZoneCombo, dialog, syncTimer));
        form->addRow(QString(), buttonRow({actionButton(gnssSerialButton, text("Підключити / стоп", "Connect / stop")),
                                          actionButton(gnssSatellitesButton, text("Супутники", "Satellites")),
                                          actionButton(qthMapButton, text("Карта", "Map"))}));
        form->addRow(QString(), buttonRow({actionButton(gnssUbxSystemsButton, QStringLiteral("UBX CFG-GNSS")),
                                          actionButton(gnssUbxSaveButton, text("Зберегти в модуль", "Save to module"))}));
        form->addRow(QString(), buttonRow({actionButton(gnssNmeaLogButton, QStringLiteral("NMEA log")),
                                          actionButton(gnssSerialRawLogButton, QStringLiteral("Raw UBX/NMEA")),
                                          actionButton(gnssNmeaReplayButton, text("Реплей", "Replay"))}));
    } else if (type == QStringLiteral("gpio")) {
        title = QStringLiteral("GPIO");
        for (int i = 0; i < 8; ++i)
            form->addRow(QString(), linkedCheckBox(checkBoxes[i], QStringLiteral("GPIO %1").arg(i + 1), dialog, syncTimer));
    } else if (type == QStringLiteral("recorder")) {
        title = text("Записувач", "Recorder");
        form->addRow(text("Режим:", "Mode:"), linkedCombo(recordingModeCombo, dialog, syncTimer));
        form->addRow(QString(), linkedCheckBox(spectrumFrameBufferCheckbox,
                                               text("Передбуфер спектра", "Spectrum prebuffer"),
                                               dialog, syncTimer));
        form->addRow(text("Передбуфер:", "Prebuffer:"), linkedSpin(spectrumFramePrebufferSpin, dialog, syncTimer));
        form->addRow(text("Біни:", "Bins:"), linkedCombo(spectrumFrameBinsCombo, dialog, syncTimer));
        auto *record = new QPushButton(text("Запис", "Record"), dialog);
        connect(record, &QPushButton::clicked, recordButton, &QPushButton::click);
        form->addRow(QString(), record);
    } else {
        dialog->deleteLater();
        return;
    }

    dialog->setWindowTitle(title);
    auto *stayOnTop = new QCheckBox(text("Залишатися поверх вікон", "Stay on top"), dialog);
    stayOnTop->setChecked(true);
    dialog->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    connect(stayOnTop, &QCheckBox::toggled, dialog, [dialog](bool enabled) {
        dialog->setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    });
    root->addWidget(stayOnTop);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText(text("Закрити", "Close"));
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    root->addWidget(buttons);

    dspBlockEditors.insert(editorKey, dialog);
    connect(dialog, &QObject::destroyed, this, [this, editorKey]() {
        dspBlockEditors.remove(editorKey);
    });
    dialog->show();
    dialog->adjustSize();
    dialog->raise();
    dialog->activateWindow();
}

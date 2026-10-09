#include "dspflowpanel.h"
#include "dspresearchwidget.h"
#include "finetunewidget.h"

#include <QAction>
#include <QGraphicsPathItem>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QHash>
#include <QInputDialog>
#include <QImage>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <cstring>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVarLengthArray>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <functional>

namespace {

constexpr qreal kBlockWidth = 172.0;
constexpr qreal kBlockHeight = 68.0;
constexpr qreal kMiniBlockWidth = 104.0;
constexpr qreal kMiniBlockHeight = 40.0;
constexpr qreal kFineTuneBlockWidth = 260.0;
constexpr qreal kFineTuneBlockHeight = 108.0;
constexpr qreal kVfoDisplayBlockWidth = 292.0;
constexpr qreal kVfoDisplayBlockHeight = 194.0;
constexpr qreal kVisualBlockMinWidth = 220.0;
constexpr qreal kVisualBlockMinHeight = 150.0;
constexpr qreal kVisualBlockMaxWidth = 1200.0;
constexpr qreal kVisualBlockMaxHeight = 900.0;
constexpr qreal kPortRadius = 6.0;

class DspConnectionItem;

bool isResearchSettingsType(const QString &type) {
    return type == QStringLiteral("oscilloscope") || type == QStringLiteral("constellation") ||
           type == QStringLiteral("eye_diagram") || type == QStringLiteral("digital_sync_lab");
}

bool isResearchViewType(const QString &type) {
    return type == QStringLiteral("oscilloscope_view") || type == QStringLiteral("constellation_view") ||
           type == QStringLiteral("eye_diagram_view") || type == QStringLiteral("digital_sync_view");
}

QString settingsTypeForResearchView(const QString &type) {
    if (type == QStringLiteral("oscilloscope_view")) return QStringLiteral("oscilloscope");
    if (type == QStringLiteral("constellation_view")) return QStringLiteral("constellation");
    if (type == QStringLiteral("eye_diagram_view")) return QStringLiteral("eye_diagram");
    if (type == QStringLiteral("digital_sync_view")) return QStringLiteral("digital_sync_lab");
    return QString();
}

DspResearchWidget::Mode researchModeForView(const QString &type) {
    if (type == QStringLiteral("constellation_view")) return DspResearchWidget::Mode::Constellation;
    if (type == QStringLiteral("eye_diagram_view")) return DspResearchWidget::Mode::EyeDiagram;
    if (type == QStringLiteral("digital_sync_view")) return DspResearchWidget::Mode::Synchronization;
    return DspResearchWidget::Mode::Oscilloscope;
}

class MiniVfoDisplay final : public QWidget {
public:
    explicit MiniVfoDisplay(bool waterfall, QWidget *parent = nullptr)
        : QWidget(parent), waterfall_(waterfall), waterfallImage_(256, 104, QImage::Format_RGB32) {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setMinimumSize(270, 138);
        waterfallImage_.fill(QColor(4, 7, 12));
        updateTimer_.start();
    }

    void setSpectrum(const std::vector<float> &frequencies,
                     const std::vector<float> &levels,
                     double centerHz,
                     double bandwidthHz,
                     const QString &name) {
        if (frequencies.empty() || frequencies.size() != levels.size() ||
            !std::isfinite(centerHz) || !std::isfinite(bandwidthHz) || bandwidthHz <= 0.0) return;
        if (updateTimer_.elapsed() < 55) return;
        updateTimer_.restart();
        name_ = name;
        centerHz_ = centerHz;
        bandwidthHz_ = bandwidthHz;
        const double low = centerHz - bandwidthHz * 0.5;
        const double high = centerHz + bandwidthHz * 0.5;
        if (high < frequencies.front() || low > frequencies.back()) {
            setStatus(QStringLiteral("VFO outside visible span"));
            return;
        }
        auto begin = std::lower_bound(frequencies.begin(), frequencies.end(), static_cast<float>(low));
        auto end = std::upper_bound(frequencies.begin(), frequencies.end(), static_cast<float>(high));
        if (begin == end) {
            begin = std::lower_bound(frequencies.begin(), frequencies.end(), static_cast<float>(centerHz));
            if (begin == frequencies.end()) --begin;
            end = begin + 1;
        }
        status_.clear();
        const std::size_t first = static_cast<std::size_t>(std::distance(frequencies.begin(), begin));
        const std::size_t last = static_cast<std::size_t>(std::distance(frequencies.begin(), end));
        constexpr int bins = 256;
        displayLevels_.assign(bins, -160.0f);
        for (int pixel = 0; pixel < bins; ++pixel) {
            const std::size_t sourceBegin = first + (last - first) * static_cast<std::size_t>(pixel) / bins;
            const std::size_t sourceEnd = first + (last - first) * static_cast<std::size_t>(pixel + 1) / bins;
            for (std::size_t index = sourceBegin; index < (std::max)(sourceBegin + 1U, sourceEnd) && index < levels.size(); ++index) {
                const std::size_t shiftedIndex = (index + levels.size() / 2U) % levels.size();
                if (std::isfinite(levels[shiftedIndex])) displayLevels_[static_cast<std::size_t>(pixel)] =
                    (std::max)(displayLevels_[static_cast<std::size_t>(pixel)], levels[shiftedIndex]);
            }
        }
        if (waterfall_) {
            for (int y = waterfallImage_.height() - 1; y > 0; --y) {
                std::memcpy(waterfallImage_.scanLine(y), waterfallImage_.constScanLine(y - 1),
                            static_cast<std::size_t>(waterfallImage_.bytesPerLine()));
            }
            const float levelSpan = maximumDbfs_ - minimumDbfs_;
            const auto colorForLevel = [this, levelSpan](float level) {
                const float value = std::clamp((level - minimumDbfs_) / levelSpan, 0.0f, 1.0f);
                const int red = int(255.0f * std::clamp((value - 0.50f) * 2.0f, 0.0f, 1.0f));
                const int green = int(255.0f * std::clamp(1.0f - std::abs(value - 0.55f) * 2.2f, 0.0f, 1.0f));
                const int blue = int(255.0f * std::clamp(1.0f - value * 1.35f, 0.0f, 1.0f));
                return qRgb(red, green, blue);
            };
            QRgb *line = reinterpret_cast<QRgb *>(waterfallImage_.scanLine(0));
            for (int x = 0; x < waterfallImage_.width(); ++x) line[x] = colorForLevel(displayLevels_[static_cast<std::size_t>(x)]);
        }
        update();
    }

    void setStatus(const QString &status) {
        if (status_ == status) return;
        status_ = status;
        displayLevels_.clear();
        waterfallImage_.fill(QColor(4, 7, 12));
        update();
    }

    void setDbfsRange(float minimumDbfs, float maximumDbfs) {
        minimumDbfs = std::clamp(minimumDbfs, -200.0f, 19.0f);
        maximumDbfs = std::clamp(maximumDbfs, minimumDbfs + 1.0f, 20.0f);
        if (qFuzzyCompare(minimumDbfs_ + 201.0f, minimumDbfs + 201.0f) &&
            qFuzzyCompare(maximumDbfs_ + 201.0f, maximumDbfs + 201.0f)) {
            return;
        }
        minimumDbfs_ = minimumDbfs;
        maximumDbfs_ = maximumDbfs;
        waterfallImage_.fill(QColor(4, 7, 12));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(5, 8, 13));
        const QRect plot = rect().adjusted(5, 18, -5, -18);
        painter.setPen(QPen(QColor(38, 48, 60), 1.0));
        for (int i = 1; i < 4; ++i) painter.drawLine(plot.left(), plot.top() + plot.height() * i / 4,
                                                    plot.right(), plot.top() + plot.height() * i / 4);
        if (waterfall_) {
            painter.drawImage(plot, waterfallImage_);
        } else if (!displayLevels_.empty()) {
            QPolygonF line;
            line.reserve(static_cast<int>(displayLevels_.size()));
            for (std::size_t index = 0; index < displayLevels_.size(); ++index) {
                const qreal x = plot.left() + plot.width() * qreal(index) / qreal(displayLevels_.size() - 1U);
                const qreal normalized = std::clamp(
                    (displayLevels_[index] - minimumDbfs_) / (maximumDbfs_ - minimumDbfs_),
                    0.0f,
                    1.0f);
                line << QPointF(x, plot.bottom() - normalized * plot.height());
            }
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(QColor(73, 220, 145), 1.4));
            painter.drawPolyline(line);
        }
        if (!status_.isEmpty()) {
            painter.setPen(QColor(176, 186, 198));
            painter.drawText(plot, Qt::AlignCenter | Qt::TextWordWrap, status_);
        }
        painter.setPen(QColor(220, 226, 234));
        painter.drawText(QRect(5, 1, width() - 10, 16), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("%1  %2 MHz / %3 kHz")
                             .arg(name_)
                             .arg(centerHz_ / 1.0e6, 0, 'f', 6)
                             .arg(bandwidthHz_ / 1000.0, 0, 'f', 1));
        painter.setPen(QColor(145, 157, 171));
        painter.drawText(QRect(5, height() - 17, width() - 10, 16), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("%1").arg((centerHz_ - bandwidthHz_ * 0.5) / 1.0e6, 0, 'f', 6));
        painter.drawText(QRect(5, height() - 17, width() - 10, 16), Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("%1 MHz").arg((centerHz_ + bandwidthHz_ * 0.5) / 1.0e6, 0, 'f', 6));
    }

private:
    bool waterfall_ = false;
    QImage waterfallImage_;
    std::vector<float> displayLevels_;
    QString name_ = QStringLiteral("VFO");
    double centerHz_ = 0.0;
    double bandwidthHz_ = 0.0;
    QString status_ = QStringLiteral("Connect VFO channel");
    QElapsedTimer updateTimer_;
    float minimumDbfs_ = -140.0f;
    float maximumDbfs_ = -40.0f;
};

bool isMiniControlBlockType(const QString &type) {
    return type == QStringLiteral("run_control") || type == QStringLiteral("enable_control");
}

bool isVerticalControlSourceType(const QString &type) {
    return isMiniControlBlockType(type) || type == QStringLiteral("frequency_control") ||
           type == QStringLiteral("fine_tune_control");
}

bool acceptsControlType(const QString &targetType, const QString &controlType) {
    if (controlType == QStringLiteral("run_control")) {
        return targetType == QStringLiteral("receiver_source") || targetType == QStringLiteral("iq_source") ||
               targetType == QStringLiteral("network_input") || targetType == QStringLiteral("playback");
    }
    if (controlType == QStringLiteral("frequency_control") ||
        controlType == QStringLiteral("fine_tune_control")) {
        return targetType == QStringLiteral("receiver_source") || targetType == QStringLiteral("iq_source") ||
               targetType == QStringLiteral("network_input") || targetType == QStringLiteral("playback");
    }
    if (controlType != QStringLiteral("enable_control")) return false;
    static const QSet<QString> toggleable = {
        QStringLiteral("spectrum_display"), QStringLiteral("second_spectrum"),
        QStringLiteral("waterfall_3d"), QStringLiteral("audio_output"),
        QStringLiteral("decoder"), QStringLiteral("dmr_decoder"), QStringLiteral("cw_decoder"),
        QStringLiteral("sstv_decoder"), QStringLiteral("digital_video"),
        QStringLiteral("digital_audio_settings"), QStringLiteral("digital_video_settings"),
        QStringLiteral("agile_scan"), QStringLiteral("standard_scan"), QStringLiteral("listening_scan"),
        QStringLiteral("spectrum_measurement"), QStringLiteral("spur_suppression"),
        QStringLiteral("hf_interference"), QStringLiteral("gnss_sdr"), QStringLiteral("gnss_serial")
        , QStringLiteral("multi_vfo_channelizer")
    };
    return toggleable.contains(targetType);
}

bool acceptsAnyControl(const QString &targetType) {
    return acceptsControlType(targetType, QStringLiteral("run_control")) ||
           acceptsControlType(targetType, QStringLiteral("frequency_control")) ||
           acceptsControlType(targetType, QStringLiteral("enable_control"));
}

QColor blockColor(const QString &type) {
    if (type == QStringLiteral("receiver_source") || type == QStringLiteral("iq_source")) {
        return QColor(37, 115, 155);
    }
    if (type == QStringLiteral("channel_filter") || type == QStringLiteral("resampler") ||
        type == QStringLiteral("audio_filter") || type == QStringLiteral("multi_vfo_channelizer") ||
        type == QStringLiteral("vfo_channel")) {
        return QColor(43, 130, 91);
    }
    if (type == QStringLiteral("demodulator") || type == QStringLiteral("decoder")) {
        return QColor(148, 94, 36);
    }
    if (type == QStringLiteral("fft")) {
        return QColor(112, 76, 158);
    }
    if (type.contains(QStringLiteral("scan")) || type.contains(QStringLiteral("hunter"))) {
        return QColor(44, 126, 139);
    }
    if (type.contains(QStringLiteral("waterfall")) || type.contains(QStringLiteral("spectrum")) ||
        type == QStringLiteral("oscilloscope") || type == QStringLiteral("constellation") ||
        type == QStringLiteral("eye_diagram") || type == QStringLiteral("digital_sync_lab") ||
        isResearchViewType(type) ||
        type == QStringLiteral("zero_span") || type == QStringLiteral("research_analysis")) {
        return QColor(112, 76, 158);
    }
    if (type.startsWith(QStringLiteral("gnss")) || type == QStringLiteral("qth_map")) {
        return QColor(45, 132, 98);
    }
    if (type.contains(QStringLiteral("decoder")) || type.startsWith(QStringLiteral("digital_"))) {
        return QColor(148, 94, 36);
    }
    return QColor(82, 92, 108);
}

bool typeHasInput(const QString &type) {
    static const QSet<QString> noInput = {
        QStringLiteral("receiver_source"), QStringLiteral("iq_source"), QStringLiteral("network_input"),
        QStringLiteral("run_control"), QStringLiteral("enable_control"), QStringLiteral("frequency_control"),
        QStringLiteral("fine_tune_control"),
        QStringLiteral("playback"), QStringLiteral("gnss_serial"), QStringLiteral("agile_scan"),
        QStringLiteral("standard_scan"), QStringLiteral("listening_scan"), QStringLiteral("qth_map"),
        QStringLiteral("gpio"), QStringLiteral("transmitter"), QStringLiteral("presets"),
        QStringLiteral("calibration"), QStringLiteral("spectrum_replay"),
        QStringLiteral("application_settings")
    };
    return !noInput.contains(type);
}

bool typeHasOutput(const QString &type) {
    static const QSet<QString> noOutput = {
        QStringLiteral("audio_output"), QStringLiteral("recorder"), QStringLiteral("network_output"),
        QStringLiteral("fft"), QStringLiteral("decoder"), QStringLiteral("spectrum_display"),
        QStringLiteral("waterfall_2d"), QStringLiteral("waterfall_3d"), QStringLiteral("second_spectrum"),
        QStringLiteral("spectrum_measurement"), QStringLiteral("zoom_spectrum"), QStringLiteral("zero_span"),
        QStringLiteral("research_analysis"), QStringLiteral("oscilloscope_view"),
        QStringLiteral("constellation_view"), QStringLiteral("eye_diagram_view"),
        QStringLiteral("digital_sync_view"),
        QStringLiteral("spur_suppression"),
        QStringLiteral("spectrum_recorder"), QStringLiteral("spectrum_replay"),
        QStringLiteral("dmr_decoder"), QStringLiteral("cw_decoder"), QStringLiteral("sstv_decoder"),
        QStringLiteral("digital_video"), QStringLiteral("digital_text_output"),
        QStringLiteral("digital_image_output"), QStringLiteral("vfo_spectrum"), QStringLiteral("vfo_waterfall"),
        QStringLiteral("dmr_hunter"), QStringLiteral("fpv_hunter"),
        QStringLiteral("digital_video_hunter"), QStringLiteral("gnss_sdr"), QStringLiteral("gnss_serial"),
        QStringLiteral("qth_map"), QStringLiteral("agile_scan"), QStringLiteral("standard_scan"),
        QStringLiteral("listening_scan"), QStringLiteral("gpio"), QStringLiteral("presets"),
        QStringLiteral("calibration"), QStringLiteral("application_settings")
    };
    return !noOutput.contains(type);
}

QStringList allBlockTypes() {
    return {QStringLiteral("run_control"), QStringLiteral("enable_control"),
            QStringLiteral("receiver_source"), QStringLiteral("iq_source"),
            QStringLiteral("network_input"), QStringLiteral("playback"),
            QStringLiteral("frequency_control"), QStringLiteral("fine_tune_control"),
            QStringLiteral("channel_filter"),
            QStringLiteral("multi_vfo_channelizer"), QStringLiteral("vfo_channel"),
            QStringLiteral("vfo_spectrum"), QStringLiteral("vfo_waterfall"),
            QStringLiteral("resampler"), QStringLiteral("fft"),
            QStringLiteral("hf_interference"),
            QStringLiteral("demodulator"), QStringLiteral("audio_filter"),
            QStringLiteral("filter_low_pass"), QStringLiteral("filter_high_pass"),
            QStringLiteral("filter_band_pass"), QStringLiteral("filter_notch"),
            QStringLiteral("filter_dc_blocker"), QStringLiteral("filter_de_emphasis"),
            QStringLiteral("filter_parametric_eq"), QStringLiteral("filter_low_shelf"),
            QStringLiteral("filter_high_shelf"), QStringLiteral("filter_adaptive_notch"),
            QStringLiteral("filter_noise_blanker"), QStringLiteral("filter_cw"),
            QStringLiteral("filter_ctcss"), QStringLiteral("filter_spectral_denoise"),
            QStringLiteral("filter_custom_fir"), QStringLiteral("filter_gain"),
            QStringLiteral("filter_compressor"), QStringLiteral("filter_limiter"),
            QStringLiteral("filter_noise_gate"), QStringLiteral("decoder"),
            QStringLiteral("digital_audio_settings"), QStringLiteral("digital_video_settings"),
            QStringLiteral("digital_text_output"), QStringLiteral("digital_image_output"),
            QStringLiteral("spectrum_display"), QStringLiteral("waterfall_2d"),
            QStringLiteral("waterfall_3d"), QStringLiteral("second_spectrum"),
            QStringLiteral("agile_scan"), QStringLiteral("standard_scan"),
            QStringLiteral("listening_scan"), QStringLiteral("spectrum_measurement"),
            QStringLiteral("zoom_spectrum"), QStringLiteral("zero_span"),
            QStringLiteral("research_analysis"), QStringLiteral("oscilloscope"),
            QStringLiteral("constellation"), QStringLiteral("eye_diagram"),
            QStringLiteral("digital_sync_lab"), QStringLiteral("oscilloscope_view"),
            QStringLiteral("constellation_view"), QStringLiteral("eye_diagram_view"),
            QStringLiteral("digital_sync_view"),
            QStringLiteral("spur_suppression"),
            QStringLiteral("spectrum_recorder"),
            QStringLiteral("spectrum_replay"), QStringLiteral("dmr_decoder"),
            QStringLiteral("cw_decoder"), QStringLiteral("sstv_decoder"),
            QStringLiteral("digital_video"), QStringLiteral("dmr_hunter"),
            QStringLiteral("fpv_hunter"), QStringLiteral("digital_video_hunter"),
            QStringLiteral("gnss_sdr"), QStringLiteral("gnss_serial"),
            QStringLiteral("qth_map"), QStringLiteral("gpio"),
            QStringLiteral("recorder"), QStringLiteral("network_output"),
            QStringLiteral("audio_output"), QStringLiteral("transmitter"),
            QStringLiteral("presets"), QStringLiteral("calibration"),
            QStringLiteral("application_settings")};
}

bool isFilterBlockType(const QString &type) {
    return type.startsWith(QStringLiteral("filter_"));
}

QString blockCategory(const QString &type) {
    if (isFilterBlockType(type)) return QStringLiteral("filters");
    if (type.contains(QStringLiteral("scan")) || type.contains(QStringLiteral("hunter")))
        return QStringLiteral("scanning");
    if (type == QStringLiteral("spectrum_display") || type.startsWith(QStringLiteral("waterfall_")) ||
        type == QStringLiteral("vfo_spectrum") || type == QStringLiteral("vfo_waterfall") ||
        type == QStringLiteral("second_spectrum") || type == QStringLiteral("oscilloscope") ||
        type == QStringLiteral("constellation") || type == QStringLiteral("eye_diagram") ||
        type == QStringLiteral("digital_sync_lab") || isResearchViewType(type))
        return QStringLiteral("visualization");
    if (type == QStringLiteral("spectrum_measurement") || type == QStringLiteral("zoom_spectrum") ||
        type == QStringLiteral("zero_span") || type == QStringLiteral("research_analysis") ||
        type == QStringLiteral("spur_suppression") || type == QStringLiteral("calibration") ||
        type == QStringLiteral("spectrum_recorder") || type == QStringLiteral("spectrum_replay"))
        return QStringLiteral("measurement");
    if (type == QStringLiteral("decoder") || type == QStringLiteral("dmr_decoder") ||
        type == QStringLiteral("cw_decoder") || type == QStringLiteral("sstv_decoder") ||
        type.startsWith(QStringLiteral("digital_"))) return QStringLiteral("digital");
    if (type.startsWith(QStringLiteral("gnss_")) || type == QStringLiteral("qth_map"))
        return QStringLiteral("navigation");
    if (type == QStringLiteral("recorder") || type == QStringLiteral("network_input") ||
        type == QStringLiteral("network_output") || type == QStringLiteral("playback") ||
        type == QStringLiteral("audio_output") || type == QStringLiteral("transmitter") ||
        type == QStringLiteral("gpio") || type == QStringLiteral("presets") ||
        type == QStringLiteral("application_settings")) return QStringLiteral("io_tools");
    return QStringLiteral("signal_path");
}

QString categoryTitle(const QString &category, bool ukrainian) {
    if (category == QStringLiteral("signal_path")) return ukrainian ? QStringLiteral("Тракт сигналу") : QStringLiteral("Signal path");
    if (category == QStringLiteral("filters")) return ukrainian ? QStringLiteral("Аудіофільтри") : QStringLiteral("Audio filters");
    if (category == QStringLiteral("scanning")) return ukrainian ? QStringLiteral("Сканування і пошук") : QStringLiteral("Scanning and hunting");
    if (category == QStringLiteral("visualization")) return ukrainian ? QStringLiteral("Візуалізація") : QStringLiteral("Visualization");
    if (category == QStringLiteral("measurement")) return ukrainian ? QStringLiteral("Вимірювання і дослідження") : QStringLiteral("Measurement and research");
    if (category == QStringLiteral("digital")) return ukrainian ? QStringLiteral("Цифрові режими") : QStringLiteral("Digital modes");
    if (category == QStringLiteral("navigation")) return QStringLiteral("GNSS / QTH");
    return ukrainian ? QStringLiteral("Ввід, вивід і сервіс") : QStringLiteral("I/O and tools");
}

class DspBlockItem final : public QGraphicsItem {
public:
    DspBlockItem(QString id, QString type, QString title, bool customTitle,
                 const std::function<void(double)> &fineTuneDelta = {}, int vfoIndex = -1)
        : id_(std::move(id)), type_(std::move(type)), title_(std::move(title)), customTitle_(customTitle),
          vfoIndex_(vfoIndex) {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        setCacheMode(type_ == QStringLiteral("fine_tune_control") ? NoCache : DeviceCoordinateCache);
        setCursor(Qt::OpenHandCursor);
        if (type_ == QStringLiteral("fine_tune_control")) {
            fineTuneWidget_ = new FineTuneScaleWidget();
            fineTuneWidget_->setFixedSize(int(kFineTuneBlockWidth - 16.0), 56);
            fineTuneProxy_ = new QGraphicsProxyWidget(this);
            fineTuneProxy_->setWidget(fineTuneWidget_);
            fineTuneProxy_->setPos(8.0, 42.0);
            QObject::connect(fineTuneWidget_, &FineTuneScaleWidget::fineTuneDelta,
                             fineTuneWidget_, [fineTuneDelta](double deltaHz) {
                if (fineTuneDelta) fineTuneDelta(deltaHz);
            });
        }
        if (type_ == QStringLiteral("vfo_spectrum") || type_ == QStringLiteral("vfo_waterfall")) {
            miniVfoDisplay_ = new MiniVfoDisplay(type_ == QStringLiteral("vfo_waterfall"));
            miniVfoDisplay_->setAttribute(Qt::WA_TransparentForMouseEvents);
            miniVfoProxy_ = new QGraphicsProxyWidget(this);
            miniVfoProxy_->setWidget(miniVfoDisplay_);
            miniVfoProxy_->setPos(8.0, 43.0);
        }
        if (isResearchViewType(type_)) {
            researchDisplay_ = new DspResearchWidget(researchModeForView(type_));
            researchDisplay_->setAttribute(Qt::WA_TransparentForMouseEvents);
            researchProxy_ = new QGraphicsProxyWidget(this);
            researchProxy_->setWidget(researchDisplay_);
            researchProxy_->setPos(8.0, 43.0);
        }
        if (isVisualBlock()) resizeEmbeddedWidget();
    }

    QRectF boundingRect() const override {
        const qreal width = blockWidth();
        const qreal height = blockHeight();
        return QRectF(-kPortRadius - 2.0, -kPortRadius - 2.0,
                      width + (kPortRadius + 2.0) * 2.0,
                      height + (kPortRadius + 2.0) * 2.0);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *) override {
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRectF body(0.0, 0.0, blockWidth(), blockHeight());
        const bool selected = option->state.testFlag(QStyle::State_Selected);
        if (isMiniControlBlock()) {
            const QColor fill = controlActive_ ? QColor(181, 55, 62) : QColor(39, 151, 83);
            painter->setPen(QPen(selected ? QColor(255, 205, 92) : fill.lighter(135), selected ? 2.0 : 1.2));
            painter->setBrush(fill);
            painter->drawRoundedRect(body, 7.0, 7.0);
            painter->setPen(Qt::white);
            QFont font = painter->font();
            font.setBold(true);
            painter->setFont(font);
            painter->drawText(body.adjusted(8.0, 3.0, -8.0, -3.0), Qt::AlignCenter,
                              controlLabel_.isEmpty() ? title_ : controlLabel_);
            painter->setPen(QPen(QColor(238, 224, 255), 1.0));
            painter->setBrush(QColor(178, 107, 230));
            painter->drawEllipse(controlTopPort(), kPortRadius, kPortRadius);
            painter->drawEllipse(controlBottomPort(), kPortRadius, kPortRadius);
            return;
        }
        painter->setPen(QPen(selected ? QColor(255, 194, 72) : QColor(79, 88, 101),
                             selected ? 2.0 : 1.0));
        painter->setBrush(QColor(31, 35, 42));
        painter->drawRoundedRect(body, 6.0, 6.0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(blockColor(type_));
        painter->drawRoundedRect(QRectF(1.0, 1.0, blockWidth() - 2.0, 9.0), 5.0, 5.0);
        painter->drawRect(QRectF(1.0, 6.0, blockWidth() - 2.0, 5.0));

        painter->setPen(QColor(239, 242, 246));
        QFont titleFont = painter->font();
        titleFont.setBold(true);
        painter->setFont(titleFont);
        painter->drawText(QRectF(13.0, 18.0, blockWidth() - 26.0, 25.0),
                          Qt::AlignCenter | Qt::TextSingleLine,
                          painter->fontMetrics().elidedText(title_, Qt::ElideRight, int(blockWidth() - 28.0)));
        QFont typeFont = painter->font();
        typeFont.setBold(false);
        typeFont.setPointSizeF(std::max(7.0, typeFont.pointSizeF() - 1.0));
        painter->setFont(typeFont);
        painter->setPen(QColor(151, 161, 174));
        if (!isVisualBlock()) {
            painter->drawText(QRectF(12.0, 43.0, blockWidth() - 24.0, 17.0),
                              Qt::AlignCenter | Qt::TextSingleLine, type_.toUpper());
        }

        painter->setPen(QPen(QColor(219, 226, 234), 1.0));
        if (hasInput()) {
            painter->setBrush(QColor(87, 181, 255));
            painter->drawEllipse(inputPort(), kPortRadius, kPortRadius);
        }
        if (hasOutput()) {
            if (isVerticalControlSource()) {
                painter->setBrush(QColor(178, 107, 230));
                painter->drawEllipse(controlTopPort(), kPortRadius, kPortRadius);
                painter->drawEllipse(controlBottomPort(), kPortRadius, kPortRadius);
            } else {
                painter->setBrush(QColor(255, 172, 72));
                painter->drawEllipse(outputPort(), kPortRadius, kPortRadius);
            }
        }
        if (acceptsAnyControl(type_)) {
            painter->setBrush(QColor(178, 107, 230));
            painter->drawEllipse(controlTopPort(), kPortRadius, kPortRadius);
            painter->drawEllipse(controlBottomPort(), kPortRadius, kPortRadius);
        }
        if (isVisualBlock()) {
            painter->setPen(QPen(QColor(132, 145, 160), 1.2));
            const QRectF handle = resizeHandleRect();
            painter->drawLine(handle.bottomLeft() + QPointF(3.0, -1.0), handle.topRight() + QPointF(-1.0, 3.0));
            painter->drawLine(handle.bottomLeft() + QPointF(8.0, -1.0), handle.topRight() + QPointF(-1.0, 8.0));
        }
    }

    QString id() const { return id_; }
    QString blockType() const { return type_; }
    QString title() const { return title_; }
    bool customTitle() const { return customTitle_; }
    bool isMiniControlBlock() const { return isMiniControlBlockType(type_); }
    bool isVerticalControlSource() const { return isVerticalControlSourceType(type_); }
    bool isVisualBlock() const { return miniVfoDisplay_ || researchDisplay_; }
    qreal blockWidth() const {
        if (type_ == QStringLiteral("fine_tune_control")) return kFineTuneBlockWidth;
        if (isVisualBlock()) return visualWidth_;
        return isMiniControlBlock() ? kMiniBlockWidth : kBlockWidth;
    }
    qreal blockHeight() const {
        if (type_ == QStringLiteral("fine_tune_control")) return kFineTuneBlockHeight;
        if (isVisualBlock()) return visualHeight_;
        return isMiniControlBlock() ? kMiniBlockHeight : kBlockHeight;
    }
    qreal visualWidth() const { return visualWidth_; }
    qreal visualHeight() const { return visualHeight_; }
    void setVisualSize(qreal width, qreal height) {
        if (!isVisualBlock()) return;
        width = std::clamp(width, kVisualBlockMinWidth, kVisualBlockMaxWidth);
        height = std::clamp(height, kVisualBlockMinHeight, kVisualBlockMaxHeight);
        if (qFuzzyCompare(width + 1.0, visualWidth_ + 1.0) &&
            qFuzzyCompare(height + 1.0, visualHeight_ + 1.0)) return;
        prepareGeometryChange();
        visualWidth_ = width;
        visualHeight_ = height;
        resizeEmbeddedWidget();
        updateConnections();
        update();
    }
    QJsonObject settings() const { return settings_; }
    void setSettings(const QJsonObject &settings) {
        settings_ = settings;
        if (researchDisplay_) researchDisplay_->setSettings(settings_);
        if (miniVfoDisplay_) {
            miniVfoDisplay_->setDbfsRange(
                static_cast<float>(settings_.value(QStringLiteral("minimumDbfs")).toDouble(-140.0)),
                static_cast<float>(settings_.value(QStringLiteral("maximumDbfs")).toDouble(-40.0)));
        }
    }
    void setResearchConnection(bool connected, const QJsonObject &settings, bool ukrainian) {
        if (!researchDisplay_) return;
        researchDisplay_->setSettings(settings);
        researchDisplay_->setConnected(connected, ukrainian);
    }
    bool hasInput() const { return typeHasInput(type_); }
    bool hasOutput() const { return typeHasOutput(type_); }
    QPointF inputPort() const { return QPointF(0.0, blockHeight() * 0.5); }
    QPointF outputPort() const { return QPointF(blockWidth(), blockHeight() * 0.5); }
    QPointF controlTopPort() const { return QPointF(blockWidth() * 0.5, 0.0); }
    QPointF controlBottomPort() const { return QPointF(blockWidth() * 0.5, blockHeight()); }
    QPointF inputScenePos() const { return mapToScene(inputPort()); }
    QPointF outputScenePos() const { return mapToScene(outputPort()); }
    QPointF controlOutputScenePos(const QPointF &targetPosition) const {
        return mapToScene(targetPosition.y() < sceneBoundingRect().center().y() ? controlTopPort() : controlBottomPort());
    }
    QPointF controlInputScenePos(const QPointF &sourcePosition) const {
        return mapToScene(sourcePosition.y() < sceneBoundingRect().center().y() ? controlTopPort() : controlBottomPort());
    }
    bool inputContains(const QPointF &pos) const {
        return hasInput() && QLineF(inputScenePos(), pos).length() <= kPortRadius + 5.0;
    }
    bool outputContains(const QPointF &pos) const {
        if (!hasOutput()) return false;
        if (isVerticalControlSource()) {
            return QLineF(mapToScene(controlTopPort()), pos).length() <= kPortRadius + 5.0 ||
                   QLineF(mapToScene(controlBottomPort()), pos).length() <= kPortRadius + 5.0;
        }
        return QLineF(outputScenePos(), pos).length() <= kPortRadius + 5.0;
    }
    bool controlInputContains(const QPointF &pos, const QString &controlType) const {
        if (!acceptsControlType(type_, controlType)) return false;
        return QLineF(mapToScene(controlTopPort()), pos).length() <= kPortRadius + 5.0 ||
               QLineF(mapToScene(controlBottomPort()), pos).length() <= kPortRadius + 5.0;
    }
    void setTitle(const QString &title, bool custom) {
        title_ = title;
        customTitle_ = custom;
        update();
    }
    void setControlState(bool active, bool ukrainian) {
        controlActive_ = active;
        if (type_ == QStringLiteral("run_control"))
            controlLabel_ = active ? (ukrainian ? QStringLiteral("СТОП") : QStringLiteral("STOP"))
                                   : (ukrainian ? QStringLiteral("СТАРТ") : QStringLiteral("START"));
        else if (type_ == QStringLiteral("enable_control"))
            controlLabel_ = active ? (ukrainian ? QStringLiteral("ВИКЛ") : QStringLiteral("OFF"))
                                   : (ukrainian ? QStringLiteral("ВКЛ") : QStringLiteral("ON"));
        update();
    }
    void setFineTuneRangeHz(double rangeHz) {
        if (fineTuneWidget_) fineTuneWidget_->setRangeHz(rangeHz);
    }
    int vfoIndex() const { return vfoIndex_; }
    void setVfoIndex(int index) {
        vfoIndex_ = index;
        update();
    }
    void setMiniVfoSpectrum(const std::vector<float> &frequencies,
                            const std::vector<float> &levels,
                            double centerHz,
                            double bandwidthHz,
                            const QString &name) {
        if (miniVfoDisplay_) miniVfoDisplay_->setSpectrum(frequencies, levels, centerHz, bandwidthHz, name);
    }
    void setMiniVfoStatus(const QString &status) {
        if (miniVfoDisplay_) miniVfoDisplay_->setStatus(status);
    }
    void addConnection(DspConnectionItem *edge) {
        if (edge && !connections_.contains(edge)) connections_.append(edge);
    }
    void removeConnection(DspConnectionItem *edge) { connections_.removeAll(edge); }
    QList<DspConnectionItem*> connections() const { return connections_; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override {
        if (isVisualBlock() && event->button() == Qt::LeftButton && resizeHandleRect().contains(event->pos())) {
            resizing_ = true;
            resizeStartScene_ = event->scenePos();
            resizeStartSize_ = QSizeF(visualWidth_, visualHeight_);
            setFlag(ItemIsMovable, false);
            setCursor(Qt::SizeFDiagCursor);
            event->accept();
            return;
        }
        setCursor(Qt::ClosedHandCursor);
        QGraphicsItem::mousePressEvent(event);
    }
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override {
        if (resizing_) {
            const QPointF delta = event->scenePos() - resizeStartScene_;
            setVisualSize(resizeStartSize_.width() + delta.x(), resizeStartSize_.height() + delta.y());
            event->accept();
            return;
        }
        QGraphicsItem::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override {
        if (resizing_) {
            resizing_ = false;
            setFlag(ItemIsMovable, true);
            setCursor(Qt::OpenHandCursor);
            event->accept();
            return;
        }
        setCursor(Qt::OpenHandCursor);
        QGraphicsItem::mouseReleaseEvent(event);
    }

private:
    void updateConnections();
    QRectF resizeHandleRect() const { return QRectF(blockWidth() - 18.0, blockHeight() - 18.0, 16.0, 16.0); }
    void resizeEmbeddedWidget() {
        const QSize size(std::max(1, int(visualWidth_ - 16.0)),
                         std::max(1, int(visualHeight_ - 51.0)));
        if (miniVfoDisplay_) miniVfoDisplay_->setFixedSize(size);
        if (researchDisplay_) researchDisplay_->setFixedSize(size);
    }

    QString id_;
    QString type_;
    QString title_;
    bool customTitle_ = false;
    bool controlActive_ = false;
    QString controlLabel_;
    FineTuneScaleWidget *fineTuneWidget_ = nullptr;
    QGraphicsProxyWidget *fineTuneProxy_ = nullptr;
    MiniVfoDisplay *miniVfoDisplay_ = nullptr;
    QGraphicsProxyWidget *miniVfoProxy_ = nullptr;
    DspResearchWidget *researchDisplay_ = nullptr;
    QGraphicsProxyWidget *researchProxy_ = nullptr;
    qreal visualWidth_ = kVfoDisplayBlockWidth;
    qreal visualHeight_ = kVfoDisplayBlockHeight;
    bool resizing_ = false;
    QPointF resizeStartScene_;
    QSizeF resizeStartSize_;
    QJsonObject settings_;
    int vfoIndex_ = -1;
    QList<DspConnectionItem*> connections_;
};

class DspConnectionItem final : public QGraphicsPathItem {
public:
    DspConnectionItem(DspBlockItem *source, DspBlockItem *target) : source_(source), target_(target) {
        setZValue(-5.0);
        setFlag(ItemIsSelectable, true);
        if (source_) source_->addConnection(this);
        if (target_) target_->addConnection(this);
        updatePath();
    }
    ~DspConnectionItem() override {
        if (source_) source_->removeConnection(this);
        if (target_) target_->removeConnection(this);
    }
    DspBlockItem *source() const { return source_; }
    DspBlockItem *target() const { return target_; }
    bool isControlConnection() const { return source_ && source_->isVerticalControlSource(); }
    void updatePath() {
        if (!source_ || !target_) return;
        QPainterPath path;
        if (isControlConnection()) {
            const QPointF from = source_->controlOutputScenePos(target_->sceneBoundingRect().center());
            const QPointF to = target_->controlInputScenePos(source_->sceneBoundingRect().center());
            const qreal bend = std::max<qreal>(32.0, qAbs(to.y() - from.y()) * 0.45);
            path.moveTo(from);
            const qreal direction = to.y() < from.y() ? -bend : bend;
            path.cubicTo(from + QPointF(0.0, direction), to - QPointF(0.0, direction), to);
        } else {
            const QPointF from = source_->outputScenePos();
            const QPointF to = target_->inputScenePos();
            const qreal bend = std::max<qreal>(45.0, qAbs(to.x() - from.x()) * 0.45);
            path.moveTo(from);
            path.cubicTo(from + QPointF(bend, 0.0), to - QPointF(bend, 0.0), to);
        }
        setPath(path);
        const QColor normal = isControlConnection() ? QColor(190, 122, 235) : QColor(104, 188, 255);
        setPen(QPen(isSelected() ? QColor(255, 194, 72) : normal,
                    isSelected() ? 3.0 : 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    QPainterPath shape() const override {
        QPainterPathStroker stroker;
        stroker.setWidth(12.0);
        return stroker.createStroke(path());
    }
protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override {
        const QVariant result = QGraphicsPathItem::itemChange(change, value);
        if (change == ItemSelectedHasChanged) updatePath();
        return result;
    }
private:
    DspBlockItem *source_ = nullptr;
    DspBlockItem *target_ = nullptr;
};

void DspBlockItem::updateConnections() {
    for (DspConnectionItem *edge : connections_) if (edge) edge->updatePath();
}

QVariant DspBlockItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    const QVariant result = QGraphicsItem::itemChange(change, value);
    if (change == ItemPositionHasChanged) {
        const auto snapshot = connections_;
        for (DspConnectionItem *edge : snapshot) if (edge) edge->updatePath();
    }
    return result;
}

class DspFlowView final : public QGraphicsView {
public:
    explicit DspFlowView(QGraphicsScene *scene, QWidget *parent = nullptr) : QGraphicsView(scene, parent) {
        setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorViewCenter);
        setDragMode(QGraphicsView::RubberBandDrag);
        setBackgroundBrush(QColor(20, 23, 28));
    }
protected:
    void wheelEvent(QWheelEvent *event) override {
        for (QGraphicsItem *item = itemAt(event->position().toPoint()); item; item = item->parentItem()) {
            auto *block = dynamic_cast<DspBlockItem*>(item);
            if (block && block->blockType() == QStringLiteral("fine_tune_control")) {
                QGraphicsView::wheelEvent(event);
                return;
            }
        }
        const qreal factor = event->angleDelta().y() > 0 ? 1.15 : (1.0 / 1.15);
        const qreal next = transform().m11() * factor;
        if (next >= 0.22 && next <= 4.5) scale(factor, factor);
        event->accept();
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::MiddleButton) {
            panning_ = true;
            panStart_ = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QGraphicsView::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (panning_) {
            const QPoint delta = event->pos() - panStart_;
            panStart_ = event->pos();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            event->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::MiddleButton && panning_) {
            panning_ = false;
            unsetCursor();
            event->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(event);
    }
private:
    bool panning_ = false;
    QPoint panStart_;
};

} // namespace

class DspFlowScene final : public QGraphicsScene {
public:
    explicit DspFlowScene(QObject *parent = nullptr) : QGraphicsScene(parent) {
        setSceneRect(-1800.0, -1200.0, 3600.0, 2400.0);
    }

    std::function<void()> changed;
    std::function<void(const QString&, const QPointF&)> addRequested;
    std::function<void(const QString&, const QString&)> activated;
    std::function<void(const QString&, const QString&, const QString&, const QString&)> controlTriggered;
    std::function<void(double)> fineTuneDelta;
    std::function<QString(const QString&)> standardTitle;
    std::function<void(const QString&)> statusChanged;
    bool ukrainian = false;

    DspBlockItem *addBlock(const QString &type, const QString &title, const QPointF &position,
                           const QString &id = QString(), bool customTitle = false, bool notify = true,
                           int requestedVfoIndex = -1) {
        int vfoIndex = requestedVfoIndex;
        if (type == QStringLiteral("vfo_channel") && vfoIndex < 0) {
            QSet<int> used;
            for (QGraphicsItem *item : items()) {
                if (auto *existing = dynamic_cast<DspBlockItem *>(item);
                    existing && existing->blockType() == QStringLiteral("vfo_channel")) {
                    used.insert(existing->vfoIndex());
                }
            }
            vfoIndex = 0;
            while (used.contains(vfoIndex)) ++vfoIndex;
        }
        QString resolvedTitle = title;
        if (type == QStringLiteral("vfo_channel") && vfoIndex >= 0 && !customTitle) {
            resolvedTitle += QStringLiteral(" %1").arg(vfoIndex + 1);
        }
        auto *block = new DspBlockItem(id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id,
                                       type, resolvedTitle, customTitle,
                                       [this](double deltaHz) { if (fineTuneDelta) fineTuneDelta(deltaHz); },
                                       vfoIndex);
        addItem(block);
        block->setPos(position);
        if (notify) notifyChanged();
        return block;
    }

    bool addConnection(DspBlockItem *source, DspBlockItem *target, bool notify = true) {
        if (!source || !target || source == target || !source->hasOutput()) return false;
        const bool control = source->isVerticalControlSource();
        if (control ? !acceptsControlType(target->blockType(), source->blockType()) : !target->hasInput()) return false;
        for (QGraphicsItem *item : items()) {
            auto *edge = dynamic_cast<DspConnectionItem*>(item);
            if (edge && edge->source() == source && edge->target() == target) return false;
        }
        QList<DspConnectionItem*> oldInputs;
        for (QGraphicsItem *item : items()) {
            auto *edge = dynamic_cast<DspConnectionItem*>(item);
            if (!edge) continue;
            if (control) {
                if (edge->source() == source ||
                    (edge->isControlConnection() && edge->target() == target &&
                     edge->source()->blockType() == source->blockType())) oldInputs.append(edge);
            } else if (!edge->isControlConnection() && edge->target() == target) {
                oldInputs.append(edge);
            }
        }
        for (DspConnectionItem *edge : oldInputs) delete edge;
        addItem(new DspConnectionItem(source, target));
        refreshResearchDisplays();
        if (notify) notifyChanged();
        return true;
    }

    void resetDefault(bool notify = true) {
        clearGraph(false);
        DspBlockItem *run = addBlock(QStringLiteral("run_control"), titleFor(QStringLiteral("run_control")),
                                     QPointF(-396.0, -125.0), QString(), false, false);
        DspBlockItem *source = addBlock(QStringLiteral("receiver_source"), titleFor(QStringLiteral("receiver_source")),
                                        QPointF(-430.0, -34.0), QString(), false, false);
        DspBlockItem *frequency = addBlock(QStringLiteral("frequency_control"), titleFor(QStringLiteral("frequency_control")),
                                           QPointF(-430.0, 78.0), QString(), false, false);
        DspBlockItem *filter = addBlock(QStringLiteral("channel_filter"), titleFor(QStringLiteral("channel_filter")),
                                        QPointF(-210.0, -34.0), QString(), false, false);
        DspBlockItem *demod = addBlock(QStringLiteral("demodulator"), titleFor(QStringLiteral("demodulator")),
                                       QPointF(10.0, -34.0), QString(), false, false);
        DspBlockItem *audio = addBlock(QStringLiteral("audio_filter"), titleFor(QStringLiteral("audio_filter")),
                                       QPointF(230.0, -34.0), QString(), false, false);
        DspBlockItem *output = addBlock(QStringLiteral("audio_output"), titleFor(QStringLiteral("audio_output")),
                                        QPointF(450.0, -34.0), QString(), false, false);
        DspBlockItem *fft = addBlock(QStringLiteral("fft"), titleFor(QStringLiteral("fft")),
                                     QPointF(-210.0, -155.0), QString(), false, false);
        addConnection(run, source, false);
        addConnection(frequency, source, false);
        addConnection(source, filter, false);
        addConnection(source, fft, false);
        addConnection(filter, demod, false);
        addConnection(demod, audio, false);
        addConnection(audio, output, false);
        if (notify) notifyChanged();
    }

    void clearGraph(bool notify = true) {
        cancelPendingConnection();
        const auto snapshot = items();
        for (QGraphicsItem *item : snapshot) if (dynamic_cast<DspConnectionItem*>(item)) delete item;
        const auto remaining = items();
        for (QGraphicsItem *item : remaining) if (dynamic_cast<DspBlockItem*>(item)) delete item;
        if (notify) notifyChanged();
    }

    void deleteSelection() {
        QList<DspConnectionItem*> edges;
        QList<DspBlockItem*> blocks;
        for (QGraphicsItem *item : selectedItems()) {
            if (auto *edge = dynamic_cast<DspConnectionItem*>(item)) {
                edges.append(edge);
            } else if (auto *block = dynamic_cast<DspBlockItem*>(item)) {
                blocks.append(block);
                for (DspConnectionItem *edge : block->connections()) if (!edges.contains(edge)) edges.append(edge);
            }
        }
        if (edges.isEmpty() && blocks.isEmpty()) return;
        for (DspConnectionItem *edge : edges) delete edge;
        for (DspBlockItem *block : blocks) delete block;
        refreshResearchDisplays();
        notifyChanged();
    }

    void renameSelection() {
        DspBlockItem *block = selectedBlock();
        if (!block) return;
        bool ok = false;
        const QString title = QInputDialog::getText(nullptr,
                                                    ukrainian ? QStringLiteral("Назва блока") : QStringLiteral("Block name"),
                                                    ukrainian ? QStringLiteral("Назва:") : QStringLiteral("Name:"),
                                                    QLineEdit::Normal, block->title(), &ok).trimmed();
        if (ok && !title.isEmpty() && title != block->title()) {
            block->setTitle(title, true);
            notifyChanged();
        }
    }

    void activateSelection() {
        if (DspBlockItem *block = selectedBlock()) {
            if (block->isMiniControlBlock()) triggerControl(block);
            else if (block->blockType() == QStringLiteral("fine_tune_control"))
                setStatus(ukrainian ? QStringLiteral("Точне налаштування працює безпосередньо на шкалі")
                                    : QStringLiteral("Fine tune is controlled directly on the scale"));
            else if (activated) activated(block->blockType(), block->id());
        }
    }

    void setControlStates(bool receiverRunning, const QHash<QString, bool> &enabledByBlockType) {
        for (QGraphicsItem *item : items()) {
            auto *block = dynamic_cast<DspBlockItem*>(item);
            if (!block || !block->isMiniControlBlock()) continue;
            bool active = false;
            if (block->blockType() == QStringLiteral("run_control")) {
                active = receiverRunning;
            } else if (DspBlockItem *target = controlTargetFor(block)) {
                active = enabledByBlockType.value(target->blockType(), false);
            }
            block->setControlState(active, ukrainian);
        }
    }

    void setFineTuneRangeHz(double rangeHz) {
        for (QGraphicsItem *item : items()) {
            if (auto *block = dynamic_cast<DspBlockItem*>(item)) block->setFineTuneRangeHz(rangeHz);
        }
    }

    QJsonObject blockSettings(const QString &blockId) const {
        DspBlockItem *block = blockById(blockId);
        return block ? block->settings() : QJsonObject();
    }

    bool setBlockSettings(const QString &blockId, const QJsonObject &settings) {
        DspBlockItem *block = blockById(blockId);
        if (!block ||
            (!isResearchSettingsType(block->blockType()) &&
             block->blockType() != QStringLiteral("vfo_spectrum") &&
             block->blockType() != QStringLiteral("vfo_waterfall"))) {
            return false;
        }
        block->setSettings(settings);
        if (isResearchSettingsType(block->blockType())) refreshResearchDisplays();
        notifyChanged();
        return true;
    }

    void updateMultiVfoSpectrum(const QString &configurationJson,
                                const std::vector<float> &frequencies,
                                const std::vector<float> &levels) {
        const QJsonDocument document = QJsonDocument::fromJson(configurationJson.toUtf8());
        const QJsonArray channels = document.object().value(QStringLiteral("channels")).toArray();
        if (channels.isEmpty()) return;
        for (QGraphicsItem *item : items()) {
            auto *display = dynamic_cast<DspBlockItem *>(item);
            if (!display || (display->blockType() != QStringLiteral("vfo_spectrum") &&
                             display->blockType() != QStringLiteral("vfo_waterfall"))) continue;
            int channelIndex = -1;
            for (QGraphicsItem *edgeItem : items()) {
                auto *edge = dynamic_cast<DspConnectionItem *>(edgeItem);
                if (edge && !edge->isControlConnection() && edge->target() == display && edge->source() &&
                    edge->source()->blockType() == QStringLiteral("vfo_channel")) {
                    channelIndex = edge->source()->vfoIndex();
                    break;
                }
            }
            if (channelIndex < 0) {
                display->setMiniVfoStatus(ukrainian
                    ? QStringLiteral("Під'єднайте блок Канал VFO")
                    : QStringLiteral("Connect a VFO channel block"));
                continue;
            }
            if (channelIndex >= channels.size()) {
                display->setMiniVfoStatus(ukrainian
                    ? QStringLiteral("Для цього номера VFO немає каналу")
                    : QStringLiteral("No channel for this VFO index"));
                continue;
            }
            const QJsonObject channel = channels.at(channelIndex).toObject();
            if (!channel.value(QStringLiteral("enabled")).toBool(true)) {
                display->setMiniVfoStatus(ukrainian
                    ? QStringLiteral("Канал VFO вимкнений")
                    : QStringLiteral("VFO channel is disabled"));
                continue;
            }
            display->setMiniVfoSpectrum(frequencies, levels,
                                        channel.value(QStringLiteral("frequencyHz")).toDouble(),
                                        channel.value(QStringLiteral("bandwidthHz")).toDouble(12500.0),
                                        channel.value(QStringLiteral("name")).toString(QStringLiteral("VFO %1").arg(channelIndex + 1)));
        }
    }

    DspBlockItem *blockById(const QString &blockId) const {
        for (QGraphicsItem *item : items()) {
            auto *block = dynamic_cast<DspBlockItem *>(item);
            if (block && block->id() == blockId) return block;
        }
        return nullptr;
    }

    DspBlockItem *vfoChannelForBlock(const QString &blockId) const {
        DspBlockItem *block = blockById(blockId);
        if (!block) return nullptr;
        if (block->blockType() == QStringLiteral("vfo_channel")) return block;
        if (block->blockType() != QStringLiteral("vfo_spectrum") &&
            block->blockType() != QStringLiteral("vfo_waterfall")) return nullptr;
        for (QGraphicsItem *item : items()) {
            auto *edge = dynamic_cast<DspConnectionItem *>(item);
            if (edge && !edge->isControlConnection() && edge->target() == block && edge->source() &&
                edge->source()->blockType() == QStringLiteral("vfo_channel")) {
                return edge->source();
            }
        }
        return nullptr;
    }

    int vfoIndexForBlock(const QString &blockId) const {
        DspBlockItem *channel = vfoChannelForBlock(blockId);
        return channel ? channel->vfoIndex() : -1;
    }

    bool assignVfoIndexToBlock(const QString &blockId, int channelIndex) {
        if (channelIndex < 0) return false;
        DspBlockItem *block = blockById(blockId);
        if (!block) return false;
        DspBlockItem *channel = vfoChannelForBlock(blockId);
        if (block->blockType() == QStringLiteral("vfo_channel")) {
            channel = block;
        } else if (block->blockType() == QStringLiteral("vfo_spectrum") ||
                   block->blockType() == QStringLiteral("vfo_waterfall")) {
            if (!channel || channel->vfoIndex() != channelIndex) {
                channel = nullptr;
                for (QGraphicsItem *item : items()) {
                    auto *candidate = dynamic_cast<DspBlockItem *>(item);
                    if (candidate && candidate->blockType() == QStringLiteral("vfo_channel") &&
                        candidate->vfoIndex() == channelIndex) {
                        channel = candidate;
                        break;
                    }
                }
                if (!channel) {
                    const QString title = standardTitle
                        ? standardTitle(QStringLiteral("vfo_channel"))
                        : QStringLiteral("VFO channel");
                    channel = addBlock(QStringLiteral("vfo_channel"), title,
                                       block->pos() - QPointF(250.0, 0.0),
                                       QString(), false, false, channelIndex);
                }
                addConnection(channel, block, false);
            }
        } else {
            return false;
        }
        if (!channel) return false;
        channel->setVfoIndex(channelIndex);
        if (!channel->customTitle()) {
            QString title = standardTitle
                ? standardTitle(QStringLiteral("vfo_channel"))
                : QStringLiteral("VFO channel");
            title += QStringLiteral(" %1").arg(channelIndex + 1);
            channel->setTitle(title, false);
        }
        notifyChanged();
        return true;
    }

    void addMultiVfoBranch(const QString &channelTitle,
                           const QString &spectrumTitle,
                           const QString &waterfallTitle,
                           int channelIndex) {
        const QRectF visible = itemsBoundingRect();
        const QPointF origin = visible.isEmpty() ? QPointF(0.0, 0.0)
                                                  : QPointF(visible.right() + 90.0, visible.top());
        DspBlockItem *channel = addBlock(QStringLiteral("vfo_channel"), channelTitle, origin,
                                         QString(), false, true, channelIndex);
        DspBlockItem *spectrum = addBlock(QStringLiteral("vfo_spectrum"), spectrumTitle,
                                          origin + QPointF(250.0, -70.0));
        DspBlockItem *waterfall = addBlock(QStringLiteral("vfo_waterfall"), waterfallTitle,
                                           origin + QPointF(250.0, 160.0));
        addConnection(channel, spectrum);
        addConnection(channel, waterfall);
        clearSelection();
        channel->setSelected(true);
        notifyChanged();
    }

    void refreshStandardTitles() {
        if (!standardTitle) return;
        for (QGraphicsItem *item : items()) {
            auto *block = dynamic_cast<DspBlockItem*>(item);
            if (block && !block->customTitle()) {
                QString title = standardTitle(block->blockType());
                if (block->blockType() == QStringLiteral("vfo_channel") && block->vfoIndex() >= 0) {
                    title += QStringLiteral(" %1").arg(block->vfoIndex() + 1);
                }
                block->setTitle(title, false);
            }
        }
        setControlStates(false, {});
    }

    QString configurationJson() const {
        QJsonArray blocks;
        QJsonArray connections;
        QList<DspBlockItem*> orderedBlocks;
        QList<DspConnectionItem*> orderedConnections;
        for (QGraphicsItem *item : items(Qt::AscendingOrder)) {
            if (auto *block = dynamic_cast<DspBlockItem*>(item)) orderedBlocks.append(block);
            else if (auto *edge = dynamic_cast<DspConnectionItem*>(item)) orderedConnections.append(edge);
        }
        for (DspBlockItem *block : orderedBlocks) {
            QJsonObject object;
            object.insert(QStringLiteral("id"), block->id());
            object.insert(QStringLiteral("type"), block->blockType());
            object.insert(QStringLiteral("title"), block->title());
            object.insert(QStringLiteral("customTitle"), block->customTitle());
            object.insert(QStringLiteral("x"), block->pos().x());
            object.insert(QStringLiteral("y"), block->pos().y());
            if (block->vfoIndex() >= 0) object.insert(QStringLiteral("vfoIndex"), block->vfoIndex());
            if (block->isVisualBlock()) {
                object.insert(QStringLiteral("width"), block->visualWidth());
                object.insert(QStringLiteral("height"), block->visualHeight());
            }
            if (!block->settings().isEmpty()) object.insert(QStringLiteral("settings"), block->settings());
            blocks.append(object);
        }
        for (DspConnectionItem *edge : orderedConnections) {
            if (!edge->source() || !edge->target()) continue;
            QJsonObject object;
            object.insert(QStringLiteral("source"), edge->source()->id());
            object.insert(QStringLiteral("target"), edge->target()->id());
            connections.append(object);
        }
        QJsonObject root;
        root.insert(QStringLiteral("version"), 3);
        root.insert(QStringLiteral("blocks"), blocks);
        root.insert(QStringLiteral("connections"), connections);
        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }

    bool loadConfiguration(const QString &json) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
        const QJsonObject root = document.object();
        const int version = root.value(QStringLiteral("version")).toInt(1);
        const QJsonArray blockArray = root.value(QStringLiteral("blocks")).toArray();
        const QJsonArray connectionArray = root.value(QStringLiteral("connections")).toArray();
        if (blockArray.isEmpty() || blockArray.size() > 128 || connectionArray.size() > 256) return false;

        clearGraph(false);
        QHash<QString, DspBlockItem*> byId;
        for (const QJsonValue &value : blockArray) {
            const QJsonObject object = value.toObject();
            const QString id = object.value(QStringLiteral("id")).toString();
            const QString type = object.value(QStringLiteral("type")).toString();
            if (id.isEmpty() || type.isEmpty() || byId.contains(id)) { clearGraph(false); return false; }
            const qreal x = object.value(QStringLiteral("x")).toDouble();
            const qreal y = object.value(QStringLiteral("y")).toDouble();
            if (!qIsFinite(x) || !qIsFinite(y) || qAbs(x) > 100000.0 || qAbs(y) > 100000.0) {
                clearGraph(false); return false;
            }
            const bool custom = object.value(QStringLiteral("customTitle")).toBool(false);
            QString title = object.value(QStringLiteral("title")).toString().trimmed();
            if (title.isEmpty() || !custom) title = titleFor(type);
            DspBlockItem *block = addBlock(type, title, QPointF(x, y), id, custom, false,
                                           object.value(QStringLiteral("vfoIndex")).toInt(-1));
            block->setSettings(object.value(QStringLiteral("settings")).toObject());
            if (block->isVisualBlock()) {
                block->setVisualSize(object.value(QStringLiteral("width")).toDouble(kVfoDisplayBlockWidth),
                                     object.value(QStringLiteral("height")).toDouble(kVfoDisplayBlockHeight));
            }
            byId.insert(id, block);
        }
        DspBlockItem *receiver = nullptr;
        DspBlockItem *frequencyController = nullptr;
        DspBlockItem *runController = nullptr;
        for (DspBlockItem *block : byId) {
            if (!receiver && (block->blockType() == QStringLiteral("receiver_source") ||
                              block->blockType() == QStringLiteral("iq_source") ||
                              block->blockType() == QStringLiteral("network_input") ||
                              block->blockType() == QStringLiteral("playback"))) receiver = block;
            if (!frequencyController && block->blockType() == QStringLiteral("frequency_control"))
                frequencyController = block;
            if (!runController && block->blockType() == QStringLiteral("run_control"))
                runController = block;
        }
        for (const QJsonValue &value : connectionArray) {
            const QJsonObject object = value.toObject();
            DspBlockItem *source = byId.value(object.value(QStringLiteral("source")).toString());
            DspBlockItem *target = byId.value(object.value(QStringLiteral("target")).toString());
            if (source && target && frequencyController && receiver) {
                if (source == receiver && target == frequencyController) {
                    addConnection(frequencyController, receiver, false);
                    continue;
                }
                if (source == frequencyController && target != receiver) {
                    addConnection(receiver, target, false);
                    continue;
                }
            }
            addConnection(source, target, false);
        }
        if (version < 2 && receiver && !runController) {
            runController = addBlock(QStringLiteral("run_control"), titleFor(QStringLiteral("run_control")),
                                     receiver->pos() + QPointF((kBlockWidth - kMiniBlockWidth) * 0.5, -72.0),
                                     QString(), false, false);
            addConnection(runController, receiver, false);
        }
        refreshResearchDisplays();
        return !byId.isEmpty();
    }

    void refreshResearchDisplayLanguage() { refreshResearchDisplays(); }

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override {
        QGraphicsScene::drawBackground(painter, rect);
        constexpr int minor = 20;
        constexpr int major = 100;
        QVarLengthArray<QLineF, 256> minorLines;
        QVarLengthArray<QLineF, 128> majorLines;
        const int left = int(qFloor(rect.left() / minor)) * minor;
        const int top = int(qFloor(rect.top() / minor)) * minor;
        for (int x = left; x < rect.right(); x += minor) {
            if (x % major == 0) majorLines.append(QLineF(x, rect.top(), x, rect.bottom()));
            else minorLines.append(QLineF(x, rect.top(), x, rect.bottom()));
        }
        for (int y = top; y < rect.bottom(); y += minor) {
            if (y % major == 0) majorLines.append(QLineF(rect.left(), y, rect.right(), y));
            else minorLines.append(QLineF(rect.left(), y, rect.right(), y));
        }
        painter->setPen(QPen(QColor(32, 37, 44), 1.0));
        painter->drawLines(minorLines.constData(), minorLines.size());
        painter->setPen(QPen(QColor(43, 49, 58), 1.0));
        painter->drawLines(majorLines.constData(), majorLines.size());
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            if (DspBlockItem *source = blockAtOutput(event->scenePos())) {
                pendingSource_ = source;
                temporaryConnection_ = addPath(QPainterPath(),
                                               QPen(QColor(255, 172, 72), 2.0, Qt::DashLine));
                temporaryConnection_->setZValue(-4.0);
                updateTemporaryConnection(event->scenePos());
                setStatus(ukrainian ? QStringLiteral("Оберіть вхід блока")
                                    : QStringLiteral("Select a block input"));
                event->accept();
                return;
            }
            if (auto *block = dynamic_cast<DspBlockItem*>(itemAt(event->scenePos(), QTransform()));
                block && block->isMiniControlBlock()) {
                pressedControl_ = block;
                controlPressPosition_ = event->scenePos();
            }
        }
        QGraphicsScene::mousePressEvent(event);
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override {
        if (pendingSource_) {
            updateTemporaryConnection(event->scenePos());
            event->accept();
            return;
        }
        QGraphicsScene::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override {
        if (pendingSource_) {
            DspBlockItem *target = blockAtInput(event->scenePos(), pendingSource_);
            DspBlockItem *source = pendingSource_;
            cancelPendingConnection();
            if (target && addConnection(source, target))
                setStatus(ukrainian ? QStringLiteral("З'єднання створено") : QStringLiteral("Connection created"));
            else
                setStatus(ukrainian ? QStringLiteral("Готово") : QStringLiteral("Ready"));
            event->accept();
            return;
        }
        QGraphicsScene::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && pressedControl_) {
            DspBlockItem *block = pressedControl_;
            pressedControl_ = nullptr;
            if (QLineF(controlPressPosition_, event->scenePos()).length() < 5.0)
                triggerControl(block);
        }
        notifyChanged();
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override {
        if (auto *block = dynamic_cast<DspBlockItem*>(itemAt(event->scenePos(), QTransform()))) {
            if (block->isMiniControlBlock()) {
                event->accept();
                return;
            }
            clearSelection();
            block->setSelected(true);
            QGraphicsScene::mouseDoubleClickEvent(event);
            activateSelection();
            return;
        }
        QGraphicsScene::mouseDoubleClickEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
            deleteSelection();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            cancelPendingConnection();
            setStatus(ukrainian ? QStringLiteral("Готово") : QStringLiteral("Ready"));
            event->accept();
            return;
        }
        QGraphicsScene::keyPressEvent(event);
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override {
        QGraphicsItem *item = itemAt(event->scenePos(), QTransform());
        if (auto *block = dynamic_cast<DspBlockItem*>(item)) {
            clearSelection();
            block->setSelected(true);
            QMenu menu;
            QAction *open = menu.addAction(ukrainian ? QStringLiteral("Відкрити налаштування")
                                                      : QStringLiteral("Open settings"));
            open->setEnabled(block->blockType() != QStringLiteral("fine_tune_control"));
            menu.addSeparator();
            QAction *rename = menu.addAction(ukrainian ? QStringLiteral("Перейменувати") : QStringLiteral("Rename"));
            QAction *remove = menu.addAction(ukrainian ? QStringLiteral("Видалити") : QStringLiteral("Delete"));
            QAction *chosen = menu.exec(event->screenPos());
            if (chosen == open) activateSelection();
            else if (chosen == rename) renameSelection();
            else if (chosen == remove) deleteSelection();
            return;
        }
        if (auto *edge = dynamic_cast<DspConnectionItem*>(item)) {
            clearSelection();
            edge->setSelected(true);
            QMenu menu;
            QAction *remove = menu.addAction(ukrainian ? QStringLiteral("Видалити з'єднання")
                                                        : QStringLiteral("Delete connection"));
            if (menu.exec(event->screenPos()) == remove) deleteSelection();
            return;
        }

        QMenu menu;
        QMenu *add = menu.addMenu(ukrainian ? QStringLiteral("Додати блок") : QStringLiteral("Add block"));
        const QStringList categories = {QStringLiteral("signal_path"), QStringLiteral("filters"),
                                        QStringLiteral("scanning"), QStringLiteral("visualization"),
                                        QStringLiteral("measurement"), QStringLiteral("digital"),
                                        QStringLiteral("navigation"), QStringLiteral("io_tools")};
        QHash<QString, QMenu*> categoryMenus;
        for (const QString &category : categories)
            categoryMenus.insert(category, add->addMenu(categoryTitle(category, ukrainian)));
        for (const QString &type : allBlockTypes()) {
            QAction *action = categoryMenus.value(blockCategory(type), add)->addAction(titleFor(type));
            action->setData(type);
        }
        QAction *chosen = menu.exec(event->screenPos());
        if (chosen && addRequested) addRequested(chosen->data().toString(), event->scenePos());
    }

private:
    QString titleFor(const QString &type) const { return standardTitle ? standardTitle(type) : type; }
    DspBlockItem *selectedBlock() const {
        for (QGraphicsItem *item : selectedItems())
            if (auto *block = dynamic_cast<DspBlockItem*>(item)) return block;
        return nullptr;
    }
    void refreshResearchDisplays() {
        const QList<QGraphicsItem *> sceneItems = items();
        for (QGraphicsItem *item : sceneItems) {
            auto *view = dynamic_cast<DspBlockItem *>(item);
            if (!view || !isResearchViewType(view->blockType())) continue;
            DspBlockItem *settingsBlock = nullptr;
            const QString expectedType = settingsTypeForResearchView(view->blockType());
            for (QGraphicsItem *edgeItem : sceneItems) {
                auto *edge = dynamic_cast<DspConnectionItem *>(edgeItem);
                if (edge && !edge->isControlConnection() && edge->target() == view && edge->source() &&
                    edge->source()->blockType() == expectedType) {
                    settingsBlock = edge->source();
                    break;
                }
            }
            view->setResearchConnection(settingsBlock != nullptr,
                                        settingsBlock ? settingsBlock->settings() : QJsonObject(),
                                        ukrainian);
        }
    }
    DspBlockItem *blockAtInput(const QPointF &pos, DspBlockItem *source) const {
        for (QGraphicsItem *item : items(pos, Qt::IntersectsItemBoundingRect, Qt::DescendingOrder))
            if (auto *block = dynamic_cast<DspBlockItem*>(item); block) {
                if (source && source->isVerticalControlSource()) {
                    if (block->controlInputContains(pos, source->blockType())) return block;
                } else if (block->inputContains(pos)) {
                    return block;
                }
            }
        return nullptr;
    }
    DspBlockItem *blockAtOutput(const QPointF &pos) const {
        for (QGraphicsItem *item : items(pos, Qt::IntersectsItemBoundingRect, Qt::DescendingOrder))
            if (auto *block = dynamic_cast<DspBlockItem*>(item); block && block->outputContains(pos)) return block;
        return nullptr;
    }
    void updateTemporaryConnection(const QPointF &to) {
        if (!pendingSource_ || !temporaryConnection_) return;
        QPainterPath path;
        if (pendingSource_->isVerticalControlSource()) {
            const QPointF from = pendingSource_->controlOutputScenePos(to);
            const qreal bend = std::max<qreal>(32.0, qAbs(to.y() - from.y()) * 0.45);
            const qreal direction = to.y() < from.y() ? -bend : bend;
            path.moveTo(from);
            path.cubicTo(from + QPointF(0.0, direction), to - QPointF(0.0, direction), to);
        } else {
            const QPointF from = pendingSource_->outputScenePos();
            const qreal bend = std::max<qreal>(45.0, qAbs(to.x() - from.x()) * 0.45);
            path.moveTo(from);
            path.cubicTo(from + QPointF(bend, 0.0), to - QPointF(bend, 0.0), to);
        }
        temporaryConnection_->setPath(path);
    }
    DspBlockItem *controlTargetFor(DspBlockItem *control) const {
        if (!control) return nullptr;
        for (QGraphicsItem *item : items()) {
            auto *edge = dynamic_cast<DspConnectionItem*>(item);
            if (edge && edge->isControlConnection() && edge->source() == control) return edge->target();
        }
        return nullptr;
    }
    void triggerControl(DspBlockItem *control) {
        if (!control || !controlTriggered) return;
        DspBlockItem *target = controlTargetFor(control);
        if (control->blockType() == QStringLiteral("enable_control") && !target) {
            setStatus(ukrainian ? QStringLiteral("Під'єднайте Вкл/Викл до сумісного блока")
                                : QStringLiteral("Connect On/Off to a compatible block"));
            return;
        }
        controlTriggered(control->blockType(), control->id(),
                         target ? target->blockType() : QString(),
                         target ? target->id() : QString());
    }
    void cancelPendingConnection() {
        pendingSource_ = nullptr;
        if (temporaryConnection_) { delete temporaryConnection_; temporaryConnection_ = nullptr; }
    }
    void notifyChanged() { if (changed) changed(); }
    void setStatus(const QString &status) { if (statusChanged) statusChanged(status); }

    DspBlockItem *pendingSource_ = nullptr;
    DspBlockItem *pressedControl_ = nullptr;
    QPointF controlPressPosition_;
    QGraphicsPathItem *temporaryConnection_ = nullptr;
};

DspFlowPanel::DspFlowPanel(QWidget *parent) : QDialog(parent, Qt::Window) {
    setModal(false);
    resize(1050, 680);
    setMinimumSize(720, 460);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(6);
    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(5);
    addButton_ = new QToolButton(this);
    addButton_->setPopupMode(QToolButton::InstantPopup);
    addButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addButton_->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    openButton_ = new QPushButton(this);
    renameButton_ = new QPushButton(this);
    deleteButton_ = new QPushButton(this);
    defaultButton_ = new QPushButton(this);
    fitButton_ = new QPushButton(this);
    statusLabel_ = new QLabel(this);
    statusLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusLabel_->setStyleSheet(QStringLiteral("color: #8793a2; padding-right: 4px;"));
    toolbar->addWidget(addButton_);
    toolbar->addWidget(openButton_);
    toolbar->addWidget(renameButton_);
    toolbar->addWidget(deleteButton_);
    toolbar->addWidget(defaultButton_);
    toolbar->addWidget(fitButton_);
    toolbar->addStretch(1);
    toolbar->addWidget(statusLabel_);
    rootLayout->addLayout(toolbar);

    scene_ = new DspFlowScene(this);
    view_ = new DspFlowView(scene_, this);
    rootLayout->addWidget(view_, 1);

    saveTimer_ = new QTimer(this);
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(450);
    scene_->changed = [this]() { scheduleConfigurationChanged(); };
    scene_->standardTitle = [this](const QString &type) { return blockTitle(type); };
    scene_->statusChanged = [this](const QString &status) { statusLabel_->setText(status); };
    scene_->addRequested = [this](const QString &type, const QPointF &position) {
        DspBlockItem *block = scene_->addBlock(type, blockTitle(type), position);
        emit blockCreated(type, block->id());
    };
    scene_->activated = [this](const QString &type, const QString &id) { emit blockActivated(type, id); };
    scene_->controlTriggered = [this](const QString &controlType, const QString &controlId,
                                      const QString &targetType, const QString &targetId) {
        emit controlTriggered(controlType, controlId, targetType, targetId);
    };
    scene_->fineTuneDelta = [this](double deltaHz) { emit fineTuneDeltaRequested(deltaHz); };

    auto *controlStateTimer = new QTimer(this);
    controlStateTimer->setInterval(180);
    connect(controlStateTimer, &QTimer::timeout, this, [this]() {
        if (isVisible()) emit controlStateRefreshRequested();
    });
    controlStateTimer->start();

    connect(openButton_, &QPushButton::clicked, this, [this]() { scene_->activateSelection(); });
    connect(renameButton_, &QPushButton::clicked, this, [this]() { scene_->renameSelection(); });
    connect(deleteButton_, &QPushButton::clicked, this, [this]() { scene_->deleteSelection(); });
    connect(defaultButton_, &QPushButton::clicked, this, [this]() {
        scene_->resetDefault();
        view_->fitInView(scene_->itemsBoundingRect().adjusted(-80.0, -80.0, 80.0, 80.0), Qt::KeepAspectRatio);
    });
    connect(fitButton_, &QPushButton::clicked, this, [this]() {
        const QRectF bounds = scene_->itemsBoundingRect();
        if (!bounds.isEmpty()) view_->fitInView(bounds.adjusted(-80.0, -80.0, 80.0, 80.0), Qt::KeepAspectRatio);
    });
    connect(saveTimer_, &QTimer::timeout, this, [this]() {
        if (!loading_) emit configurationChanged(configurationJson());
    });

    setLanguage(false);
    loading_ = true;
    scene_->resetDefault(false);
    loading_ = false;
    QTimer::singleShot(0, this, [this]() {
        view_->fitInView(scene_->itemsBoundingRect().adjusted(-80.0, -80.0, 80.0, 80.0), Qt::KeepAspectRatio);
    });
}

void DspFlowPanel::setLanguage(bool ukrainian) {
    ukrainian_ = ukrainian;
    setWindowTitle(ukrainian_ ? QStringLiteral("Конструктор DSP-шляху") : QStringLiteral("DSP Path Designer"));
    addButton_->setText(ukrainian_ ? QStringLiteral("Додати") : QStringLiteral("Add"));
    openButton_->setText(ukrainian_ ? QStringLiteral("Відкрити") : QStringLiteral("Open"));
    renameButton_->setText(ukrainian_ ? QStringLiteral("Назва") : QStringLiteral("Rename"));
    deleteButton_->setText(ukrainian_ ? QStringLiteral("Видалити") : QStringLiteral("Delete"));
    defaultButton_->setText(ukrainian_ ? QStringLiteral("Типова схема") : QStringLiteral("Default path"));
    fitButton_->setText(ukrainian_ ? QStringLiteral("Вмістити") : QStringLiteral("Fit"));
    addButton_->setToolTip(ukrainian_ ? QStringLiteral("Додати DSP-блок на поле")
                                     : QStringLiteral("Add a DSP block to the canvas"));
    openButton_->setToolTip(ukrainian_ ? QStringLiteral("Відкрити чинні налаштування модуля")
                                      : QStringLiteral("Open the existing module settings"));
    renameButton_->setToolTip(ukrainian_ ? QStringLiteral("Перейменувати обраний блок")
                                        : QStringLiteral("Rename the selected block"));
    deleteButton_->setToolTip(ukrainian_ ? QStringLiteral("Видалити обрані блоки або з'єднання")
                                        : QStringLiteral("Delete selected blocks or connections"));
    defaultButton_->setToolTip(ukrainian_ ? QStringLiteral("Відновити типову схему прийому")
                                         : QStringLiteral("Restore the default receive path"));
    fitButton_->setToolTip(ukrainian_ ? QStringLiteral("Вмістити всю схему у вікно")
                                     : QStringLiteral("Fit the whole graph in the window"));
    statusLabel_->setText(ukrainian_ ? QStringLiteral("Готово") : QStringLiteral("Ready"));
    scene_->ukrainian = ukrainian_;
    scene_->refreshStandardTitles();
    scene_->refreshResearchDisplayLanguage();
    rebuildAddMenu();
}

QString DspFlowPanel::configurationJson() const {
    return scene_ ? scene_->configurationJson() : QString();
}

bool DspFlowPanel::setConfigurationJson(const QString &json) {
    loading_ = true;
    bool loaded = false;
    if (!json.trimmed().isEmpty()) loaded = scene_->loadConfiguration(json);
    if (!loaded) scene_->resetDefault(false);
    loading_ = false;
    QTimer::singleShot(0, this, [this]() {
        const QRectF bounds = scene_->itemsBoundingRect();
        if (!bounds.isEmpty()) view_->fitInView(bounds.adjusted(-80.0, -80.0, 80.0, 80.0), Qt::KeepAspectRatio);
    });
    if (loaded) scheduleConfigurationChanged();
    return loaded;
}

void DspFlowPanel::setControlStates(bool receiverRunning, const QHash<QString, bool> &enabledByBlockType) {
    if (scene_) scene_->setControlStates(receiverRunning, enabledByBlockType);
}

void DspFlowPanel::setFineTuneRangeHz(double rangeHz) {
    if (scene_) scene_->setFineTuneRangeHz(rangeHz);
}

void DspFlowPanel::updateMultiVfoSpectrum(const QString &configurationJson,
                                          const std::vector<float> &frequencies,
                                          const std::vector<float> &levels) {
    if (scene_ && isVisible()) scene_->updateMultiVfoSpectrum(configurationJson, frequencies, levels);
}

void DspFlowPanel::addMultiVfoBranch(int channelIndex) {
    if (!scene_) return;
    scene_->addMultiVfoBranch(blockTitle(QStringLiteral("vfo_channel")),
                              blockTitle(QStringLiteral("vfo_spectrum")),
                              blockTitle(QStringLiteral("vfo_waterfall")),
                              channelIndex);
}

int DspFlowPanel::vfoIndexForBlock(const QString &blockId) const {
    return scene_ ? scene_->vfoIndexForBlock(blockId) : -1;
}

bool DspFlowPanel::assignVfoIndexToBlock(const QString &blockId, int channelIndex) {
    return scene_ && scene_->assignVfoIndexToBlock(blockId, channelIndex);
}

QJsonObject DspFlowPanel::blockSettings(const QString &blockId) const {
    return scene_ ? scene_->blockSettings(blockId) : QJsonObject();
}

bool DspFlowPanel::setBlockSettings(const QString &blockId, const QJsonObject &settings) {
    return scene_ && scene_->setBlockSettings(blockId, settings);
}

void DspFlowPanel::rebuildAddMenu() {
    if (addMenu_) {
        addButton_->setMenu(nullptr);
        delete addMenu_;
    }
    addMenu_ = new QMenu(addButton_);
    const QStringList categories = {QStringLiteral("signal_path"), QStringLiteral("filters"),
                                    QStringLiteral("scanning"), QStringLiteral("visualization"),
                                    QStringLiteral("measurement"), QStringLiteral("digital"),
                                    QStringLiteral("navigation"), QStringLiteral("io_tools")};
    QHash<QString, QMenu*> categoryMenus;
    for (const QString &category : categories)
        categoryMenus.insert(category, addMenu_->addMenu(categoryTitle(category, ukrainian_)));
    for (const QString &type : allBlockTypes()) {
        QAction *action = categoryMenus.value(blockCategory(type), addMenu_)->addAction(blockTitle(type));
        action->setData(type);
        connect(action, &QAction::triggered, this, [this, type]() { addBlock(type); });
    }
    addButton_->setMenu(addMenu_);
}

void DspFlowPanel::scheduleConfigurationChanged() {
    if (!loading_) saveTimer_->start();
}

void DspFlowPanel::addBlock(const QString &type) {
    const QPointF center = view_->mapToScene(view_->viewport()->rect().center());
    const qreal offset = qreal(scene_->items().size() % 6) * 18.0;
    DspBlockItem *block = scene_->addBlock(type, blockTitle(type), center + QPointF(offset, offset));
    scene_->clearSelection();
    block->setSelected(true);
    emit blockCreated(type, block->id());
}

QString DspFlowPanel::blockTitle(const QString &type) const {
    if (type == QStringLiteral("run_control")) return ukrainian_ ? QStringLiteral("СТАРТ") : QStringLiteral("START");
    if (type == QStringLiteral("enable_control")) return ukrainian_ ? QStringLiteral("ВКЛ") : QStringLiteral("ON");
    if (type == QStringLiteral("receiver_source")) return ukrainian_ ? QStringLiteral("Вхід приймача") : QStringLiteral("Receiver input");
    if (type == QStringLiteral("iq_source")) return ukrainian_ ? QStringLiteral("Джерело IQ") : QStringLiteral("IQ source");
    if (type == QStringLiteral("network_input")) return ukrainian_ ? QStringLiteral("Мережевий вхід") : QStringLiteral("Network input");
    if (type == QStringLiteral("playback")) return ukrainian_ ? QStringLiteral("Відтворення запису") : QStringLiteral("Recording playback");
    if (type == QStringLiteral("frequency_control")) return ukrainian_ ? QStringLiteral("Частоти") : QStringLiteral("Frequencies");
    if (type == QStringLiteral("fine_tune_control")) return ukrainian_ ? QStringLiteral("Точне налаштування") : QStringLiteral("Fine tune");
    if (type == QStringLiteral("channel_filter")) return ukrainian_ ? QStringLiteral("Канальний фільтр") : QStringLiteral("Channel filter");
    if (type == QStringLiteral("multi_vfo_channelizer")) return ukrainian_ ? QStringLiteral("Multi-VFO каналайзер") : QStringLiteral("Multi-VFO channelizer");
    if (type == QStringLiteral("vfo_channel")) return ukrainian_ ? QStringLiteral("Канал VFO") : QStringLiteral("VFO channel");
    if (type == QStringLiteral("vfo_spectrum")) return ukrainian_ ? QStringLiteral("Спектр VFO") : QStringLiteral("VFO spectrum");
    if (type == QStringLiteral("vfo_waterfall")) return ukrainian_ ? QStringLiteral("Водоспад VFO") : QStringLiteral("VFO waterfall");
    if (type == QStringLiteral("resampler")) return ukrainian_ ? QStringLiteral("Ресемплер") : QStringLiteral("Resampler");
    if (type == QStringLiteral("fft")) return ukrainian_ ? QStringLiteral("FFT / спектр") : QStringLiteral("FFT / spectrum");
    if (type == QStringLiteral("hf_interference")) return ukrainian_ ? QStringLiteral("HF лабораторія завад") : QStringLiteral("HF interference lab");
    if (type == QStringLiteral("demodulator")) return ukrainian_ ? QStringLiteral("Демодулятор") : QStringLiteral("Demodulator");
    if (type == QStringLiteral("audio_filter")) return ukrainian_ ? QStringLiteral("Аудіофільтри") : QStringLiteral("Audio filters");
    if (type.startsWith(QStringLiteral("filter_"))) {
        const QString id = type.mid(7);
        const QHash<QString, QString> uk = {
            {QStringLiteral("low_pass"), QStringLiteral("ФНЧ")}, {QStringLiteral("high_pass"), QStringLiteral("ФВЧ")},
            {QStringLiteral("band_pass"), QStringLiteral("Смуговий фільтр")}, {QStringLiteral("notch"), QStringLiteral("Notch")},
            {QStringLiteral("dc_blocker"), QStringLiteral("DC blocker")}, {QStringLiteral("de_emphasis"), QStringLiteral("FM деемфаза")},
            {QStringLiteral("parametric_eq"), QStringLiteral("Параметричний EQ")}, {QStringLiteral("low_shelf"), QStringLiteral("Low shelf")},
            {QStringLiteral("high_shelf"), QStringLiteral("High shelf")}, {QStringLiteral("adaptive_notch"), QStringLiteral("Адаптивний notch")},
            {QStringLiteral("noise_blanker"), QStringLiteral("Noise blanker")}, {QStringLiteral("cw"), QStringLiteral("CW фільтр")},
            {QStringLiteral("ctcss"), QStringLiteral("CTCSS suppression")}, {QStringLiteral("spectral_denoise"), QStringLiteral("Спектральний denoise")},
            {QStringLiteral("custom_fir"), QStringLiteral("Користувацький FIR")}, {QStringLiteral("gain"), QStringLiteral("Підсилення")},
            {QStringLiteral("compressor"), QStringLiteral("Компресор / AGC")}, {QStringLiteral("limiter"), QStringLiteral("Лімітер")},
            {QStringLiteral("noise_gate"), QStringLiteral("Noise gate")}};
        const QHash<QString, QString> en = {
            {QStringLiteral("low_pass"), QStringLiteral("Low-pass")}, {QStringLiteral("high_pass"), QStringLiteral("High-pass")},
            {QStringLiteral("band_pass"), QStringLiteral("Band-pass")}, {QStringLiteral("notch"), QStringLiteral("Notch")},
            {QStringLiteral("dc_blocker"), QStringLiteral("DC blocker")}, {QStringLiteral("de_emphasis"), QStringLiteral("FM de-emphasis")},
            {QStringLiteral("parametric_eq"), QStringLiteral("Parametric EQ")}, {QStringLiteral("low_shelf"), QStringLiteral("Low shelf")},
            {QStringLiteral("high_shelf"), QStringLiteral("High shelf")}, {QStringLiteral("adaptive_notch"), QStringLiteral("Adaptive notch")},
            {QStringLiteral("noise_blanker"), QStringLiteral("Noise blanker")}, {QStringLiteral("cw"), QStringLiteral("CW filter")},
            {QStringLiteral("ctcss"), QStringLiteral("CTCSS suppression")}, {QStringLiteral("spectral_denoise"), QStringLiteral("Spectral denoise")},
            {QStringLiteral("custom_fir"), QStringLiteral("Custom FIR")}, {QStringLiteral("gain"), QStringLiteral("Gain")},
            {QStringLiteral("compressor"), QStringLiteral("Compressor / AGC")}, {QStringLiteral("limiter"), QStringLiteral("Limiter")},
            {QStringLiteral("noise_gate"), QStringLiteral("Noise gate")}};
        return (ukrainian_ ? uk : en).value(id, id);
    }
    if (type == QStringLiteral("decoder")) return ukrainian_ ? QStringLiteral("Цифровий декодер") : QStringLiteral("Digital decoder");
    if (type == QStringLiteral("digital_audio_settings")) return ukrainian_ ? QStringLiteral("Налаштування цифрового аудіо") : QStringLiteral("Digital audio settings");
    if (type == QStringLiteral("digital_video_settings")) return ukrainian_ ? QStringLiteral("Налаштування цифрового відео") : QStringLiteral("Digital video settings");
    if (type == QStringLiteral("digital_text_output")) return ukrainian_ ? QStringLiteral("Вихід тексту") : QStringLiteral("Text output");
    if (type == QStringLiteral("digital_image_output")) return ukrainian_ ? QStringLiteral("Вихід зображення") : QStringLiteral("Image output");
    if (type == QStringLiteral("spectrum_display")) return ukrainian_ ? QStringLiteral("Спектр") : QStringLiteral("Spectrum display");
    if (type == QStringLiteral("waterfall_2d")) return ukrainian_ ? QStringLiteral("Водоспад 2D") : QStringLiteral("2D waterfall");
    if (type == QStringLiteral("waterfall_3d")) return ukrainian_ ? QStringLiteral("Водоспад 3D") : QStringLiteral("3D waterfall");
    if (type == QStringLiteral("second_spectrum")) return ukrainian_ ? QStringLiteral("Другий спектр") : QStringLiteral("Second spectrum");
    if (type == QStringLiteral("agile_scan")) return QStringLiteral("Agile scan");
    if (type == QStringLiteral("standard_scan")) return ukrainian_ ? QStringLiteral("Стандартний скан") : QStringLiteral("Standard scan");
    if (type == QStringLiteral("listening_scan")) return ukrainian_ ? QStringLiteral("Скан прослуховування") : QStringLiteral("Listening scan");
    if (type == QStringLiteral("spectrum_measurement")) return ukrainian_ ? QStringLiteral("Вимірювання спектра") : QStringLiteral("Spectrum measurement");
    if (type == QStringLiteral("zoom_spectrum")) return ukrainian_ ? QStringLiteral("Вузькосмуговий аналіз") : QStringLiteral("Zoom spectrum");
    if (type == QStringLiteral("zero_span")) return QStringLiteral("Zero-span");
    if (type == QStringLiteral("research_analysis")) return ukrainian_ ? QStringLiteral("Дослідницький аналіз") : QStringLiteral("Research analysis");
    if (type == QStringLiteral("oscilloscope")) return ukrainian_ ? QStringLiteral("Осцилограф: параметри") : QStringLiteral("Oscilloscope settings");
    if (type == QStringLiteral("constellation")) return ukrainian_ ? QStringLiteral("Сузір'я: параметри") : QStringLiteral("Constellation settings");
    if (type == QStringLiteral("eye_diagram")) return ukrainian_ ? QStringLiteral("Окова діаграма: параметри") : QStringLiteral("Eye diagram settings");
    if (type == QStringLiteral("digital_sync_lab")) return ukrainian_ ? QStringLiteral("Синхронізація: параметри") : QStringLiteral("Synchronization settings");
    if (type == QStringLiteral("oscilloscope_view")) return ukrainian_ ? QStringLiteral("Осцилограф: графік") : QStringLiteral("Oscilloscope plot");
    if (type == QStringLiteral("constellation_view")) return ukrainian_ ? QStringLiteral("Сузір'я: графік") : QStringLiteral("Constellation plot");
    if (type == QStringLiteral("eye_diagram_view")) return ukrainian_ ? QStringLiteral("Окова діаграма: графік") : QStringLiteral("Eye diagram plot");
    if (type == QStringLiteral("digital_sync_view")) return ukrainian_ ? QStringLiteral("Синхронізація: графік") : QStringLiteral("Synchronization plot");
    if (type == QStringLiteral("spur_suppression")) return ukrainian_ ? QStringLiteral("Придушення spur") : QStringLiteral("Spur suppression");
    if (type == QStringLiteral("spectrum_recorder")) return ukrainian_ ? QStringLiteral("Запис кадрів спектра") : QStringLiteral("Spectrum frame recorder");
    if (type == QStringLiteral("spectrum_replay")) return ukrainian_ ? QStringLiteral("Реплей спектра / IQ") : QStringLiteral("Spectrum / IQ replay");
    if (type == QStringLiteral("dmr_decoder")) return QStringLiteral("DMR");
    if (type == QStringLiteral("cw_decoder")) return ukrainian_ ? QStringLiteral("Декодер CW") : QStringLiteral("CW decoder");
    if (type == QStringLiteral("sstv_decoder")) return QStringLiteral("SSTV");
    if (type == QStringLiteral("digital_video")) return ukrainian_ ? QStringLiteral("Цифрове відео") : QStringLiteral("Digital video");
    if (type == QStringLiteral("dmr_hunter")) return QStringLiteral("DMR Hunter");
    if (type == QStringLiteral("fpv_hunter")) return QStringLiteral("FPV Hunter");
    if (type == QStringLiteral("digital_video_hunter")) return QStringLiteral("Digital Video Hunter");
    if (type == QStringLiteral("gnss_sdr")) return ukrainian_ ? QStringLiteral("GNSS через SDR") : QStringLiteral("GNSS via SDR");
    if (type == QStringLiteral("gnss_serial")) return ukrainian_ ? QStringLiteral("GNSS модуль / NMEA / UBX") : QStringLiteral("GNSS module / NMEA / UBX");
    if (type == QStringLiteral("qth_map")) return ukrainian_ ? QStringLiteral("Карта QTH") : QStringLiteral("QTH map");
    if (type == QStringLiteral("gpio")) return QStringLiteral("GPIO");
    if (type == QStringLiteral("recorder")) return ukrainian_ ? QStringLiteral("Записувач") : QStringLiteral("Recorder");
    if (type == QStringLiteral("network_output")) return ukrainian_ ? QStringLiteral("Мережевий вихід") : QStringLiteral("Network output");
    if (type == QStringLiteral("audio_output")) return ukrainian_ ? QStringLiteral("Аудіовихід") : QStringLiteral("Audio output");
    if (type == QStringLiteral("transmitter")) return ukrainian_ ? QStringLiteral("Передавач") : QStringLiteral("Transmitter");
    if (type == QStringLiteral("presets")) return ukrainian_ ? QStringLiteral("Пресети й калібрування") : QStringLiteral("Presets and calibration");
    if (type == QStringLiteral("calibration")) return ukrainian_ ? QStringLiteral("Калібрування приймача") : QStringLiteral("Receiver calibration");
    if (type == QStringLiteral("application_settings")) return ukrainian_ ? QStringLiteral("Загальні налаштування") : QStringLiteral("Application settings");
    return type;
}

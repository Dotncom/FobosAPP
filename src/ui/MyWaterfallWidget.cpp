#include "MyWaterfallWidget.h"
#include "radiosettings.h"
#include "waterfall3drenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <QLabel>

bool changebit=false;

namespace {
constexpr double AUTO_TUNE_WINDOW_FRACTION = 1.0 / 80.0;
constexpr double AUTO_TUNE_MIN_WINDOW_HZ = 300.0;
constexpr double AUTO_TUNE_MAX_WINDOW_HZ = 500000.0;

struct SignalSample {
    double frequency = 0.0;
    float level = 0.0f;
};

const std::array<QColor, 17> &legacyWaterfallPalette() {
    static const std::array<QColor, 17> palette = {{
        QColor("#000020"),
        QColor("#000050"),
        QColor("#000090"),
        QColor("#0000F0"),
        QColor("#0000FF"),
        QColor("#50F030"),
        QColor("#1E90FF"),
        QColor("#FFFFFF"),
        QColor("#FFFF00"),
        QColor("#FE6D16"),
        QColor("#FE6D16"),
        QColor("#FF0000"),
        QColor("#FF0000"),
        QColor("#C60000"),
        QColor("#9F0000"),
        QColor("#750000"),
        QColor("#4A0000"),
    }};
    return palette;
}

QColor legacyWaterfallColor(float normalizedValue, float contrastFactor, float sensitivityFactor) {
    if (!std::isfinite(normalizedValue)) {
        normalizedValue = 0.0f;
    }
    const float value = qBound(0.0f, normalizedValue * sensitivityFactor, 1.0f);
    const auto &palette = legacyWaterfallPalette();
    int index = static_cast<int>(value * static_cast<float>(palette.size() - 1));
    index = std::clamp(index, 0, static_cast<int>(palette.size() - 1));

    const QColor baseColor = palette[static_cast<std::size_t>(index)];
    const int blue = (baseColor.green() + baseColor.blue()) / 3;
    const int r = static_cast<int>(baseColor.red() * contrastFactor + blue * (1.0f - contrastFactor));
    const int g = static_cast<int>(baseColor.green() * contrastFactor + blue * (1.0f - contrastFactor));
    const int b = static_cast<int>(baseColor.blue() * contrastFactor + blue * (1.0f - contrastFactor));
    return QColor(r, g, b);
}

void writeWaterfallColor(float normalizedValue,
                         float contrastFactor,
                         float sensitivityFactor,
                         unsigned char *dest) {
    if (!dest) {
        return;
    }
    const QColor color = legacyWaterfallColor(normalizedValue, contrastFactor, sensitivityFactor);
    dest[0] = static_cast<unsigned char>(color.red());
    dest[1] = static_cast<unsigned char>(color.green());
    dest[2] = static_cast<unsigned char>(color.blue());
}
}

MyWaterfallWidget::MyWaterfallWidget(QWidget *parent)
    : QOpenGLWidget(parent), xMin(60000000), xMax(140000000), yMin(-120), yMax(0), contrast(10), sensitivity(10),
      levelMin(-120), levelMax(0), fftLength(32768), initialized(false), secondGraph(false),
      waterfall3DRenderer(std::make_unique<Waterfall3DRenderer>()) {
		waterfallTexture = 0;
    fpsOverlayLabel = new QLabel(QStringLiteral("FPS --"), this);
    fpsOverlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    fpsOverlayLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: rgb(120, 255, 150); background-color: rgba(0, 0, 0, 180); "
        "padding: 3px 6px; border: 1px solid rgba(120, 255, 150, 100); }"));
    fpsOverlayLabel->adjustSize();
    fpsOverlayLabel->hide();
    sliceOverlayLabel = new QLabel(this);
    sliceOverlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    sliceOverlayLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: white; background-color: rgba(12, 16, 22, 215); "
        "padding: 4px 7px; border: 1px solid rgba(120, 210, 255, 180); }"));
    sliceOverlayLabel->hide();
    sliceDetailsOverlayLabel = new QLabel(this);
    sliceDetailsOverlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    sliceDetailsOverlayLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: rgb(225, 240, 255); background-color: rgba(0, 0, 0, 190); "
        "padding: 5px 7px; border: 1px solid rgba(120, 210, 255, 120); }"));
    sliceDetailsOverlayLabel->hide();
}

MyWaterfallWidget::~MyWaterfallWidget() {
    makeCurrent();
    waterfall3DRenderer->releaseGpuResources();
    if (waterfallTexture != 0) {
        glDeleteTextures(1, &waterfallTexture);
        waterfallTexture = 0;
    }
    waterfallVbo.release();
    doneCurrent();
}


void MyWaterfallWidget::wheelEvent(QWheelEvent *event) {
    if ((frequencySliceMouseActive || spectrumFrameSliceMouseActive) &&
        activeDisplayMode != DisplayMode::Waterfall2D) {
        const QPoint angleDelta = event->angleDelta();
        const QPoint pixelDelta = event->pixelDelta();
        const int signedDelta = angleDelta.y() != 0
                                    ? angleDelta.y()
                                    : (angleDelta.x() != 0
                                           ? angleDelta.x()
                                           : (pixelDelta.y() != 0 ? pixelDelta.y() : pixelDelta.x()));
        if (signedDelta == 0) {
            event->accept();
            return;
        }
        {
            QMutexLocker locker(&mutex);
            if (frequencySliceMouseActive) {
                waterfall3DRenderer->stepFrequencySlice(signedDelta > 0 ? 1 : -1);
            } else {
                waterfall3DRenderer->stepSpectrumSlice(signedDelta > 0 ? 1 : -1);
            }
        }
        updateSliceOverlay(sliceOverlayAnchor);
        update();
        event->accept();
        return;
    }
    if (activeDisplayMode != DisplayMode::Waterfall2D &&
        event->modifiers().testFlag(Qt::ControlModifier)) {
        {
            QMutexLocker locker(&mutex);
            waterfall3DRenderer->zoomCamera(event->angleDelta().y());
        }
        update();
        event->accept();
        return;
    }
    emit scaleChanged(event->angleDelta().y() > 0 ? 1 : -1);
    event->accept();
}

void MyWaterfallWidget::mousePressEvent(QMouseEvent *event) {
    const bool sliceModifier = event->modifiers().testFlag(Qt::AltModifier) ||
                               event->modifiers().testFlag(Qt::ShiftModifier);
    const bool modifierFreeSlice = modifierFreeSliceInput &&
                                   !event->modifiers().testFlag(Qt::ControlModifier);
    if (activeDisplayMode != DisplayMode::Waterfall2D &&
        event->button() == Qt::LeftButton &&
        (sliceModifier || modifierFreeSlice)) {
        bool selected = false;
        {
            QMutexLocker locker(&mutex);
            selected = waterfall3DRenderer->beginFrequencySlice(event->x(),
                                                                event->y(),
                                                                width(),
                                                                height());
        }
        if (selected) {
            spectrumFrameSliceMouseActive = false;
            frequencySliceMouseActive = true;
            updateSliceOverlay(event->pos());
            update();
        }
        event->accept();
        return;
    }
    if (activeDisplayMode != DisplayMode::Waterfall2D &&
        event->button() == Qt::RightButton &&
        (sliceModifier || modifierFreeSlice)) {
        bool selected = false;
        {
            QMutexLocker locker(&mutex);
            selected = waterfall3DRenderer->beginSpectrumSlice(event->x(),
                                                               event->y(),
                                                               width(),
                                                               height());
        }
        if (selected) {
            frequencySliceMouseActive = false;
            spectrumFrameSliceMouseActive = true;
            updateSliceOverlay(event->pos());
            update();
        }
        event->accept();
        return;
    }
    if (activeDisplayMode != DisplayMode::Waterfall2D &&
        event->button() == Qt::LeftButton &&
        event->modifiers().testFlag(Qt::ControlModifier)) {
        cameraOrbitActive = true;
        cameraOrbitLastPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (activeDisplayMode != DisplayMode::Waterfall2D &&
        event->button() == Qt::RightButton &&
        event->modifiers().testFlag(Qt::ControlModifier)) {
        cameraPanActive = true;
        cameraPanLastPos = event->pos();
        setCursor(Qt::SizeAllCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        spectrumPanActive = true;
        spectrumPanMoved = false;
        spectrumPanButton = event->button();
        spectrumPanLastPos = event->pos();
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) {
        emit tuneContextRequested(frequencyAtX(event->x()), event->globalPos());
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        emit autoTuneRequested(signalCenterNearFrequency(frequencyAtX(event->x())));
        event->accept();
        return;
    }
    QOpenGLWidget::mousePressEvent(event);
}

void MyWaterfallWidget::mouseMoveEvent(QMouseEvent *event) {
    if (cameraPanActive) {
        const QPoint delta = event->pos() - cameraPanLastPos;
        cameraPanLastPos = event->pos();
        if (!delta.isNull()) {
            {
                QMutexLocker locker(&mutex);
                waterfall3DRenderer->panCamera(static_cast<float>(delta.x()),
                                               static_cast<float>(delta.y()),
                                               width(),
                                               height());
            }
            update();
        }
        event->accept();
        return;
    }
    if (cameraOrbitActive) {
        const QPoint delta = event->pos() - cameraOrbitLastPos;
        cameraOrbitLastPos = event->pos();
        if (!delta.isNull()) {
            {
                QMutexLocker locker(&mutex);
                waterfall3DRenderer->orbitCamera(static_cast<float>(delta.x()),
                                                 static_cast<float>(delta.y()));
            }
            update();
        }
        event->accept();
        return;
    }
    if (spectrumPanActive) {
        const int deltaPixels = event->pos().x() - spectrumPanLastPos.x();
        if (deltaPixels != 0) {
            spectrumPanMoved = true;
            spectrumPanLastPos = event->pos();
            emit panRequested(deltaPixels, width());
        }
        event->accept();
        return;
    }
    QOpenGLWidget::mouseMoveEvent(event);
}

void MyWaterfallWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (spectrumFrameSliceMouseActive && event->button() == Qt::RightButton) {
        {
            QMutexLocker locker(&mutex);
            waterfall3DRenderer->endSpectrumSlice();
        }
        spectrumFrameSliceMouseActive = false;
        hideSliceOverlay();
        update();
        event->accept();
        return;
    }
    if (frequencySliceMouseActive && event->button() == Qt::LeftButton) {
        {
            QMutexLocker locker(&mutex);
            waterfall3DRenderer->endFrequencySlice();
        }
        frequencySliceMouseActive = false;
        hideSliceOverlay();
        update();
        event->accept();
        return;
    }
    if (cameraOrbitActive && event->button() == Qt::LeftButton) {
        cameraOrbitActive = false;
        unsetCursor();
        event->accept();
        return;
    }
    if (cameraPanActive && event->button() == Qt::RightButton) {
        cameraPanActive = false;
        unsetCursor();
        event->accept();
        return;
    }
    if (spectrumPanActive && event->button() == spectrumPanButton) {
        const bool middleClickAutoTune = spectrumPanButton == Qt::MiddleButton && !spectrumPanMoved;
        spectrumPanActive = false;
        spectrumPanButton = Qt::NoButton;
        if (middleClickAutoTune) {
            emit autoTuneRequested(signalCenterNearFrequency(frequencyAtX(event->x())));
        }
        event->accept();
        return;
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

void MyWaterfallWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        spectrumPanActive = false;
        spectrumPanButton = Qt::NoButton;
        emit autoTuneRequested(signalCenterNearFrequency(frequencyAtX(event->x())));
        event->accept();
        return;
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

double MyWaterfallWidget::displayFrequencyAtX(int x) const {
    if (width() <= 0 || qFuzzyCompare(xMin, xMax)) {
        return xMin;
    }
    const double normalized = std::clamp(static_cast<double>(x) / (std::max)(1, width()), 0.0, 1.0);
    return xMin + normalized * (xMax - xMin);
}

double MyWaterfallWidget::actualFrequencyForDisplayFrequency(double displayFrequency) const {
    for (const ScanVisualSegment &segment : scanSegments) {
        if (!std::isfinite(segment.startHz) ||
            !std::isfinite(segment.endHz) ||
            !std::isfinite(segment.actualStartHz) ||
            !std::isfinite(segment.actualEndHz) ||
            segment.endHz <= segment.startHz ||
            displayFrequency < segment.startHz ||
            displayFrequency > segment.endHz) {
            continue;
        }
        const double ratio = (displayFrequency - segment.startHz) / (segment.endHz - segment.startHz);
        return segment.actualStartHz +
               std::clamp(ratio, 0.0, 1.0) * (segment.actualEndHz - segment.actualStartHz);
    }
    return displayFrequency;
}

double MyWaterfallWidget::displayFrequencyForActualFrequency(double actualFrequency) const {
    for (const ScanVisualSegment &segment : scanSegments) {
        if (!std::isfinite(segment.startHz) ||
            !std::isfinite(segment.endHz) ||
            !std::isfinite(segment.actualStartHz) ||
            !std::isfinite(segment.actualEndHz) ||
            segment.endHz <= segment.startHz ||
            segment.actualEndHz <= segment.actualStartHz ||
            actualFrequency < segment.actualStartHz ||
            actualFrequency > segment.actualEndHz) {
            continue;
        }
        const double ratio = (actualFrequency - segment.actualStartHz) /
                             (segment.actualEndHz - segment.actualStartHz);
        return segment.startHz + std::clamp(ratio, 0.0, 1.0) * (segment.endHz - segment.startHz);
    }
    return actualFrequency;
}

double MyWaterfallWidget::frequencyAtX(int x) const {
    return actualFrequencyForDisplayFrequency(displayFrequencyAtX(x));
}

double MyWaterfallWidget::signalCenterNearFrequency(double frequency) {
    QMutexLocker locker(&mutex);
    if (!std::isfinite(frequency) || qFuzzyCompare(xMin, xMax) || pixelFrequencyData.empty() || pixelLevelData.empty()) {
        return frequency;
    }

    const int dataCount = std::min(static_cast<int>(pixelFrequencyData.size()),
                                   static_cast<int>(pixelLevelData.size()));
    if (dataCount <= 0) {
        return frequency;
    }

    const double targetDisplayFrequency = displayFrequencyForActualFrequency(frequency);
    const double visibleSpan = std::abs(xMax - xMin);
    const double halfWindow = (std::clamp)(visibleSpan * AUTO_TUNE_WINDOW_FRACTION,
                                           AUTO_TUNE_MIN_WINDOW_HZ,
                                           AUTO_TUNE_MAX_WINDOW_HZ);
    std::vector<SignalSample> samples;
    samples.reserve(256);
    for (int i = 0; i < dataCount; ++i) {
        const double sampleFrequency = pixelFrequencyData[static_cast<std::size_t>(i)];
        if (!std::isfinite(sampleFrequency) ||
            std::abs(sampleFrequency - targetDisplayFrequency) > halfWindow) {
            continue;
        }
        const float level = pixelLevelData[static_cast<std::size_t>(i)];
        if (!std::isfinite(level)) {
            continue;
        }
        samples.push_back({sampleFrequency, level});
    }
    if (samples.empty()) {
        return frequency;
    }

    std::sort(samples.begin(), samples.end(), [](const SignalSample &a, const SignalSample &b) {
        return a.frequency < b.frequency;
    });

    const int sampleCount = static_cast<int>(samples.size());
    const int smoothRadius = (std::clamp)(sampleCount / 160, 1, 6);
    std::vector<float> smoothedLevels(static_cast<std::size_t>(sampleCount), -160.0f);
    std::vector<float> sortedSmoothedLevels;
    sortedSmoothedLevels.reserve(static_cast<std::size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i) {
        double sum = 0.0;
        int count = 0;
        for (int j = (std::max)(0, i - smoothRadius);
             j <= (std::min)(sampleCount - 1, i + smoothRadius);
             ++j) {
            sum += samples[j].level;
            ++count;
        }
        smoothedLevels[static_cast<std::size_t>(i)] =
            count > 0 ? static_cast<float>(sum / count) : samples[i].level;
        sortedSmoothedLevels.push_back(smoothedLevels[static_cast<std::size_t>(i)]);
    }
    std::sort(sortedSmoothedLevels.begin(), sortedSmoothedLevels.end());
    const float baseline = sortedSmoothedLevels[sortedSmoothedLevels.size() / 4];

    int peakIndex = 0;
    double bestScore = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < sampleCount; ++i) {
        const double distanceRatio = std::abs(samples[i].frequency - targetDisplayFrequency) /
                                     (std::max)(1.0, halfWindow);
        const double score = smoothedLevels[static_cast<std::size_t>(i)] - distanceRatio * 8.0;
        if (score > bestScore) {
            bestScore = score;
            peakIndex = i;
        }
    }
    const float peakLevel = smoothedLevels[static_cast<std::size_t>(peakIndex)];
    const double dynamicRange = (std::max)(0.0, static_cast<double>(peakLevel - baseline));
    if (dynamicRange < 2.0) {
        return samples[peakIndex].frequency;
    }

    const float threshold = (std::max)(baseline + 3.0f,
                                       peakLevel - static_cast<float>((std::clamp)(dynamicRange * 0.42, 4.0, 10.0)));
    int left = peakIndex;
    while (left > 0 && smoothedLevels[static_cast<std::size_t>(left - 1)] >= threshold) {
        --left;
    }
    int right = peakIndex;
    while (right + 1 < sampleCount && smoothedLevels[static_cast<std::size_t>(right + 1)] >= threshold) {
        ++right;
    }

    if (right <= left) {
        return samples[peakIndex].frequency;
    }

    auto interpolatedEdge = [&](int inside, int outside) {
        if (outside < 0 || outside >= sampleCount) {
            return samples[inside].frequency;
        }
        const double insideLevel = smoothedLevels[static_cast<std::size_t>(inside)];
        const double outsideLevel = smoothedLevels[static_cast<std::size_t>(outside)];
        const double denom = insideLevel - outsideLevel;
        if (std::abs(denom) < 0.0001) {
            return samples[inside].frequency;
        }
        const double ratio = (insideLevel - threshold) / denom;
        return samples[inside].frequency +
               (samples[outside].frequency - samples[inside].frequency) *
                   (std::clamp)(ratio, 0.0, 1.0);
    };

    const double leftFrequency = interpolatedEdge(left, left - 1);
    const double rightFrequency = interpolatedEdge(right, right + 1);
    const double centerFrequency = (leftFrequency + rightFrequency) * 0.5;
    const double actualCenterFrequency = actualFrequencyForDisplayFrequency(centerFrequency);
    return std::isfinite(actualCenterFrequency)
               ? actualCenterFrequency
               : actualFrequencyForDisplayFrequency(samples[peakIndex].frequency);
}

void MyWaterfallWidget::ensureLineBuffer() {
    const int lineWidth = std::max(1, width());
    const size_t requiredSize = static_cast<size_t>(lineWidth) * 3;
    if (lineData.size() != requiredSize) {
        lineData.assign(requiredSize, 0);
    }
    if (pixelMaxData.size() != static_cast<std::size_t>(lineWidth)) {
        pixelMaxData.resize(static_cast<std::size_t>(lineWidth));
    }
}

void MyWaterfallWidget::initializeGL() {
qDebug() << "MyWaterfallWidget::initializeGL start";
initializeOpenGLFunctions();


    glClearColor(0.0f, 0.0f, 0.0f, 1.0f); 
    ensureLineBuffer();
    glGenTextures(1, &waterfallTexture);
    resetWaterfallTexture(width(), height());
    waterfallVbo.create();
    waterfallVbo.bind();
    waterfallVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    waterfallVbo.allocate(static_cast<int>(16 * sizeof(GLfloat)));
    waterfallVbo.release();
	initialized = true;
qDebug() << "MyWaterfallWidget::initializeGL done";
}

void MyWaterfallWidget::resizeGL(int w, int h) {
        glViewport(0, 0, w, h);
        ensureLineBuffer();
        resizeWaterfallTexturePreserve(w, h);
        if (waterfallVbo.isCreated()) {
            waterfallVbo.bind();
            waterfallVbo.allocate(static_cast<int>(16 * sizeof(GLfloat)));
            waterfallVbo.release();
        }
        positionInfoOverlays();
        qDebug() << "resizeGL done";
}

void MyWaterfallWidget::resetWaterfallTexture(int w, int h) {
    textureWidth = std::max(1, w);
    textureHeight = std::max(1, h);
    waterfallWriteRow = 0;

    std::vector<unsigned char> blank(static_cast<size_t>(textureWidth) * textureHeight * 3, 0);
    glBindTexture(GL_TEXTURE_2D, waterfallTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, textureWidth, textureHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, blank.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void MyWaterfallWidget::resizeWaterfallTexturePreserve(int w, int h) {
    const int newWidth = std::max(1, w);
    const int newHeight = std::max(1, h);
    if (newWidth == textureWidth && newHeight == textureHeight) {
        return;
    }

    if (waterfallTexture == 0 || textureWidth <= 0 || textureHeight <= 0) {
        resetWaterfallTexture(newWidth, newHeight);
        return;
    }

    const int oldWidth = textureWidth;
    const int oldHeight = textureHeight;
    const int oldWriteRow = waterfallWriteRow;
    std::vector<unsigned char> oldPixels(static_cast<size_t>(oldWidth) * oldHeight * 3, 0);

    glBindTexture(GL_TEXTURE_2D, waterfallTexture);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    while (glGetError() != GL_NO_ERROR) {
    }
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, oldPixels.data());
    if (glGetError() != GL_NO_ERROR) {
        glBindTexture(GL_TEXTURE_2D, 0);
        resetWaterfallTexture(newWidth, newHeight);
        return;
    }

    std::vector<unsigned char> resized(static_cast<size_t>(newWidth) * newHeight * 3, 0);
    for (int y = 0; y < newHeight; ++y) {
        const int sourceVisualY = std::min(oldHeight - 1, static_cast<int>((static_cast<long long>(y) * oldHeight) / newHeight));
        const int sourceRow = (oldWriteRow + sourceVisualY) % oldHeight;
        for (int x = 0; x < newWidth; ++x) {
            const int sourceX = std::min(oldWidth - 1, static_cast<int>((static_cast<long long>(x) * oldWidth) / newWidth));
            const size_t sourceIndex = (static_cast<size_t>(sourceRow) * oldWidth + sourceX) * 3;
            const size_t destIndex = (static_cast<size_t>(y) * newWidth + x) * 3;
            resized[destIndex + 0] = oldPixels[sourceIndex + 0];
            resized[destIndex + 1] = oldPixels[sourceIndex + 1];
            resized[destIndex + 2] = oldPixels[sourceIndex + 2];
        }
    }

    textureWidth = newWidth;
    textureHeight = newHeight;
    waterfallWriteRow = 0;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, textureWidth, textureHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, resized.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void MyWaterfallWidget::setData(const std::vector<float> &sourceXData,
                                const std::vector<float> &sourceYData,
                                double xMin,
                                double xMax,
                                int fftLength,
                                bool secondGraph,
                                float contrast,
                                float sensitivity,
                                float levelMin,
                                 float levelMax,
                                 bool displayOrdered) {
    bool shouldScheduleUpdate = false;
    bool shouldRefreshSliceOverlay = false;
    {
        QMutexLocker locker(&mutex);
        this->xMin = xMin;
        this->xMax = xMax;
        this->fftLength = std::max(0, fftLength);
        this->secondGraph = secondGraph;
        this->contrast = contrast;
        this->sensitivity = sensitivity;
        if (std::isfinite(levelMin) && std::isfinite(levelMax) && levelMax > levelMin) {
            this->levelMin = levelMin;
            this->levelMax = levelMax;
            yMin = levelMin;
            yMax = levelMax;
        }

        ensureLineBuffer();

        const int lineWidth = std::max(1, width());
        const int dataCount = std::min({this->fftLength,
                                        static_cast<int>(sourceXData.size()),
                                        static_cast<int>(sourceYData.size())});
        const bool validFrame =
            !lineData.empty() &&
            dataCount > 0 &&
            !qFuzzyCompare(this->xMin, this->xMax);

        if (!validFrame) {
            pixelFrequencyData.clear();
            pixelLevelData.clear();
            pendingTextureLine = false;
        } else {
            if (pixelMaxData.size() != static_cast<std::size_t>(lineWidth)) {
                pixelMaxData.resize(static_cast<std::size_t>(lineWidth));
            }
            if (pixelFrequencyData.size() != static_cast<std::size_t>(lineWidth)) {
                pixelFrequencyData.resize(static_cast<std::size_t>(lineWidth));
            }
            if (pixelLevelData.size() != static_cast<std::size_t>(lineWidth)) {
                pixelLevelData.resize(static_cast<std::size_t>(lineWidth));
            }

            std::fill(pixelMaxData.begin(),
                      pixelMaxData.end(),
                      std::numeric_limits<float>::quiet_NaN());

            for (int id = 0; id < dataCount; ++id) {
                if (!displayOrdered && !std::isfinite(sourceXData[static_cast<std::size_t>(id)])) {
                    continue;
                }
                const int x1 = displayOrdered
                                   ? std::clamp(static_cast<int>(
                                                    static_cast<long long>(id) * lineWidth /
                                                    (std::max)(1, dataCount)),
                                                0,
                                                lineWidth - 1)
                                   : static_cast<int>((sourceXData[static_cast<std::size_t>(id)] - this->xMin) *
                                                      lineWidth /
                                                      (this->xMax - this->xMin));
                if (x1 < 0 || x1 >= lineWidth) {
                    continue;
                }
                const int shiftedIndex = displayOrdered ? id : (id + dataCount / 2) % dataCount;
                const float value = sourceYData[static_cast<std::size_t>(shiftedIndex)];
                if (std::isfinite(value)) {
                    float &pixelValue = pixelMaxData[static_cast<std::size_t>(x1)];
                    pixelValue = std::isfinite(pixelValue) ? (std::max)(pixelValue, value) : value;
                }
            }

            int previousFilled = -1;
            float previousValue = this->levelMin;
            for (int x = 0; x < lineWidth; ++x) {
                const float value = pixelMaxData[static_cast<std::size_t>(x)];
                if (!std::isfinite(value)) {
                    continue;
                }

                if (previousFilled < 0) {
                    for (int fill = 0; fill < x; ++fill) {
                        pixelMaxData[static_cast<std::size_t>(fill)] = value;
                    }
                } else if (x - previousFilled > 1) {
                    const int gap = x - previousFilled;
                    for (int fill = previousFilled + 1; fill < x; ++fill) {
                        const float t = static_cast<float>(fill - previousFilled) / static_cast<float>(gap);
                        pixelMaxData[static_cast<std::size_t>(fill)] =
                            previousValue + (value - previousValue) * t;
                    }
                }

                previousFilled = x;
                previousValue = value;
            }

            if (previousFilled < 0) {
                std::fill(pixelMaxData.begin(), pixelMaxData.end(), this->levelMin);
            } else {
                for (int fill = previousFilled + 1; fill < lineWidth; ++fill) {
                    pixelMaxData[static_cast<std::size_t>(fill)] = previousValue;
                }
            }

            const float contrastFactor = this->contrast / 10.0f;
            const float sensitivityFactor = this->sensitivity / 10.0f;
            const double span = this->xMax - this->xMin;
            for (int x = 0; x < lineWidth; ++x) {
                const std::size_t index = static_cast<std::size_t>(x);
                pixelFrequencyData[index] = static_cast<float>(
                    this->xMin + (static_cast<double>(x) + 0.5) * span / static_cast<double>(lineWidth));
                pixelLevelData[index] = pixelMaxData[index];
                writeWaterfallColor(normalizedLevel(pixelMaxData[index]),
                                    contrastFactor,
                                    sensitivityFactor,
                                    &lineData[index * 3]);
            }
            if (activeDisplayMode != DisplayMode::Waterfall2D) {
                waterfall3DRenderer->appendRow(pixelLevelData,
                                               lineData,
                                               this->levelMin,
                                               this->levelMax,
                                               rowsPerFrame);
            }
            pendingTextureLine = true;
        }

        if (!updateQueued) {
            updateQueued = true;
            shouldScheduleUpdate = true;
        }
        if ((frequencySliceMouseActive || spectrumFrameSliceMouseActive) &&
            (!sliceOverlayUpdateTimer.isValid() || sliceOverlayUpdateTimer.elapsed() >= 200)) {
            sliceOverlayUpdateTimer.restart();
            shouldRefreshSliceOverlay = true;
        }
    }

    if (shouldRefreshSliceOverlay) {
        updateSliceOverlay(sliceOverlayAnchor);
    }
    if (shouldScheduleUpdate) {
        QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
    }
}

void MyWaterfallWidget::setRowsPerFrame(int rows) {
    const int clampedRows = (std::clamp)(rows, 1, 8);
    QMutexLocker locker(&mutex);
    rowsPerFrame = clampedRows;
}

void MyWaterfallWidget::setSpectrumMetadata(double centerFrequencyHz,
                                            double listeningFrequencyHz,
                                            double sampleRateHz,
                                            int sourceFftLength,
                                            int fftWindowType) {
    QMutexLocker locker(&mutex);
    metadataCenterFrequencyHz = centerFrequencyHz;
    metadataListeningFrequencyHz = listeningFrequencyHz;
    metadataSampleRateHz = sampleRateHz;
    metadataFftLength = (std::max)(0, sourceFftLength);
    metadataFftWindowType = normalizedFftWindowType(fftWindowType);
}

void MyWaterfallWidget::setFpsOverlayEnabled(bool enabled) {
    if (fpsOverlayEnabled == enabled) {
        return;
    }
    fpsOverlayEnabled = enabled;
    fpsElapsedTimer.invalidate();
    fpsFrameCount = 0;
    displayedFps = 0.0;
    if (fpsOverlayLabel) {
        fpsOverlayLabel->setText(QStringLiteral("FPS --"));
        fpsOverlayLabel->adjustSize();
        positionInfoOverlays();
        fpsOverlayLabel->setVisible(enabled);
        fpsOverlayLabel->raise();
    }
    update();
}

void MyWaterfallWidget::setExtendedInfoOverlayEnabled(bool enabled) {
    if (extendedInfoOverlayEnabled == enabled) {
        return;
    }
    extendedInfoOverlayEnabled = enabled;
    if (!enabled && sliceDetailsOverlayLabel) {
        sliceDetailsOverlayLabel->hide();
    } else if (frequencySliceMouseActive || spectrumFrameSliceMouseActive) {
        updateSliceOverlay(sliceOverlayAnchor);
    }
    positionInfoOverlays();
    update();
}

void MyWaterfallWidget::setRenderBackend(RenderBackend backend) {
    bool changed = false;
    {
        QMutexLocker locker(&mutex);
        changed = activeRenderBackend != backend;
        activeRenderBackend = backend;
    }
    if (changed) {
        qDebug() << "[Waterfall] render backend"
                 << (backend == RenderBackend::GpuPrepared ? "gpu-prepared" : "cpu-texture");
        update();
    }
}

MyWaterfallWidget::RenderBackend MyWaterfallWidget::renderBackend() const {
    QMutexLocker locker(&mutex);
    return activeRenderBackend;
}

void MyWaterfallWidget::setDisplayMode(DisplayMode mode) {
    {
        QMutexLocker locker(&mutex);
        activeDisplayMode = mode;
        waterfall3DRenderer->endFrequencySlice();
        waterfall3DRenderer->endSpectrumSlice();
        frequencySliceMouseActive = false;
        spectrumFrameSliceMouseActive = false;
    }
    if (mode == DisplayMode::Waterfall2D) {
        hideSliceOverlay();
    }
    update();
}

MyWaterfallWidget::DisplayMode MyWaterfallWidget::displayMode() const {
    QMutexLocker locker(&mutex);
    return activeDisplayMode;
}

void MyWaterfallWidget::set3DResolutionDivisor(int divisor) {
    {
        QMutexLocker locker(&mutex);
        waterfall3DRenderer->setResolutionDivisor(divisor);
    }
    update();
}

void MyWaterfallWidget::set3DHistoryRows(int rows) {
    {
        QMutexLocker locker(&mutex);
        waterfall3DRenderer->setHistoryCapacity(rows);
    }
    update();
}

void MyWaterfallWidget::set3DSliceScrollStep(int points) {
    QMutexLocker locker(&mutex);
    waterfall3DRenderer->setSliceScrollStep(points);
}

void MyWaterfallWidget::set3DSliceWidth(int points) {
    QMutexLocker locker(&mutex);
    waterfall3DRenderer->setSliceWidth(points);
}

void MyWaterfallWidget::set3DSpectrumSliceScrollStep(int rows) {
    QMutexLocker locker(&mutex);
    waterfall3DRenderer->setSpectrumSliceScrollStep(rows);
}

void MyWaterfallWidget::set3DSpectrumSliceWidth(int rows) {
    QMutexLocker locker(&mutex);
    waterfall3DRenderer->setSpectrumSliceWidth(rows);
}

void MyWaterfallWidget::set3DSpectrumSliceCapture(bool enabled) {
    QMutexLocker locker(&mutex);
    waterfall3DRenderer->setSpectrumSliceCapture(enabled);
}

void MyWaterfallWidget::set3DSpectrumSliceCaptureFixed(bool enabled) {
    QMutexLocker locker(&mutex);
    waterfall3DRenderer->setSpectrumSliceCaptureFixed(enabled);
}

void MyWaterfallWidget::set3DModifierFreeSliceInput(bool enabled) {
    QMutexLocker locker(&mutex);
    modifierFreeSliceInput = enabled;
}

bool MyWaterfallWidget::ensureGpuWaterfallProgram() {
    if (waterfallProgramReady) {
        return true;
    }
    if (waterfallProgramTried) {
        return false;
    }
    waterfallProgramTried = true;

    static const char *vertexSource =
        "attribute vec2 position;\n"
        "attribute vec2 texcoord;\n"
        "varying vec2 vTexCoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(position, 0.0, 1.0);\n"
        "    vTexCoord = texcoord;\n"
        "}\n";
    static const char *fragmentSource =
        "uniform sampler2D waterfallTextureSampler;\n"
        "varying vec2 vTexCoord;\n"
        "void main() {\n"
        "    gl_FragColor = texture2D(waterfallTextureSampler, vTexCoord);\n"
        "}\n";

    if (!waterfallProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource) ||
        !waterfallProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource) ||
        !waterfallProgram.link()) {
        qDebug() << "[Waterfall] GPU program unavailable; CPU texture fallback"
                 << waterfallProgram.log();
        waterfallProgram.removeAllShaders();
        return false;
    }

    waterfallProgramReady = true;
    qDebug() << "[Waterfall] GPU program ready";
    return true;
}

bool MyWaterfallWidget::drawGpuPreparedWaterfall(float vStart) {
    if (!ensureGpuWaterfallProgram()) {
        return false;
    }

    const GLfloat vertices[] = {
        -1.0f, -1.0f,  0.0f, vStart + 1.0f,
         1.0f, -1.0f,  1.0f, vStart + 1.0f,
         1.0f,  1.0f,  1.0f, vStart,
        -1.0f,  1.0f,  0.0f, vStart
    };
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    if (!waterfallVbo.isCreated()) {
        waterfallVbo.create();
        waterfallVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    }
    waterfallVbo.bind();
    waterfallVbo.allocate(vertices, static_cast<int>(sizeof(vertices)));
    waterfallProgram.bind();
    waterfallProgram.setUniformValue("waterfallTextureSampler", 0);
    const int positionLocation = waterfallProgram.attributeLocation("position");
    const int texcoordLocation = waterfallProgram.attributeLocation("texcoord");
    if (positionLocation < 0 || texcoordLocation < 0) {
        waterfallProgram.release();
        waterfallVbo.release();
        return false;
    }
    waterfallProgram.enableAttributeArray(positionLocation);
    waterfallProgram.enableAttributeArray(texcoordLocation);
    waterfallProgram.setAttributeBuffer(positionLocation, GL_FLOAT, 0, 2, 4 * sizeof(GLfloat));
    waterfallProgram.setAttributeBuffer(texcoordLocation,
                                        GL_FLOAT,
                                        2 * static_cast<int>(sizeof(GLfloat)),
                                        2,
                                        4 * static_cast<int>(sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    waterfallProgram.disableAttributeArray(texcoordLocation);
    waterfallProgram.disableAttributeArray(positionLocation);
    waterfallProgram.release();
    waterfallVbo.release();
    return true;
}

void MyWaterfallWidget::drawMiniWaterfallOverlay(float vStart) {
    if (waterfallTexture == 0 || width() <= 0 || height() <= 0) {
        return;
    }

    const float margin = 12.0f;
    const float overlayWidth = std::clamp(width() * 0.34f, 220.0f, 520.0f);
    const float overlayHeight = std::clamp(height() * 0.30f, 90.0f, 260.0f);
    const float left = static_cast<float>(width()) - overlayWidth - margin;
    const float right = static_cast<float>(width()) - margin;
    const float bottom = margin;
    const float top = margin + overlayHeight;
    const float border = 3.0f;

    glViewport(0, 0, width(), height());
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, width(), 0.0, height(), -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDisable(GL_TEXTURE_2D);
    glColor4f(0.02f, 0.025f, 0.035f, 0.88f);
    glBegin(GL_QUADS);
    glVertex2f(left - border, bottom - border);
    glVertex2f(right + border, bottom - border);
    glVertex2f(right + border, top + border);
    glVertex2f(left - border, top + border);
    glEnd();

    glColor4f(0.72f, 0.76f, 0.82f, 0.85f);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(left - border, bottom - border);
    glVertex2f(right + border, bottom - border);
    glVertex2f(right + border, top + border);
    glVertex2f(left - border, top + border);
    glEnd();

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, waterfallTexture);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, vStart + 1.0f); glVertex2f(left, bottom);
    glTexCoord2f(1.0f, vStart + 1.0f); glVertex2f(right, bottom);
    glTexCoord2f(1.0f, vStart); glVertex2f(right, top);
    glTexCoord2f(0.0f, vStart); glVertex2f(left, top);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

void MyWaterfallWidget::setLevelRange(float minLevel, float maxLevel) {
    bool shouldScheduleUpdate = false;
    {
        QMutexLocker locker(&mutex);
        if (!std::isfinite(minLevel) || !std::isfinite(maxLevel) || maxLevel <= minLevel) {
            return;
        }
        levelMin = minLevel;
        levelMax = maxLevel;
        yMin = minLevel;
        yMax = maxLevel;
        if (!updateQueued) {
            updateQueued = true;
            shouldScheduleUpdate = true;
        }
    }
    if (shouldScheduleUpdate) {
        QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
    }
}

void MyWaterfallWidget::setScanSegments(const QVector<ScanVisualSegment> &segments) {
    {
        QMutexLocker locker(&mutex);
        scanSegments = segments;
    }
    update();
}

void MyWaterfallWidget::setScanSegmentMarkersVisible(bool visible) {
    {
        QMutexLocker locker(&mutex);
        if (scanSegmentMarkersVisible == visible) {
            return;
        }
        scanSegmentMarkersVisible = visible;
    }
    update();
}

void MyWaterfallWidget::clearData() {
    {
        QMutexLocker locker(&mutex);
        pixelFrequencyData.clear();
        pixelLevelData.clear();
        fftLength = 0;
        std::fill(lineData.begin(), lineData.end(), 0);
        pendingTextureLine = false;
        textureClearRequested = true;
        waterfall3DRenderer->clear();
    }
    update();
}

void MyWaterfallWidget::uploadPendingTextureLine() {
    ensureLineBuffer();
    const int texWidth = std::max(1, width());
    const int texHeight = std::max(1, height());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (texWidth != textureWidth || texHeight != textureHeight) {
        resizeWaterfallTexturePreserve(texWidth, texHeight);
    }
    if (!pendingTextureLine || lineData.empty()) {
        return;
    }

    glBindTexture(GL_TEXTURE_2D, waterfallTexture);
    const int rowsToWrite = (std::clamp)(rowsPerFrame, 1, (std::max)(1, textureHeight));
    if (rowsToWrite <= 1) {
        waterfallWriteRow = (waterfallWriteRow + textureHeight - 1) % textureHeight;
        glTexSubImage2D(GL_TEXTURE_2D,
                        0,
                        0,
                        waterfallWriteRow,
                        textureWidth,
                        1,
                        GL_RGB,
                        GL_UNSIGNED_BYTE,
                        lineData.data());
    } else {
        const std::size_t rowBytes = static_cast<std::size_t>(textureWidth) * 3U;
        const std::size_t uploadBytes = rowBytes * static_cast<std::size_t>(rowsToWrite);
        if (textureUploadRows.size() != uploadBytes) {
            textureUploadRows.resize(uploadBytes);
        }
        const std::size_t copyBytes = (std::min)(rowBytes, lineData.size());
        for (int row = 0; row < rowsToWrite; ++row) {
            auto rowBegin = textureUploadRows.begin() + static_cast<std::ptrdiff_t>(rowBytes * row);
            std::copy(lineData.begin(),
                      lineData.begin() + static_cast<std::ptrdiff_t>(copyBytes),
                      rowBegin);
            if (copyBytes < rowBytes) {
                std::fill(rowBegin + static_cast<std::ptrdiff_t>(copyBytes),
                          rowBegin + static_cast<std::ptrdiff_t>(rowBytes),
                          0);
            }
        }

        const int startRow = (waterfallWriteRow + textureHeight - rowsToWrite) % textureHeight;
        waterfallWriteRow = startRow;
        const int firstRunRows = (std::min)(rowsToWrite, textureHeight - startRow);
        glTexSubImage2D(GL_TEXTURE_2D,
                        0,
                        0,
                        startRow,
                        textureWidth,
                        firstRunRows,
                        GL_RGB,
                        GL_UNSIGNED_BYTE,
                        textureUploadRows.data());
        const int wrappedRows = rowsToWrite - firstRunRows;
        if (wrappedRows > 0) {
            glTexSubImage2D(GL_TEXTURE_2D,
                            0,
                            0,
                            0,
                            textureWidth,
                            wrappedRows,
                            GL_RGB,
                            GL_UNSIGNED_BYTE,
                            textureUploadRows.data() + rowBytes * static_cast<std::size_t>(firstRunRows));
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    pendingTextureLine = false;
}

void MyWaterfallWidget::computeLineData() {
    bool shouldScheduleUpdate = false;
	QMutexLocker locker(&mutex);
    ensureLineBuffer();
    if (lineData.empty() || pixelLevelData.empty()) {
        if (!updateQueued) {
            updateQueued = true;
            shouldScheduleUpdate = true;
        }
        locker.unlock();
        if (shouldScheduleUpdate) {
            QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
        }
        return;
    }

	float contrastFactor = contrast/10;
	float sensitivityFactor = sensitivity/10;
    const int lineWidth = std::max(1, width());
    const int dataCount = std::min(lineWidth, static_cast<int>(pixelLevelData.size()));
    if (dataCount < lineWidth) {
        std::fill(lineData.begin(), lineData.end(), 0);
    }
    for (int x = 0; x < dataCount; ++x) {
        writeWaterfallColor(normalizedLevel(pixelLevelData[static_cast<std::size_t>(x)]),
                            contrastFactor,
                            sensitivityFactor,
                            &lineData[static_cast<std::size_t>(x) * 3]);
    }
    pendingTextureLine = true;
    if (!updateQueued) {
        updateQueued = true;
        shouldScheduleUpdate = true;
    }
    locker.unlock();
    if (shouldScheduleUpdate) {
        QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
    }
}

void MyWaterfallWidget::paintGL() {
	updateFpsCounter();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    bool drawSegmentOverlay = true;
    std::vector<float> secondGraphLevels;
    {
        QMutexLocker locker(&mutex);
        updateQueued = false;
        if (width() <= 0 || height() <= 0) {
            return;
        }

        if (textureClearRequested) {
            ensureLineBuffer();
            std::fill(lineData.begin(), lineData.end(), 0);
            resetWaterfallTexture(width(), height());
            pendingTextureLine = false;
            textureClearRequested = false;
        }

        uploadPendingTextureLine();

        if (activeDisplayMode != DisplayMode::Waterfall2D) {
            drawSegmentOverlay = false;
            waterfall3DRenderer->render(width(), height());
            if (activeDisplayMode == DisplayMode::Waterfall3DWithMini) {
                const float vStart = static_cast<float>(waterfallWriteRow) /
                                     static_cast<float>((std::max)(1, textureHeight));
                drawMiniWaterfallOverlay(vStart);
            }
        } else if (secondGraph == true) {
	        // Draw the secondary spectrum with QPainter below. The old fixed-function
	        // OpenGL path was not portable to Raspberry Pi and inherited stale state
	        // after switching back from the 3D renderer.
            secondGraphLevels.reserve(pixelLevelData.size());
            for (const float level : pixelLevelData) {
                secondGraphLevels.push_back(normalizedLevel(level));
            }
	    } else {
        //waterfall

        glViewport(0, 0, width(), height());
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, width(), 0, height(), -1, 1);

	    glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, waterfallTexture);
        glColor3f(1.0f, 1.0f, 1.0f);

        const float vStart = static_cast<float>(waterfallWriteRow) / static_cast<float>(textureHeight);
        const bool useGpuPrepared = activeRenderBackend == RenderBackend::GpuPrepared;
        if (!useGpuPrepared || !drawGpuPreparedWaterfall(vStart)) {
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, vStart + 1.0f); glVertex2f(0, 0);
            glTexCoord2f(1.0f, vStart + 1.0f); glVertex2f(width(), 0);
            glTexCoord2f(1.0f, vStart); glVertex2f(width(), height());
            glTexCoord2f(0.0f, vStart); glVertex2f(0, height());
            glEnd();
        }
            glBindTexture(GL_TEXTURE_2D, 0);
	    }
    }
    if (!secondGraphLevels.empty() || drawSegmentOverlay) {
        QPainter painter(this);
        if (!secondGraphLevels.empty()) {
            painter.setRenderHint(QPainter::Antialiasing, false);
            const int pointCount = std::min(width(), static_cast<int>(secondGraphLevels.size()));
            const float graphHeight = static_cast<float>(height()) * 0.75f;
            for (int x = 1; x < pointCount; ++x) {
                const float previousLevel = secondGraphLevels[static_cast<std::size_t>(x - 1)];
                const float currentLevel = secondGraphLevels[static_cast<std::size_t>(x)];
                painter.setPen(valueToColors(currentLevel));
                painter.drawLine(x - 1,
                                 height() - 1 - qRound(previousLevel * graphHeight),
                                 x,
                                 height() - 1 - qRound(currentLevel * graphHeight));
            }
        }
        if (drawSegmentOverlay) {
            drawScanSegments(painter);
        }
    }
}

void MyWaterfallWidget::updateFpsCounter() {
    if (!fpsOverlayEnabled) {
        return;
    }
    if (!fpsElapsedTimer.isValid()) {
        fpsElapsedTimer.start();
        fpsFrameCount = 0;
    }
    ++fpsFrameCount;
    const qint64 elapsedMs = fpsElapsedTimer.elapsed();
    if (elapsedMs >= 500) {
        displayedFps = static_cast<double>(fpsFrameCount) * 1000.0 /
                       static_cast<double>(elapsedMs);
        fpsFrameCount = 0;
        fpsElapsedTimer.restart();
        if (fpsOverlayLabel) {
            fpsOverlayLabel->setText(QStringLiteral("FPS %1").arg(displayedFps, 0, 'f', 1));
            fpsOverlayLabel->adjustSize();
            positionInfoOverlays();
            fpsOverlayLabel->raise();
        }
    }
}

void MyWaterfallWidget::positionInfoOverlays() {
    int nextY = 8;
    if (fpsOverlayLabel) {
        fpsOverlayLabel->move((std::max)(8, width() - fpsOverlayLabel->width() - 8), nextY);
        if (fpsOverlayLabel->isVisible()) {
            nextY += fpsOverlayLabel->height() + 4;
        }
    }
    if (sliceDetailsOverlayLabel) {
        sliceDetailsOverlayLabel->move(
            (std::max)(8, width() - sliceDetailsOverlayLabel->width() - 8),
            nextY);
    }
}

void MyWaterfallWidget::updateSliceOverlay(const QPoint &anchor) {
    if (!sliceOverlayLabel) {
        return;
    }

    QString text;
    QString detailsText;
    {
        QMutexLocker locker(&mutex);
        if (frequencySliceMouseActive) {
            double centerRatio = 0.0;
            double firstRatio = 0.0;
            double lastRatio = 0.0;
            Waterfall3DRenderer::SliceStatistics statistics;
            if (waterfall3DRenderer->frequencySliceRange(centerRatio, firstRatio, lastRatio)) {
                const double span = xMax - xMin;
                const double centerHz = xMin + centerRatio * span;
                const double firstHz = xMin + firstRatio * span;
                const double lastHz = xMin + lastRatio * span;
                const bool hasStatistics =
                    waterfall3DRenderer->frequencySliceStatistics(statistics);
                const QString maximumText = hasStatistics
                                                ? QStringLiteral("\nmax  %1 dBFS")
                                                      .arg(statistics.maximumLevelDb, 0, 'f', 1)
                                                : QString();
                text = qFuzzyCompare(firstHz, lastHz)
                           ? QStringLiteral("f  %1 MHz%2")
                                 .arg(centerHz / 1.0e6, 0, 'f', 6)
                                 .arg(maximumText)
                           : QStringLiteral("f  %1 MHz\n%2 - %3 MHz%4")
                                 .arg(centerHz / 1.0e6, 0, 'f', 6)
                                 .arg(firstHz / 1.0e6, 0, 'f', 6)
                                 .arg(lastHz / 1.0e6, 0, 'f', 6)
                                 .arg(maximumText);
                if (extendedInfoOverlayEnabled && hasStatistics) {
                    const int selectedColumns = statistics.lastColumn -
                                                statistics.firstColumn + 1;
                    const double peakRatio = statistics.columnCount > 1
                                                 ? static_cast<double>(statistics.peakColumn) /
                                                       static_cast<double>(statistics.columnCount - 1)
                                                 : 0.0;
                    const double peakHz = xMin + peakRatio * span;
                    const double rawBinWidthHz = metadataFftLength > 0 && metadataSampleRateHz > 0.0
                                                     ? metadataSampleRateHz / metadataFftLength
                                                     : 0.0;
                    const double resolutionBandwidthHz =
                        rawBinWidthHz * fftWindowEnbwBins(metadataFftWindowType);
                    const double displayPointWidthHz = statistics.columnCount > 1
                                                           ? span / (statistics.columnCount - 1)
                                                           : span;
                    detailsText = QStringLiteral(
                                      "FREQUENCY SLICE\n"
                                      "f       %1 MHz\n"
                                      "range   %2 - %3 MHz\n"
                                      "width   %4 kHz / %5 pt\n"
                                      "level   %6 / %7 / %8 dBFS\n"
                                      "peak    %9 MHz\n"
                                      "center  %10 MHz\n"
                                      "listen  %11 MHz\n"
                                      "SR      %12 MHz\n"
                                      "FFT     %13\n"
                                      "window  %14\n"
                                      "RBW     %15 Hz\n"
                                      "bin     %16 Hz\n"
                                      "display %17 Hz / pt\n"
                                      "history %18 rows")
                                      .arg(centerHz / 1.0e6, 0, 'f', 6)
                                      .arg(firstHz / 1.0e6, 0, 'f', 6)
                                      .arg(lastHz / 1.0e6, 0, 'f', 6)
                                      .arg(std::abs(lastHz - firstHz) / 1.0e3, 0, 'f', 3)
                                      .arg(selectedColumns)
                                      .arg(statistics.minimumLevelDb, 0, 'f', 1)
                                      .arg(statistics.averageLevelDb, 0, 'f', 1)
                                      .arg(statistics.maximumLevelDb, 0, 'f', 1)
                                      .arg(peakHz / 1.0e6, 0, 'f', 6)
                                      .arg(metadataCenterFrequencyHz / 1.0e6, 0, 'f', 6)
                                      .arg(metadataListeningFrequencyHz / 1.0e6, 0, 'f', 6)
                                      .arg(metadataSampleRateHz / 1.0e6, 0, 'f', 3)
                                      .arg(metadataFftLength)
                                      .arg(QString::fromLatin1(fftWindowTypeName(metadataFftWindowType)))
                                      .arg(resolutionBandwidthHz, 0, 'f', 3)
                                      .arg(rawBinWidthHz, 0, 'f', 3)
                                      .arg(displayPointWidthHz, 0, 'f', 3)
                                      .arg(statistics.rowCount);
                }
            }
        } else if (spectrumFrameSliceMouseActive) {
            int firstRow = 0;
            int lastRow = 0;
            int rowCount = 0;
            Waterfall3DRenderer::SliceStatistics statistics;
            if (waterfall3DRenderer->spectrumSliceRange(firstRow, lastRow, rowCount)) {
                const bool hasStatistics =
                    waterfall3DRenderer->spectrumSliceStatistics(statistics);
                const QString maximumText = hasStatistics
                                                ? QStringLiteral("\nmax  %1 dBFS")
                                                      .arg(statistics.maximumLevelDb, 0, 'f', 1)
                                                : QString();
                text = QStringLiteral("rows  %1 - %2 / %3\n%4 - %5 MHz%6")
                           .arg(firstRow + 1)
                           .arg(lastRow + 1)
                           .arg(rowCount)
                           .arg(xMin / 1.0e6, 0, 'f', 6)
                           .arg(xMax / 1.0e6, 0, 'f', 6)
                           .arg(maximumText);
                if (extendedInfoOverlayEnabled && hasStatistics) {
                    const double peakRatio = statistics.columnCount > 1
                                                 ? static_cast<double>(statistics.peakColumn) /
                                                       static_cast<double>(statistics.columnCount - 1)
                                                 : 0.0;
                    const double peakHz = xMin + peakRatio * (xMax - xMin);
                    const double rawBinWidthHz = metadataFftLength > 0 && metadataSampleRateHz > 0.0
                                                     ? metadataSampleRateHz / metadataFftLength
                                                     : 0.0;
                    const double resolutionBandwidthHz =
                        rawBinWidthHz * fftWindowEnbwBins(metadataFftWindowType);
                    const double displayPointWidthHz = statistics.columnCount > 1
                                                           ? (xMax - xMin) / (statistics.columnCount - 1)
                                                           : (xMax - xMin);
                    detailsText = QStringLiteral(
                                      "SPECTRUM SLICE\n"
                                      "rows    %1 - %2 / %3\n"
                                      "count   %4 rows / %5 pt\n"
                                      "f       %6 - %7 MHz\n"
                                      "level   %8 / %9 / %10 dBFS\n"
                                      "peak    %11 MHz\n"
                                      "peak row %12\n"
                                      "center  %13 MHz\n"
                                      "listen  %14 MHz\n"
                                      "SR      %15 MHz\n"
                                      "FFT     %16\n"
                                      "window  %17\n"
                                      "RBW     %18 Hz\n"
                                      "bin     %19 Hz\n"
                                      "display %20 Hz / pt")
                                      .arg(statistics.firstRow + 1)
                                      .arg(statistics.lastRow + 1)
                                      .arg(statistics.rowCount)
                                      .arg(statistics.lastRow - statistics.firstRow + 1)
                                      .arg(statistics.columnCount)
                                      .arg(xMin / 1.0e6, 0, 'f', 6)
                                      .arg(xMax / 1.0e6, 0, 'f', 6)
                                      .arg(statistics.minimumLevelDb, 0, 'f', 1)
                                      .arg(statistics.averageLevelDb, 0, 'f', 1)
                                      .arg(statistics.maximumLevelDb, 0, 'f', 1)
                                      .arg(peakHz / 1.0e6, 0, 'f', 6)
                                      .arg(statistics.peakRow + 1)
                                      .arg(metadataCenterFrequencyHz / 1.0e6, 0, 'f', 6)
                                      .arg(metadataListeningFrequencyHz / 1.0e6, 0, 'f', 6)
                                      .arg(metadataSampleRateHz / 1.0e6, 0, 'f', 3)
                                      .arg(metadataFftLength)
                                      .arg(QString::fromLatin1(fftWindowTypeName(metadataFftWindowType)))
                                      .arg(resolutionBandwidthHz, 0, 'f', 3)
                                      .arg(rawBinWidthHz, 0, 'f', 3)
                                      .arg(displayPointWidthHz, 0, 'f', 3);
                }
            }
        }
    }

    if (text.isEmpty()) {
        hideSliceOverlay();
        return;
    }

    sliceOverlayAnchor = anchor;
    sliceOverlayLabel->setText(text);
    sliceOverlayLabel->adjustSize();
    int overlayX = anchor.x() + 14;
    int overlayY = anchor.y() + 14;
    if (overlayX + sliceOverlayLabel->width() > width() - 6) {
        overlayX = anchor.x() - sliceOverlayLabel->width() - 14;
    }
    if (overlayY + sliceOverlayLabel->height() > height() - 6) {
        overlayY = anchor.y() - sliceOverlayLabel->height() - 14;
    }
    sliceOverlayLabel->move((std::clamp)(overlayX, 6, (std::max)(6, width() - sliceOverlayLabel->width() - 6)),
                            (std::clamp)(overlayY, 6, (std::max)(6, height() - sliceOverlayLabel->height() - 6)));
    sliceOverlayLabel->show();
    sliceOverlayLabel->raise();
    if (sliceDetailsOverlayLabel) {
        if (extendedInfoOverlayEnabled && !detailsText.isEmpty()) {
            sliceDetailsOverlayLabel->setText(detailsText);
            sliceDetailsOverlayLabel->adjustSize();
            sliceDetailsOverlayLabel->show();
            sliceDetailsOverlayLabel->raise();
            positionInfoOverlays();
        } else {
            sliceDetailsOverlayLabel->hide();
        }
    }
}

void MyWaterfallWidget::hideSliceOverlay() {
    if (sliceOverlayLabel) {
        sliceOverlayLabel->hide();
    }
    if (sliceDetailsOverlayLabel) {
        sliceDetailsOverlayLabel->hide();
    }
}

void MyWaterfallWidget::drawScanSegments(QPainter &painter) const {
    QMutexLocker locker(&mutex);
    if (!scanSegmentMarkersVisible ||
        scanSegments.size() < 2 ||
        width() <= 0 ||
        height() <= 0 ||
        qFuzzyCompare(xMin, xMax)) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen edgePen(QColor(255, 255, 255, 42));
    edgePen.setWidth(1);
    painter.setPen(edgePen);
    for (const ScanVisualSegment &segment : scanSegments) {
        if (!std::isfinite(segment.startHz) || !std::isfinite(segment.endHz)) {
            continue;
        }
        const int left = static_cast<int>(std::round((segment.startHz - xMin) * width() / (xMax - xMin)));
        const int right = static_cast<int>(std::round((segment.endHz - xMin) * width() / (xMax - xMin)));
        if (right < 0 || left > width() || right <= left) {
            continue;
        }
        painter.drawLine(left, 0, left, height());
        painter.drawLine(right, 0, right, height());
    }
    painter.restore();
}

QColor MyWaterfallWidget::valueToColor(float value, float contrastFactor, float sensitivityFactor) {
    if (!std::isfinite(value)) {
        value = 0.0f;
    }
    value = qBound(0.0f, value * sensitivityFactor, 1.0f);
    QVector<QColor> colorMap = {
        QColor("#000020"),
        QColor("#000050"),
        QColor("#000090"),
        QColor("#0000F0"),
        QColor("#0000FF"),
        QColor("#50F030"),
        QColor("#1E90FF"),
        QColor("#FFFFFF"),
        QColor("#FFFF00"),
        QColor("#FE6D16"),
        QColor("#FE6D16"),
        QColor("#FF0000"),
        QColor("#FF0000"),
        QColor("#C60000"),
        QColor("#9F0000"),
        QColor("#750000"),
        QColor("#4A0000"),
        //QColor("#F000F0")
    };
    int index = static_cast<int>(value * (colorMap.size() - 1));
    index = std::clamp(index, 0, colorMap.size() - 1);
    QColor baseColor = colorMap[index];
    int blue = (baseColor.green() + baseColor.blue())/3;
    int r = static_cast<int>(baseColor.red() * contrastFactor + blue * (1.0f - contrastFactor));
    int g = static_cast<int>(baseColor.green() * contrastFactor + blue * (1.0f - contrastFactor));
    int b = static_cast<int>(baseColor.blue() * contrastFactor + blue * (1.0f - contrastFactor));
    return QColor(r, g, b);
}

float MyWaterfallWidget::normalizedLevel(float value) const {
    if (!std::isfinite(value) || qFuzzyCompare(levelMin, levelMax)) {
        return 0.0f;
    }
    return qBound(0.0f, (value - levelMin) / (levelMax - levelMin), 1.0f);
}


 QColor MyWaterfallWidget::valueToColors(float value) {
    if (!std::isfinite(value)) {
        value = 0.0f;
    }
    value = qBound(0.0f, value, 1.0f);
    int r, g, b;
    if (value < 0.12f) {
        float ratio = value / 0.2f;
        r = 0;
        g = 0;
        b = static_cast<int>(255 * ratio);
    } else if (value < 0.26f) {
        float ratio = (value - 0.12f) / 0.2f;
        r = 0;
        g = static_cast<int>(255 * (1 - ratio));
        b = 255;
    } else if (value < 0.40f) {
        float ratio = (value - 0.28f) / 0.2f;
        r = 0;
        g = 255;
        b = static_cast<int>(255 * (1 - ratio));
    } else if (value < 0.54f) {
        float ratio = (value - 0.40f) / 0.2f;
        r = static_cast<int>(255 * ratio);
        g = 255;
        b = 0;
    } else if (value < 0.68f) {
        float ratio = (value - 0.54f) / 0.2f;
        r = 255;
        g = static_cast<int>(255 * (1 - 0.5f * ratio));
        b = 0;    
    } else if (value < 0.86f) {
        float ratio = (value - 0.68f) / 0.2f;
        r = 255;
        g = static_cast<int>(128 * (1 - 0.5f * ratio));
        b = 0;
    } else {
        float ratio = (value - 0.86f) / 0.2f;
        r = 255;
        g = 0;
        b = static_cast<int>(255 * (1 - ratio));
    }
    return QColor(r, g, b);
}

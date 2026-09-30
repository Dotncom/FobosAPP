#ifndef MYWATERFALLWIDGET_H
#define MYWATERFALLWIDGET_H

#include <QObject>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <vector>
#include <QColor>
#include <QOpenGLTexture>
#include <QMutex>
#include <QWheelEvent>
#include <QPainter>
#include <QMouseEvent>
#include <QPoint>
#include <cmath>
#include <memory>
#include "scanvisualassembler.h"

class Waterfall3DRenderer;

class MyWaterfallWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    enum class RenderBackend {
        CpuTexture,
        GpuPrepared
    };

    enum class DisplayMode {
        Waterfall2D = 0,
        Waterfall3D = 1,
        Waterfall3DWithMini = 2
    };

    explicit MyWaterfallWidget(QWidget *parent = nullptr);
    ~MyWaterfallWidget();
    bool initialized;
    void setData(const std::vector<float> &xData, const std::vector<float> &yData, double minFrequency, double maxFrequency, int fftLength, bool secondGraph, float contrast, float sensitivity, float levelMin, float levelMax);
    void setRowsPerFrame(int rows);
    void setRenderBackend(RenderBackend backend);
    RenderBackend renderBackend() const;
    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const;
    void set3DResolutionDivisor(int divisor);
    void set3DHistoryRows(int rows);
    void set3DSliceScrollStep(int points);
    void set3DSliceWidth(int points);
    void set3DSpectrumSliceScrollStep(int rows);
    void set3DSpectrumSliceWidth(int rows);
    void set3DSpectrumSliceCapture(bool enabled);
    void set3DSpectrumSliceCaptureFixed(bool enabled);
    void set3DModifierFreeSliceInput(bool enabled);
    void setLevelRange(float minLevel, float maxLevel);
    void setScanSegments(const QVector<ScanVisualSegment> &segments);
    void setScanSegmentMarkersVisible(bool visible);
    void clearData();
    void computeLineData();
signals:
    void scaleChanged(int delta);
    void tuneContextRequested(double frequency, const QPoint &globalPos);
    void autoTuneRequested(double frequency);
    void panRequested(int deltaPixels, int widthPixels);
protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
	void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
private:
    void ensureLineBuffer();
    void resetWaterfallTexture(int w, int h);
    void resizeWaterfallTexturePreserve(int w, int h);
    bool ensureGpuWaterfallProgram();
    bool drawGpuPreparedWaterfall(float vStart);
    void drawMiniWaterfallOverlay(float vStart);
    void uploadPendingTextureLine();
    void drawScanSegments(QPainter &painter) const;
    double displayFrequencyAtX(int x) const;
    double actualFrequencyForDisplayFrequency(double displayFrequency) const;
    double displayFrequencyForActualFrequency(double actualFrequency) const;
    double frequencyAtX(int x) const;
    double signalCenterNearFrequency(double frequency);
    mutable QMutex mutex;
    QOpenGLBuffer waterfallVbo;
    QOpenGLShaderProgram waterfallProgram;
    GLuint waterfallTexture;
    std::vector<unsigned char> lineData;
    std::vector<unsigned char> textureUploadRows;
    std::vector<float> pixelMaxData;
    std::vector<float> pixelFrequencyData;
    std::vector<float> pixelLevelData;
    QColor valueToColor(float value, float contrastFactor, float sensitivityFactor);
    QColor valueToColors(float value);
    float normalizedLevel(float value) const;
    std::vector<float> xData;
    std::vector<float> yData;
    float yMin, yMax, contrast, sensitivity, levelMin, levelMax;
    double xMin, xMax;
    int fftLength;
    int textureWidth = 0;
    int textureHeight = 0;
    int waterfallWriteRow = 0;
    int rowsPerFrame = 2;
    RenderBackend activeRenderBackend = RenderBackend::CpuTexture;
    DisplayMode activeDisplayMode = DisplayMode::Waterfall2D;
    std::unique_ptr<Waterfall3DRenderer> waterfall3DRenderer;
    bool waterfallProgramReady = false;
    bool waterfallProgramTried = false;
    bool secondGraph;
    bool changebit;
    bool spectrumPanActive = false;
    bool spectrumPanMoved = false;
    Qt::MouseButton spectrumPanButton = Qt::NoButton;
    QPoint spectrumPanLastPos;
    bool cameraOrbitActive = false;
    QPoint cameraOrbitLastPos;
    bool cameraPanActive = false;
    QPoint cameraPanLastPos;
    bool frequencySliceMouseActive = false;
    bool spectrumFrameSliceMouseActive = false;
    bool modifierFreeSliceInput = false;
    bool pendingTextureLine = false;
    bool textureClearRequested = false;
    bool updateQueued = false;
    QVector<ScanVisualSegment> scanSegments;
    bool scanSegmentMarkersVisible = true;
};

#endif // MYWATERFALLWIDGET_H

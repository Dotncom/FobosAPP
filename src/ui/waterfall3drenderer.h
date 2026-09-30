#ifndef WATERFALL3DRENDERER_H
#define WATERFALL3DRENDERER_H

#include <array>
#include <cstdint>
#include <deque>
#include <vector>

class Waterfall3DRenderer {
public:
    void appendRow(const std::vector<float> &levels,
                   const std::vector<unsigned char> &rgb,
                   float levelMin,
                   float levelMax,
                   int repeatRows);
    void clear();
    bool hasData() const;
    void setResolutionDivisor(int divisor);
    void setHistoryCapacity(int rows);
    void setHighlightedHistoryRow(int row);
    void setSliceScrollStep(int points);
    void setSliceWidth(int points);
    void setSpectrumSliceScrollStep(int rows);
    void setSpectrumSliceWidth(int rows);
    void setSpectrumSliceCapture(bool enabled);
    void setSpectrumSliceCaptureFixed(bool enabled);
    bool beginFrequencySlice(int screenX, int screenY, int viewportWidth, int viewportHeight);
    bool beginSpectrumSlice(int screenX, int screenY, int viewportWidth, int viewportHeight);
    void stepFrequencySlice(int direction);
    void stepSpectrumSlice(int direction);
    void endFrequencySlice();
    void endSpectrumSlice();
    double selectedFrequencyRatio() const;
    void orbitCamera(float deltaX, float deltaY);
    void panCamera(float deltaX, float deltaY, int viewportWidth, int viewportHeight);
    void zoomCamera(int wheelDelta);
    void render(int viewportWidth, int viewportHeight) const;

private:
    struct VertexSample {
        float height = 0.0f;
        std::array<std::uint8_t, 3> color{{0, 0, 0}};
    };

    using HistoryRow = std::vector<VertexSample>;

    std::deque<HistoryRow> historyRows;
    int maxHistoryRows = 128;
    int highlightedHistoryRow = -1;
    int resolutionDivisor = 4;
    float cameraYawDegrees = -38.0f;
    float cameraTiltDegrees = -60.0f;
    float cameraDistance = 3.45f;
    float cameraZoom = 1.0f;
    float cameraPanX = 0.0f;
    float cameraPanY = 0.0f;
    int sliceScrollStep = 1;
    int sliceWidth = 1;
    int selectedSliceColumn = -1;
    bool frequencySliceActive = false;
    int spectrumSliceScrollStep = 1;
    int spectrumSliceWidth = 1;
    int selectedSpectrumRow = -1;
    bool spectrumSliceActive = false;
    bool spectrumSliceCapture = false;
    bool spectrumSliceCaptureFixed = false;
    std::vector<HistoryRow> capturedSpectrumRows;
    int capturedSpectrumFirstRow = -1;
    int capturedSpectrumRowsRemaining = 0;

    void refreshCapturedSpectrumRows();
};

#endif // WATERFALL3DRENDERER_H

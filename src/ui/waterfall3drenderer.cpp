#include "waterfall3drenderer.h"

#include <QtGui/qopengl.h>
#include <QMatrix4x4>
#include <QVector4D>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
float normalizedHeight(float level, float levelMin, float levelMax) {
    if (!std::isfinite(level) || !std::isfinite(levelMin) ||
        !std::isfinite(levelMax) || levelMax <= levelMin) {
        return 0.0f;
    }
    return std::clamp((level - levelMin) / (levelMax - levelMin), 0.0f, 1.0f);
}

}

void Waterfall3DRenderer::appendRow(const std::vector<float> &levels,
                                    const std::vector<unsigned char> &rgb,
                                    float levelMin,
                                    float levelMax,
                                    int repeatRows) {
    if (levels.empty()) {
        return;
    }

    const int sourceColumns = static_cast<int>(levels.size());
    const int outputColumns = (std::max)(2, sourceColumns / resolutionDivisor);
    if (!historyRows.empty() &&
        static_cast<int>(historyRows.back().size()) != outputColumns) {
        historyRows.clear();
        selectedSliceColumn = -1;
        frequencySliceActive = false;
        selectedSpectrumRow = -1;
        spectrumSliceActive = false;
    }
    HistoryRow row(static_cast<std::size_t>(outputColumns));

    for (int column = 0; column < outputColumns; ++column) {
        const int begin = column * sourceColumns / outputColumns;
        const int end = (std::max)(begin + 1, (column + 1) * sourceColumns / outputColumns);
        double levelSum = 0.0;
        int validLevels = 0;
        for (int source = begin; source < end && source < sourceColumns; ++source) {
            const float level = levels[static_cast<std::size_t>(source)];
            if (std::isfinite(level)) {
                levelSum += level;
                ++validLevels;
            }
        }

        const float averageLevel = validLevels > 0
                                       ? static_cast<float>(levelSum / static_cast<double>(validLevels))
                                       : levelMin;
        int representativeIndex = begin;
        float closestDistance = std::numeric_limits<float>::infinity();
        for (int source = begin; source < end && source < sourceColumns; ++source) {
            const float level = levels[static_cast<std::size_t>(source)];
            if (!std::isfinite(level)) {
                continue;
            }
            const float distance = std::abs(level - averageLevel);
            if (distance < closestDistance) {
                closestDistance = distance;
                representativeIndex = source;
            }
        }

        VertexSample &sample = row[static_cast<std::size_t>(column)];
        sample.height = normalizedHeight(averageLevel, levelMin, levelMax);
        const std::size_t colorOffset = static_cast<std::size_t>(representativeIndex) * 3U;
        if (colorOffset + 2U < rgb.size()) {
            sample.color = {{rgb[colorOffset], rgb[colorOffset + 1U], rgb[colorOffset + 2U]}};
        }
    }

    const int copies = (std::clamp)(repeatRows, 1, 8);
    for (int copy = 0; copy < copies; ++copy) {
        historyRows.push_back(row);
        while (static_cast<int>(historyRows.size()) > maxHistoryRows) {
            historyRows.pop_front();
            if (highlightedHistoryRow >= 0) {
                --highlightedHistoryRow;
            }
            if (spectrumSliceActive && spectrumSliceCapture && spectrumSliceCaptureFixed) {
                --capturedSpectrumRowsRemaining;
                if (capturedSpectrumRowsRemaining <= 0) {
                    capturedSpectrumRows.clear();
                    capturedSpectrumFirstRow = -1;
                    spectrumSliceActive = false;
                    selectedSpectrumRow = -1;
                }
            } else if (spectrumSliceActive && spectrumSliceCapture) {
                --selectedSpectrumRow;
            }
        }
    }
}

void Waterfall3DRenderer::clear() {
    historyRows.clear();
    selectedSliceColumn = -1;
    frequencySliceActive = false;
    highlightedHistoryRow = -1;
    selectedSpectrumRow = -1;
    spectrumSliceActive = false;
    capturedSpectrumRows.clear();
    capturedSpectrumFirstRow = -1;
    capturedSpectrumRowsRemaining = 0;
}

bool Waterfall3DRenderer::hasData() const {
    return historyRows.size() >= 2 && !historyRows.front().empty();
}

void Waterfall3DRenderer::setResolutionDivisor(int divisor) {
    const int normalizedDivisor = std::clamp(divisor, 1, 64);
    if (resolutionDivisor == normalizedDivisor) {
        return;
    }
    resolutionDivisor = normalizedDivisor;
    historyRows.clear();
    selectedSliceColumn = -1;
    frequencySliceActive = false;
    selectedSpectrumRow = -1;
    spectrumSliceActive = false;
    capturedSpectrumRows.clear();
    capturedSpectrumFirstRow = -1;
    capturedSpectrumRowsRemaining = 0;
}

void Waterfall3DRenderer::setHistoryCapacity(int rows) {
    const int normalizedRows = std::clamp(rows, 2, 2048);
    if (maxHistoryRows == normalizedRows) {
        return;
    }
    maxHistoryRows = normalizedRows;
    while (static_cast<int>(historyRows.size()) > maxHistoryRows) {
        historyRows.pop_front();
        if (spectrumSliceActive && spectrumSliceCapture && spectrumSliceCaptureFixed) {
            --capturedSpectrumRowsRemaining;
        } else if (spectrumSliceActive && spectrumSliceCapture) {
            --selectedSpectrumRow;
        }
    }
    if (spectrumSliceActive && spectrumSliceCapture && spectrumSliceCaptureFixed &&
        capturedSpectrumRowsRemaining <= 0) {
        capturedSpectrumRows.clear();
        capturedSpectrumFirstRow = -1;
        spectrumSliceActive = false;
        selectedSpectrumRow = -1;
    }
    if (spectrumSliceActive && !spectrumSliceCapture && !historyRows.empty()) {
        selectedSpectrumRow = std::clamp(selectedSpectrumRow,
                                         0,
                                         static_cast<int>(historyRows.size()) - 1);
    }
    highlightedHistoryRow = -1;
}

void Waterfall3DRenderer::setHighlightedHistoryRow(int row) {
    highlightedHistoryRow = row >= 0 && row < static_cast<int>(historyRows.size()) ? row : -1;
}

void Waterfall3DRenderer::setSliceScrollStep(int points) {
    sliceScrollStep = std::clamp(points, 1, 256);
}

void Waterfall3DRenderer::setSliceWidth(int points) {
    sliceWidth = std::clamp(points, 1, 4096);
}

void Waterfall3DRenderer::setSpectrumSliceScrollStep(int rows) {
    spectrumSliceScrollStep = std::clamp(rows, 1, 2048);
}

void Waterfall3DRenderer::setSpectrumSliceWidth(int rows) {
    spectrumSliceWidth = std::clamp(rows, 1, 2048);
}

void Waterfall3DRenderer::setSpectrumSliceCapture(bool enabled) {
    spectrumSliceCapture = enabled;
    if (enabled && spectrumSliceCaptureFixed && spectrumSliceActive) {
        refreshCapturedSpectrumRows();
    } else if (!enabled) {
        capturedSpectrumRows.clear();
        capturedSpectrumFirstRow = -1;
        capturedSpectrumRowsRemaining = 0;
    }
    if (!enabled && spectrumSliceActive && !historyRows.empty()) {
        selectedSpectrumRow = std::clamp(selectedSpectrumRow,
                                         0,
                                         static_cast<int>(historyRows.size()) - 1);
    }
}

void Waterfall3DRenderer::setSpectrumSliceCaptureFixed(bool enabled) {
    spectrumSliceCaptureFixed = enabled;
    capturedSpectrumRows.clear();
    capturedSpectrumFirstRow = -1;
    capturedSpectrumRowsRemaining = 0;
    if (enabled && spectrumSliceCapture && spectrumSliceActive) {
        refreshCapturedSpectrumRows();
    }
}

void Waterfall3DRenderer::refreshCapturedSpectrumRows() {
    capturedSpectrumRows.clear();
    capturedSpectrumFirstRow = -1;
    capturedSpectrumRowsRemaining = 0;
    if (!spectrumSliceCapture || !spectrumSliceCaptureFixed ||
        !spectrumSliceActive || historyRows.empty() ||
        selectedSpectrumRow < 0) {
        return;
    }

    const int rowCount = static_cast<int>(historyRows.size());
    const int halfWidth = spectrumSliceWidth / 2;
    const int firstRow = std::max(0, selectedSpectrumRow - halfWidth);
    const int lastRow = std::min(rowCount - 1, firstRow + spectrumSliceWidth - 1);
    capturedSpectrumRows.reserve(static_cast<std::size_t>(lastRow - firstRow + 1));
    for (int row = firstRow; row <= lastRow; ++row) {
        capturedSpectrumRows.push_back(historyRows[static_cast<std::size_t>(row)]);
    }
    capturedSpectrumFirstRow = firstRow;
    capturedSpectrumRowsRemaining = lastRow + 1;
}

bool Waterfall3DRenderer::beginFrequencySlice(int screenX,
                                             int screenY,
                                             int viewportWidth,
                                             int viewportHeight) {
    if (!hasData() || viewportWidth <= 0 || viewportHeight <= 0) {
        return false;
    }

    const int rowCount = static_cast<int>(historyRows.size());
    const int columnCount = static_cast<int>(historyRows.front().size());
    if (rowCount < 2 || columnCount < 1) {
        return false;
    }

    const float aspect = static_cast<float>(viewportWidth) /
                         static_cast<float>((std::max)(1, viewportHeight));
    const float projectionScale = 1.0f / cameraZoom;
    QMatrix4x4 projection;
    projection.frustum(-0.72f * aspect * projectionScale,
                       0.72f * aspect * projectionScale,
                       -0.72f * projectionScale,
                       0.72f * projectionScale,
                       1.0f,
                       12.0f);
    QMatrix4x4 model;
    model.translate(0.0f, -0.08f, -cameraDistance);
    model.rotate(cameraTiltDegrees, 1.0f, 0.0f, 0.0f);
    model.rotate(cameraYawDegrees, 0.0f, 0.0f, 1.0f);
    model.translate(cameraPanX, cameraPanY, 0.0f);
    model.scale(1.05f, 1.05f, 1.18f);
    const QMatrix4x4 transform = projection * model;

    float bestDistanceSquared = std::numeric_limits<float>::infinity();
    int bestColumn = -1;
    const int rowStep = std::max(1, rowCount / 512);
    const int columnStep = std::max(1, columnCount / 1024);
    for (int row = 0; row < rowCount; row += rowStep) {
        const HistoryRow &historyRow = historyRows[static_cast<std::size_t>(row)];
        if (static_cast<int>(historyRow.size()) != columnCount) {
            continue;
        }
        const float time = -1.0f + 2.0f * static_cast<float>(row) /
                                       static_cast<float>(rowCount - 1);
        for (int column = 0; column < columnCount; column += columnStep) {
            const float frequency = columnCount > 1
                                        ? -1.0f + 2.0f * static_cast<float>(column) /
                                                      static_cast<float>(columnCount - 1)
                                        : 0.0f;
            const VertexSample &sample = historyRow[static_cast<std::size_t>(column)];
            const QVector4D clip = transform * QVector4D(time,
                                                        frequency,
                                                        sample.height * 0.72f,
                                                        1.0f);
            if (clip.w() <= 0.0f) {
                continue;
            }
            const float inverseW = 1.0f / clip.w();
            const float projectedX = (clip.x() * inverseW + 1.0f) * 0.5f * viewportWidth;
            const float projectedY = (1.0f - clip.y() * inverseW) * 0.5f * viewportHeight;
            const float dx = projectedX - static_cast<float>(screenX);
            const float dy = projectedY - static_cast<float>(screenY);
            const float distanceSquared = dx * dx + dy * dy;
            if (distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                bestColumn = column;
            }
        }
    }

    constexpr float MaxPickDistancePixels = 36.0f;
    if (bestColumn < 0 ||
        bestDistanceSquared > MaxPickDistancePixels * MaxPickDistancePixels) {
        return false;
    }
    selectedSliceColumn = bestColumn;
    frequencySliceActive = true;
    spectrumSliceActive = false;
    selectedSpectrumRow = -1;
    return true;
}

bool Waterfall3DRenderer::beginSpectrumSlice(int screenX,
                                            int screenY,
                                            int viewportWidth,
                                            int viewportHeight) {
    if (!hasData() || viewportWidth <= 0 || viewportHeight <= 0) {
        return false;
    }

    const int rowCount = static_cast<int>(historyRows.size());
    const int columnCount = static_cast<int>(historyRows.front().size());
    if (rowCount < 2 || columnCount < 1) {
        return false;
    }

    const float aspect = static_cast<float>(viewportWidth) /
                         static_cast<float>(std::max(1, viewportHeight));
    const float projectionScale = 1.0f / cameraZoom;
    QMatrix4x4 projection;
    projection.frustum(-0.72f * aspect * projectionScale,
                       0.72f * aspect * projectionScale,
                       -0.72f * projectionScale,
                       0.72f * projectionScale,
                       1.0f,
                       12.0f);
    QMatrix4x4 model;
    model.translate(0.0f, -0.08f, -cameraDistance);
    model.rotate(cameraTiltDegrees, 1.0f, 0.0f, 0.0f);
    model.rotate(cameraYawDegrees, 0.0f, 0.0f, 1.0f);
    model.translate(cameraPanX, cameraPanY, 0.0f);
    model.scale(1.05f, 1.05f, 1.18f);
    const QMatrix4x4 transform = projection * model;

    float bestDistanceSquared = std::numeric_limits<float>::infinity();
    int bestRow = -1;
    const int rowStep = std::max(1, rowCount / 512);
    const int columnStep = std::max(1, columnCount / 1024);
    for (int row = 0; row < rowCount; row += rowStep) {
        const HistoryRow &historyRow = historyRows[static_cast<std::size_t>(row)];
        if (static_cast<int>(historyRow.size()) != columnCount) {
            continue;
        }
        const float time = -1.0f + 2.0f * static_cast<float>(row) /
                                       static_cast<float>(rowCount - 1);
        for (int column = 0; column < columnCount; column += columnStep) {
            const float frequency = columnCount > 1
                                        ? -1.0f + 2.0f * static_cast<float>(column) /
                                                      static_cast<float>(columnCount - 1)
                                        : 0.0f;
            const VertexSample &sample = historyRow[static_cast<std::size_t>(column)];
            const QVector4D clip = transform * QVector4D(time,
                                                        frequency,
                                                        sample.height * 0.72f,
                                                        1.0f);
            if (clip.w() <= 0.0f) {
                continue;
            }
            const float inverseW = 1.0f / clip.w();
            const float projectedX = (clip.x() * inverseW + 1.0f) * 0.5f * viewportWidth;
            const float projectedY = (1.0f - clip.y() * inverseW) * 0.5f * viewportHeight;
            const float dx = projectedX - static_cast<float>(screenX);
            const float dy = projectedY - static_cast<float>(screenY);
            const float distanceSquared = dx * dx + dy * dy;
            if (distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                bestRow = row;
            }
        }
    }

    constexpr float MaxPickDistancePixels = 36.0f;
    if (bestRow < 0 ||
        bestDistanceSquared > MaxPickDistancePixels * MaxPickDistancePixels) {
        return false;
    }
    selectedSpectrumRow = bestRow;
    spectrumSliceActive = true;
    frequencySliceActive = false;
    selectedSliceColumn = -1;
    if (spectrumSliceCapture && spectrumSliceCaptureFixed) {
        refreshCapturedSpectrumRows();
    }
    return true;
}

void Waterfall3DRenderer::stepFrequencySlice(int direction) {
    if (!frequencySliceActive || selectedSliceColumn < 0 || historyRows.empty()) {
        return;
    }
    const int columnCount = static_cast<int>(historyRows.front().size());
    selectedSliceColumn = std::clamp(selectedSliceColumn + direction * sliceScrollStep,
                                     0,
                                     (std::max)(0, columnCount - 1));
}

void Waterfall3DRenderer::stepSpectrumSlice(int direction) {
    if (!spectrumSliceActive || historyRows.empty()) {
        return;
    }
    const int rowCount = static_cast<int>(historyRows.size());
    selectedSpectrumRow = std::clamp(selectedSpectrumRow + direction * spectrumSliceScrollStep,
                                     0,
                                     std::max(0, rowCount - 1));
    if (spectrumSliceCapture && spectrumSliceCaptureFixed) {
        refreshCapturedSpectrumRows();
    }
}

void Waterfall3DRenderer::endFrequencySlice() {
    frequencySliceActive = false;
    selectedSliceColumn = -1;
}

void Waterfall3DRenderer::endSpectrumSlice() {
    spectrumSliceActive = false;
    selectedSpectrumRow = -1;
    capturedSpectrumRows.clear();
    capturedSpectrumFirstRow = -1;
    capturedSpectrumRowsRemaining = 0;
}

double Waterfall3DRenderer::selectedFrequencyRatio() const {
    if (selectedSliceColumn < 0 || historyRows.empty() || historyRows.front().size() < 2) {
        return -1.0;
    }
    return static_cast<double>(selectedSliceColumn) /
           static_cast<double>(historyRows.front().size() - 1);
}

void Waterfall3DRenderer::orbitCamera(float deltaX, float deltaY) {
    cameraYawDegrees = std::fmod(cameraYawDegrees + deltaX * 0.4f, 360.0f);
    cameraTiltDegrees = std::clamp(cameraTiltDegrees + deltaY * 0.35f, -82.0f, -5.0f);
}

void Waterfall3DRenderer::panCamera(float deltaX,
                                    float deltaY,
                                    int viewportWidth,
                                    int viewportHeight) {
    if (viewportWidth <= 0 || viewportHeight <= 0 ||
        (!std::isfinite(deltaX) || !std::isfinite(deltaY))) {
        return;
    }

    constexpr float Pi = 3.14159265358979323846f;
    const float inverseZoom = 1.0f / std::max(0.35f, cameraZoom);
    const float screenX = 2.25f * deltaX / static_cast<float>(viewportWidth) * inverseZoom;
    const float tiltRadians = cameraTiltDegrees * Pi / 180.0f;
    const float verticalProjection = std::max(0.22f, std::abs(std::cos(tiltRadians)));
    const float screenY = -2.25f * deltaY / static_cast<float>(viewportHeight) *
                          inverseZoom / verticalProjection;
    const float yawRadians = cameraYawDegrees * Pi / 180.0f;
    const float cosine = std::cos(yawRadians);
    const float sine = std::sin(yawRadians);

    cameraPanX = std::clamp(cameraPanX + cosine * screenX + sine * screenY,
                            -4.0f,
                            4.0f);
    cameraPanY = std::clamp(cameraPanY - sine * screenX + cosine * screenY,
                            -4.0f,
                            4.0f);
}

void Waterfall3DRenderer::zoomCamera(int wheelDelta) {
    if (wheelDelta == 0) {
        return;
    }
    const float wheelSteps = static_cast<float>(wheelDelta) / 120.0f;
    cameraZoom *= std::pow(1.18f, wheelSteps);
    cameraZoom = std::clamp(cameraZoom, 0.35f, 8.0f);
}

void Waterfall3DRenderer::render(int viewportWidth, int viewportHeight) const {
    if (viewportWidth <= 0 || viewportHeight <= 0) {
        return;
    }

    glViewport(0, 0, viewportWidth, viewportHeight);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glShadeModel(GL_SMOOTH);

    const double aspect = static_cast<double>(viewportWidth) /
                          static_cast<double>((std::max)(1, viewportHeight));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double projectionScale = 1.0 / static_cast<double>(cameraZoom);
    glFrustum(-0.72 * aspect * projectionScale,
              0.72 * aspect * projectionScale,
              -0.72 * projectionScale,
              0.72 * projectionScale,
              1.0,
              12.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, -0.08f, -cameraDistance);
    glRotatef(cameraTiltDegrees, 1.0f, 0.0f, 0.0f);
    glRotatef(cameraYawDegrees, 0.0f, 0.0f, 1.0f);
    glTranslatef(cameraPanX, cameraPanY, 0.0f);
    glScalef(1.05f, 1.05f, 1.18f);

    glColor3f(0.16f, 0.18f, 0.21f);
    glBegin(GL_LINES);
    for (int line = 0; line <= 8; ++line) {
        const float coordinate = -1.0f + 0.25f * static_cast<float>(line);
        glVertex3f(coordinate, -1.0f, 0.0f);
        glVertex3f(coordinate, 1.0f, 0.0f);
        glVertex3f(-1.0f, coordinate, 0.0f);
        glVertex3f(1.0f, coordinate, 0.0f);
    }
    glEnd();

    if (hasData()) {
        const int rowCount = static_cast<int>(historyRows.size());
        const int columnCount = static_cast<int>(historyRows.front().size());
        int firstColumn = 0;
        int lastColumn = columnCount - 1;
        int firstRow = 0;
        int lastRow = rowCount - 1;
        const bool renderCapturedSpectrum =
            spectrumSliceActive && spectrumSliceCapture && spectrumSliceCaptureFixed &&
            !capturedSpectrumRows.empty();
        if (frequencySliceActive && selectedSliceColumn >= 0) {
            const int halfWidth = sliceWidth / 2;
            firstColumn = (std::max)(0, selectedSliceColumn - halfWidth);
            lastColumn = (std::min)(columnCount - 1,
                                    firstColumn + sliceWidth - 1);
            firstColumn = (std::max)(0, lastColumn - sliceWidth + 1);
        }
        bool spectrumSliceVisible = true;
        if (renderCapturedSpectrum) {
            firstRow = capturedSpectrumFirstRow;
            lastRow = firstRow + static_cast<int>(capturedSpectrumRows.size()) - 1;
            spectrumSliceVisible = firstRow >= 0 && lastRow < rowCount;
        } else if (spectrumSliceActive) {
            const int halfWidth = spectrumSliceWidth / 2;
            const int requestedFirst = selectedSpectrumRow - halfWidth;
            const int requestedLast = requestedFirst + spectrumSliceWidth - 1;
            firstRow = std::max(0, requestedFirst);
            lastRow = std::min(rowCount - 1, requestedLast);
            spectrumSliceVisible = firstRow <= lastRow &&
                                   requestedLast >= 0 &&
                                   requestedFirst < rowCount;
        }

        const auto &rowAt = [&](int row) -> const HistoryRow & {
            if (renderCapturedSpectrum) {
                return capturedSpectrumRows[static_cast<std::size_t>(row - firstRow)];
            }
            return historyRows[static_cast<std::size_t>(row)];
        };

        if (!spectrumSliceVisible) {
            // A captured spectrum row has left the rolling 3D history.
        } else if (frequencySliceActive && firstColumn == lastColumn) {
            glLineWidth(3.0f);
            glBegin(GL_LINE_STRIP);
            for (int row = firstRow; row <= lastRow; ++row) {
                const HistoryRow &historyRow = rowAt(row);
                if (static_cast<int>(historyRow.size()) != columnCount) {
                    continue;
                }
                const float time = -1.0f + 2.0f * static_cast<float>(row) /
                                               static_cast<float>(rowCount - 1);
                const float frequency = columnCount > 1
                                            ? -1.0f + 2.0f * static_cast<float>(firstColumn) /
                                                          static_cast<float>(columnCount - 1)
                                            : 0.0f;
                const VertexSample &sample = historyRow[static_cast<std::size_t>(firstColumn)];
                glColor3ub(sample.color[0], sample.color[1], sample.color[2]);
                glVertex3f(time, frequency, sample.height * 0.72f);
            }
            glEnd();
            glLineWidth(1.0f);
        } else if (spectrumSliceActive && firstRow == lastRow) {
            const HistoryRow &selected = rowAt(firstRow);
            if (static_cast<int>(selected.size()) == columnCount) {
                const float time = -1.0f + 2.0f * static_cast<float>(firstRow) /
                                               static_cast<float>(rowCount - 1);
                glLineWidth(3.0f);
                glBegin(GL_LINE_STRIP);
                for (int column = firstColumn; column <= lastColumn; ++column) {
                    const float frequency = columnCount > 1
                                                ? -1.0f + 2.0f * static_cast<float>(column) /
                                                              static_cast<float>(columnCount - 1)
                                                : 0.0f;
                    const VertexSample &sample = selected[static_cast<std::size_t>(column)];
                    glColor3ub(sample.color[0], sample.color[1], sample.color[2]);
                    glVertex3f(time, frequency, sample.height * 0.72f);
                }
                glEnd();
                glLineWidth(1.0f);
            }
        } else for (int row = firstRow; row < lastRow; ++row) {
            const HistoryRow &first = rowAt(row);
            const HistoryRow &second = rowAt(row + 1);
            if (static_cast<int>(first.size()) != columnCount ||
                static_cast<int>(second.size()) != columnCount) {
                continue;
            }

            const float time0 = -1.0f + 2.0f * static_cast<float>(row) /
                                            static_cast<float>(rowCount - 1);
            const float time1 = -1.0f + 2.0f * static_cast<float>(row + 1) /
                                            static_cast<float>(rowCount - 1);
            glBegin(GL_TRIANGLE_STRIP);
            for (int column = firstColumn; column <= lastColumn; ++column) {
                const float frequency = columnCount > 1
                                            ? -1.0f + 2.0f * static_cast<float>(column) /
                                                          static_cast<float>(columnCount - 1)
                                            : 0.0f;
                const VertexSample &sample0 = first[static_cast<std::size_t>(column)];
                const VertexSample &sample1 = second[static_cast<std::size_t>(column)];
                glColor3ub(sample0.color[0], sample0.color[1], sample0.color[2]);
                glVertex3f(time0, frequency, sample0.height * 0.72f);
                glColor3ub(sample1.color[0], sample1.color[1], sample1.color[2]);
                glVertex3f(time1, frequency, sample1.height * 0.72f);
            }
            glEnd();
        }

        if (highlightedHistoryRow >= 0 && highlightedHistoryRow < rowCount) {
            const HistoryRow &highlighted = historyRows[static_cast<std::size_t>(highlightedHistoryRow)];
            if (static_cast<int>(highlighted.size()) == columnCount) {
                const float time = -1.0f + 2.0f * static_cast<float>(highlightedHistoryRow) /
                                               static_cast<float>(rowCount - 1);
                glLineWidth(2.0f);
                glColor3f(1.0f, 0.25f, 0.18f);
                glBegin(GL_LINE_STRIP);
                for (int column = 0; column < columnCount; ++column) {
                    const float frequency = columnCount > 1
                                                ? -1.0f + 2.0f * static_cast<float>(column) /
                                                              static_cast<float>(columnCount - 1)
                                                : 0.0f;
                    const VertexSample &sample = highlighted[static_cast<std::size_t>(column)];
                    glVertex3f(time, frequency, sample.height * 0.72f + 0.012f);
                }
                glEnd();
                glLineWidth(1.0f);
            }
        }
    }

    glDisable(GL_DEPTH_TEST);
}

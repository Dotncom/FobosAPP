#include "receivercalibrationtable.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr double kMinimumFrequencySpacingHz = 0.001;

ReceiverCalibrationCorrection interpolate(const ReceiverCalibrationPoint &lower,
                                          const ReceiverCalibrationPoint &upper,
                                          double frequencyHz) {
    ReceiverCalibrationCorrection result;
    result.valid = true;
    const double spanHz = upper.frequencyHz - lower.frequencyHz;
    const double ratio = spanHz > kMinimumFrequencySpacingHz
                             ? std::clamp((frequencyHz - lower.frequencyHz) / spanHz, 0.0, 1.0)
                             : 0.0;
    result.frequencyOffsetHz = lower.frequencyOffsetHz +
                               (upper.frequencyOffsetHz - lower.frequencyOffsetHz) * ratio;
    result.amplitudeOffsetDb = lower.amplitudeOffsetDb +
                               (upper.amplitudeOffsetDb - lower.amplitudeOffsetDb) * ratio;
    result.uncertaintyDb = lower.uncertaintyDb +
                           (upper.uncertaintyDb - lower.uncertaintyDb) * ratio;
    return result;
}

} // namespace

void ReceiverCalibrationTable::setPoints(const QVector<ReceiverCalibrationPoint> &points) {
    calibrationPoints.clear();
    calibrationPoints.reserve(points.size());
    for (const ReceiverCalibrationPoint &point : points) {
        if (!std::isfinite(point.frequencyHz) || point.frequencyHz < 0.0 ||
            !std::isfinite(point.frequencyOffsetHz) ||
            !std::isfinite(point.amplitudeOffsetDb) ||
            !std::isfinite(point.uncertaintyDb) || point.uncertaintyDb < 0.0) {
            continue;
        }
        ReceiverCalibrationPoint clean = point;
        clean.note = clean.note.trimmed().left(256);
        calibrationPoints.append(clean);
    }
    std::sort(calibrationPoints.begin(), calibrationPoints.end(),
              [](const ReceiverCalibrationPoint &a, const ReceiverCalibrationPoint &b) {
                  return a.frequencyHz < b.frequencyHz;
              });

    QVector<ReceiverCalibrationPoint> unique;
    unique.reserve(calibrationPoints.size());
    for (const ReceiverCalibrationPoint &point : std::as_const(calibrationPoints)) {
        if (!unique.isEmpty() &&
            std::abs(unique.last().frequencyHz - point.frequencyHz) < kMinimumFrequencySpacingHz) {
            unique.last() = point;
        } else {
            unique.append(point);
        }
    }
    calibrationPoints = std::move(unique);
}

ReceiverCalibrationCorrection ReceiverCalibrationTable::correctionAt(double frequencyHz) const {
    ReceiverCalibrationCorrection result;
    if (calibrationPoints.isEmpty() || !std::isfinite(frequencyHz)) {
        return result;
    }
    result.valid = true;
    if (calibrationPoints.size() == 1 || frequencyHz <= calibrationPoints.first().frequencyHz) {
        result.frequencyOffsetHz = calibrationPoints.first().frequencyOffsetHz;
        result.amplitudeOffsetDb = calibrationPoints.first().amplitudeOffsetDb;
        result.uncertaintyDb = calibrationPoints.first().uncertaintyDb;
        return result;
    }
    if (frequencyHz >= calibrationPoints.last().frequencyHz) {
        result.frequencyOffsetHz = calibrationPoints.last().frequencyOffsetHz;
        result.amplitudeOffsetDb = calibrationPoints.last().amplitudeOffsetDb;
        result.uncertaintyDb = calibrationPoints.last().uncertaintyDb;
        return result;
    }

    const auto upper = std::lower_bound(
        calibrationPoints.cbegin(), calibrationPoints.cend(), frequencyHz,
        [](const ReceiverCalibrationPoint &point, double value) {
            return point.frequencyHz < value;
        });
    const int upperIndex = static_cast<int>(std::distance(calibrationPoints.cbegin(), upper));
    return interpolate(calibrationPoints.at(upperIndex - 1),
                       calibrationPoints.at(upperIndex),
                       frequencyHz);
}

double ReceiverCalibrationTable::interpolatedAmplitudeOffset(double frequencyHz,
                                                              int &upperIndex) const {
    if (calibrationPoints.isEmpty() || !std::isfinite(frequencyHz)) {
        return 0.0;
    }
    if (calibrationPoints.size() == 1 || frequencyHz <= calibrationPoints.first().frequencyHz) {
        upperIndex = calibrationPoints.size() > 1 ? 1 : 0;
        return calibrationPoints.first().amplitudeOffsetDb;
    }
    while (upperIndex < calibrationPoints.size() &&
           frequencyHz > calibrationPoints.at(upperIndex).frequencyHz) {
        ++upperIndex;
    }
    if (upperIndex >= calibrationPoints.size()) {
        return calibrationPoints.last().amplitudeOffsetDb;
    }
    return interpolate(calibrationPoints.at(upperIndex - 1),
                       calibrationPoints.at(upperIndex),
                       frequencyHz).amplitudeOffsetDb;
}

void ReceiverCalibrationTable::applyAmplitudeCorrection(const std::vector<float> &frequencies,
                                                         std::vector<float> &primary,
                                                         std::vector<float> *secondary,
                                                         double baseOffsetDb) const {
    const std::size_t count = (std::min)(frequencies.size(), primary.size());
    if (count == 0) {
        return;
    }
    const bool haveCurve = !calibrationPoints.isEmpty();
    if (!haveCurve && (!std::isfinite(baseOffsetDb) || std::abs(baseOffsetDb) <= 0.000001)) {
        return;
    }

    int upperIndex = calibrationPoints.size() > 1 ? 1 : 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0 && frequencies[i] < frequencies[i - 1]) {
            upperIndex = calibrationPoints.size() > 1 ? 1 : 0;
        }
        const double curveDb = haveCurve
                                   ? interpolatedAmplitudeOffset(frequencies[i], upperIndex)
                                   : 0.0;
        const float correctionDb = static_cast<float>(
            (std::isfinite(baseOffsetDb) ? baseOffsetDb : 0.0) + curveDb);
        if (std::isfinite(primary[i])) {
            primary[i] += correctionDb;
        }
        if (secondary && i < secondary->size() && std::isfinite((*secondary)[i])) {
            (*secondary)[i] += correctionDb;
        }
    }
}

#ifndef RECEIVERCALIBRATIONTABLE_H
#define RECEIVERCALIBRATIONTABLE_H

#include <QString>
#include <QVector>

#include <vector>

struct ReceiverCalibrationPoint {
    double frequencyHz = 0.0;
    double frequencyOffsetHz = 0.0;
    double amplitudeOffsetDb = 0.0;
    double uncertaintyDb = 0.0;
    QString note;
};

struct ReceiverCalibrationCorrection {
    bool valid = false;
    double frequencyOffsetHz = 0.0;
    double amplitudeOffsetDb = 0.0;
    double uncertaintyDb = 0.0;
};

class ReceiverCalibrationTable {
public:
    void setPoints(const QVector<ReceiverCalibrationPoint> &points);
    const QVector<ReceiverCalibrationPoint> &points() const { return calibrationPoints; }
    bool isEmpty() const { return calibrationPoints.isEmpty(); }

    ReceiverCalibrationCorrection correctionAt(double frequencyHz) const;
    void applyAmplitudeCorrection(const std::vector<float> &frequencies,
                                  std::vector<float> &primary,
                                  std::vector<float> *secondary,
                                  double baseOffsetDb) const;

private:
    double interpolatedAmplitudeOffset(double frequencyHz, int &upperIndex) const;

    QVector<ReceiverCalibrationPoint> calibrationPoints;
};

#endif // RECEIVERCALIBRATIONTABLE_H

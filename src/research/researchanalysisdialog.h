#ifndef RESEARCHANALYSISDIALOG_H
#define RESEARCHANALYSISDIALOG_H

#include "spectrumscienceanalyzer.h"

#include <QDialog>
#include <QString>

#include <functional>
#include <complex>
#include <cstdint>
#include <limits>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTimer;
class ResearchPlotWidget;

struct ResearchRadioContext {
    double sampleRateHz = 0.0;
    double centerFrequencyHz = 0.0;
    double listeningFrequencyHz = 0.0;
    int inputMode = 0;
    int fftLength = 0;
    int fftWindowType = 0;
};

struct ResearchSpectrumSettings {
    int detectorMode = SPECTRUM_DETECTOR_SAMPLE;
    int detectorFrames = 8;
    double vbwHz = 0.0;
    int fftOverlapPercent = 0;
    int averageFrameCount = 0;
    bool percentile50 = false;
    bool percentile90 = false;
    bool percentile99 = false;
    int amplitudeUnit = 0;
};

class ResearchAnalysisDialog : public QDialog {
public:
    enum Tab {
        InterferenceTab = 0,
        StatisticsTab = 1,
        IqTab = 2,
        DualInputTab = 3,
        AnalyzerTab = 4
    };

    using Translator = std::function<QString(const QString &, const QString &)>;
    using ContextProvider = std::function<ResearchRadioContext()>;
    using SpectrumSettingsProvider = std::function<ResearchSpectrumSettings()>;
    using SpectrumSettingsApplier = std::function<void(const ResearchSpectrumSettings &)>;

    explicit ResearchAnalysisDialog(Translator translator,
                                    ContextProvider contextProvider,
                                    SpectrumSettingsProvider settingsProvider,
                                    SpectrumSettingsApplier settingsApplier,
                                    QWidget *parent = nullptr);

    void selectTab(Tab tab);
    void appendSpectrumFrame(const std::vector<float> &frequencies,
                             const std::vector<float> &levels,
                             const SpectrumScienceMetrics &metrics);

private:
    struct Peak {
        int bin = -1;
        double frequencyHz = 0.0;
        float levelDb = -200.0f;
        float prominenceDb = 0.0f;
        float referenceDeltaDb = 0.0f;
        int harmonic = 0;
        int family = 0;
    };

    QString trText(const QString &key, const QString &fallback) const;
    void refreshIqSnapshot();
    void updateInterference(const std::vector<float> &frequencies,
                            const std::vector<float> &levels);
    void updateStatistics(const std::vector<float> &levels,
                          const SpectrumScienceMetrics &metrics);
    void captureInterferenceReference();
    void clearInterferenceReference();
    void resetStatistics();
    void rebuildTexts();
    void applySpectrumSettings();
    void updateAnalyzerStatus();

    Translator translator;
    ContextProvider contextProvider;
    SpectrumSettingsProvider spectrumSettingsProvider;
    SpectrumSettingsApplier spectrumSettingsApplier;
    QTabWidget *tabs = nullptr;
    QTimer *iqTimer = nullptr;

    ResearchPlotWidget *interferencePlot = nullptr;
    QLabel *interferenceStatus = nullptr;
    QDoubleSpinBox *interferenceProminenceSpin = nullptr;
    QDoubleSpinBox *interferenceMinSpacingSpin = nullptr;
    QDoubleSpinBox *interferenceMaxSpacingSpin = nullptr;
    QPushButton *interferenceCaptureButton = nullptr;
    QPushButton *interferenceClearButton = nullptr;
    QTableWidget *interferenceTable = nullptr;
    std::vector<float> latestSpectrumFrequencies;
    std::vector<float> latestSpectrumLevels;
    std::vector<float> interferenceReferenceFrequencies;
    std::vector<float> interferenceReferenceLevels;
    double previousCombStepHz = 0.0;
    double previousCombMeanLevelDb = std::numeric_limits<double>::quiet_NaN();

    ResearchPlotWidget *statisticsPlot = nullptr;
    QLabel *statisticsStatus = nullptr;
    QDoubleSpinBox *statisticsThresholdSpin = nullptr;
    QSpinBox *statisticsHistorySpin = nullptr;
    QPushButton *statisticsResetButton = nullptr;
    std::vector<double> statisticsHistorySeconds;
    std::vector<float> statisticsHistoryDb;
    std::vector<float> statisticsActivityHeatmap;
    int statisticsHeatmapColumns = 0;
    int statisticsHeatmapRows = 0;
    qint64 statisticsStartMs = 0;

    ResearchPlotWidget *iqPlot = nullptr;
    QLabel *iqStatus = nullptr;
    QCheckBox *iqFreezeCheckbox = nullptr;
    QSpinBox *iqSampleCountSpin = nullptr;
    std::uint64_t lastIqEpoch = 0;
    std::uint64_t lastIqTotalFloatCount = 0;
    qint64 lastIqStatsTimeMs = 0;
    double measuredIqRateHz = 0.0;

    ResearchPlotWidget *dualPlot = nullptr;
    QLabel *dualStatus = nullptr;
    QCheckBox *dualFreezeCheckbox = nullptr;
    std::vector<std::complex<float>> dualCrossAverage;
    std::vector<float> dualFirstPowerAverage;
    std::vector<float> dualSecondPowerAverage;

    QComboBox *detectorCombo = nullptr;
    QSpinBox *detectorFramesSpin = nullptr;
    QDoubleSpinBox *vbwSpin = nullptr;
    QComboBox *fftOverlapCombo = nullptr;
    QSpinBox *averageFramesSpin = nullptr;
    QCheckBox *percentile50Checkbox = nullptr;
    QCheckBox *percentile90Checkbox = nullptr;
    QCheckBox *percentile99Checkbox = nullptr;
    QComboBox *amplitudeUnitCombo = nullptr;
    QLabel *analyzerStatus = nullptr;
};

#endif // RESEARCHANALYSISDIALOG_H

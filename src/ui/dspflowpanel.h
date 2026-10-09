#pragma once

#include <QDialog>
#include <QHash>
#include <QJsonObject>
#include <vector>

class DspFlowScene;
class QLabel;
class QGraphicsView;
class QMenu;
class QPushButton;
class QTimer;
class QToolButton;

class DspFlowPanel final : public QDialog {
    Q_OBJECT

public:
    explicit DspFlowPanel(QWidget *parent = nullptr);

    void setLanguage(bool ukrainian);
    QString configurationJson() const;
    bool setConfigurationJson(const QString &json);
    void setControlStates(bool receiverRunning, const QHash<QString, bool> &enabledByBlockType);
    void setFineTuneRangeHz(double rangeHz);
    void updateMultiVfoSpectrum(const QString &configurationJson,
                                const std::vector<float> &frequencies,
                                const std::vector<float> &levels);
    void addMultiVfoBranch(int channelIndex = -1);
    int vfoIndexForBlock(const QString &blockId) const;
    bool assignVfoIndexToBlock(const QString &blockId, int channelIndex);
    QJsonObject blockSettings(const QString &blockId) const;
    bool setBlockSettings(const QString &blockId, const QJsonObject &settings);

signals:
    void configurationChanged(const QString &json);
    void blockActivated(const QString &type, const QString &id);
    void blockCreated(const QString &type, const QString &id);
    void controlTriggered(const QString &controlType,
                          const QString &controlId,
                          const QString &targetType,
                          const QString &targetId);
    void controlStateRefreshRequested();
    void fineTuneDeltaRequested(double deltaHz);

private:
    void rebuildAddMenu();
    void scheduleConfigurationChanged();
    void addBlock(const QString &type);
    QString blockTitle(const QString &type) const;

    bool ukrainian_ = false;
    bool loading_ = false;
    DspFlowScene *scene_ = nullptr;
    QGraphicsView *view_ = nullptr;
    QToolButton *addButton_ = nullptr;
    QPushButton *openButton_ = nullptr;
    QPushButton *renameButton_ = nullptr;
    QPushButton *deleteButton_ = nullptr;
    QPushButton *defaultButton_ = nullptr;
    QPushButton *fitButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QMenu *addMenu_ = nullptr;
    QTimer *saveTimer_ = nullptr;
};

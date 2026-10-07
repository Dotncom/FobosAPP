#ifndef TRANSMITTERBACKEND_H
#define TRANSMITTERBACKEND_H

#include <QString>
#include <QVector>

#include <complex>

enum class TxModulation {
    Am,
    Nfm,
    Wfm,
    Dsb,
    Usb,
    Lsb,
    Cw,
    Ft8
};

struct TxConfiguration {
    TxModulation modulation = TxModulation::Nfm;
    double frequencyHz = 100000000.0;
    int sampleRate = 48000;
    float level = 0.50f;
    double deviationHz = 2500.0;
    double toneHz = 700.0;
    int cwWpm = 18;
    bool alignFt8ToUtcSlot = true;
};

class TransmitterBackend {
public:
    virtual ~TransmitterBackend() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual bool isRfCapable() const = 0;
    virtual bool start(const TxConfiguration &configuration, QString *error) = 0;
    virtual bool writeIq(const std::complex<float> *samples, int count, QString *error) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
};

class SimulatorTransmitterBackend final : public TransmitterBackend {
public:
    QString id() const override;
    QString displayName() const override;
    bool isRfCapable() const override;
    bool start(const TxConfiguration &configuration, QString *error) override;
    bool writeIq(const std::complex<float> *samples, int count, QString *error) override;
    void stop() override;
    bool isRunning() const override;

    const QVector<std::complex<float>> &capturedIq() const;
    const TxConfiguration &configuration() const;
    void clear();

private:
    TxConfiguration currentConfiguration;
    QVector<std::complex<float>> iq;
    bool running = false;
};

#endif // TRANSMITTERBACKEND_H

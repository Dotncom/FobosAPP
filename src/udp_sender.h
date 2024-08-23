// udp_sender.h
#ifndef UDP_SENDER_H
#define UDP_SENDER_H

#include <QObject>
#include <QUdpSocket>
#include <QByteArray>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

class UdpSender : public QObject {
    Q_OBJECT

public:
    UdpSender(QObject *parent = nullptr);
    void sendData(const QByteArray &data);

public slots:
    void process();
    
signals:
    void errorOccurred(QAbstractSocket::SocketError socketError);

private slots:
    void handleError(QAbstractSocket::SocketError socketError) {
        qWarning() << "Socket error:" << socketError;
    }
private:
    QUdpSocket *udpSocket;
    QByteArray buffer;
    QMutex mutex;
};

#endif // UDP_SENDER_H

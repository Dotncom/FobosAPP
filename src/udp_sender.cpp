// udp_sender.cpp
#include "udp_sender.h"
#include <QHostAddress>

UdpSender::UdpSender(QObject *parent) : QObject(parent) {
    udpSocket = new QUdpSocket(this);
}

void UdpSender::sendData(const QByteArray &data) {
    QMutexLocker locker(&mutex);
    buffer = data;
}

void UdpSender::process() {
    while (true) {
        QMutexLocker locker(&mutex);
        if (!buffer.isEmpty()) {
            udpSocket->writeDatagram(buffer, QHostAddress("127.0.0.1"), 2109);
            buffer.clear();
        }
        locker.unlock();
        QThread::msleep(10); 
    }
}

#include "dataprocessor.h"
#include <fobos.h>
#include <QDebug>
#include <main.h>
#include <cstring>
#include <QVector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <QElapsedTimer>

#define DEFAULT_BUFS_COUNT 32

extern int DEFAULT_BUF_LEN;
size_t iqDataSize = DEFAULT_BUF_LEN;
extern float* iqData;
extern fobos_dev_t *device;
extern int globalMode;
int actualBufLength = 32768;



DataProcessor::DataProcessor(fobos_dev_t* dev, QObject *parent)
    : QThread(parent), running(false) {
}
    
DataProcessor::~DataProcessor() {
    stop();
    wait();
}

void DataProcessor::run() {
    qDebug() << "Starting read operation based on globalMode...";
    running = true;
    iqData = new float[iqDataSize];
    if (globalMode == 0) {
        while (running) {
            int ret = fobos_rx_read_async(device, [](float *buf, uint32_t buf_length, void *ctx) {
                auto *processor = static_cast<DataProcessor*>(ctx);
				processor->handleData(buf, buf_length);
              },
              this,
              DEFAULT_BUFS_COUNT,
              DEFAULT_BUF_LEN);

            if (ret != FOBOS_ERR_OK) {
                qDebug() << "Failed to start async read, error code:" << ret;
                running = false;  // Ensure to stop the loop
                break;
            } else {
                qDebug() << "Async read started successfully.";
                exec();
            }
        }

    } else if (globalMode == 1 || globalMode == 2 || globalMode == 3) {
            int ret = fobos_rx_start_sync(device, DEFAULT_BUF_LEN);
            if (ret != FOBOS_ERR_OK) {
                qDebug() << "Failed to start sync mode, error code:" << ret;
                running = false;  // Ensure to stop the loop
                
            }		
        while (running) {
            uint32_t actual_buf_length;
            ret = fobos_rx_read_sync(device, iqData, &actual_buf_length);
            //qDebug() << "Buffer length is " << actualBufLength;
            if (ret != FOBOS_ERR_OK) {
                qDebug() << "Failed to read sync data, error code:" << ret;
                    running = false;  // Ensure to stop the loop
                    break;
            }

            actualBufLength = actual_buf_length;
            emit dataReady();
        }
    }  
}

void DataProcessor::handleData(float *buf, uint32_t buf_length) {
        if (buf_length < iqDataSize) {
        qDebug() << "Buffer length mismatch in handleData!";
        return;
    }
    iqData = buf;
    emit dataReady();
}
 

void DataProcessor::stop() {
    if (running) {
        running = false;
        if (globalMode == 0) {
            fobos_rx_cancel_async(device);
        } else if (globalMode == 1 || globalMode == 2 || globalMode == 3) {
            fobos_rx_stop_sync(device);
        }
        qDebug() << "Attempting to stop DataProcessor.";
        //{
         //   std::unique_lock<std::mutex> lock(dataMutex);
        //    isReading = false;
         //   dataCondition.notify_all();
          //  if (!dataCondition.wait_for(lock, std::chrono::seconds(5), [this]() { return !isReading; })) {
          //      qDebug() << "Error: DataProcessor did not stop reading within timeout.";
          //  } else {
          //      qDebug() << "DataProcessor stopped reading.";
          //  }
        //}
        QThread::quit();
        if (!QThread::wait(5000)) { // Timeout for waiting
            qDebug() << "Error: DataProcessor thread did not quit within timeout.";
        } else {
            qDebug() << "DataProcessor thread successfully quit.";
        }
        qDebug() << "DataProcessor stopped";
    }
}


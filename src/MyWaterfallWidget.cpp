#include "MyWaterfallWidget.h"
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QColor>
#include <QPainter>
#include <QThread>
#include <QOpenGLTexture>
#include <QWheelEvent>

bool changebit=false;

ComputeThread::ComputeThread(MyWaterfallWidget *widget) : widget(widget) {}

void ComputeThread::run() {
    if (widget) {
        widget->computeLineData();
    }
}

MyWaterfallWidget::MyWaterfallWidget(QWidget *parent)
    : QOpenGLWidget(parent), xMin(60000000), xMax(140000000), yMin(0), yMax(100), fftLength(65536), initialized(false), lineData(nullptr), secondGraph(false) {
		waterfallTexture = 0;
		computeThread = new ComputeThread(this);

}

MyWaterfallWidget::~MyWaterfallWidget() {
	delete[] lineData;
    computeThread->quit();
    computeThread->wait();
    delete computeThread;
}

void MyWaterfallWidget::wheelEvent(QWheelEvent *event) {
    emit scaleChanged(event->angleDelta().y() > 0 ? 1 : -1);
    event->accept();
}

void MyWaterfallWidget::initializeGL() {
	qDebug() << "attempting initializeOpenGLFunctions...";
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f); 
        if (lineData == nullptr) {
			qDebug() << "linedata nullptr";
        lineData = new unsigned char[fftLength*3];
        qDebug() << "linedata setted";
    }
        glGenTextures(1, &waterfallTexture);
        glBindTexture(GL_TEXTURE_2D, waterfallTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width(), height(), 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

    waterfallVbo.create();
    waterfallVbo.bind();
    waterfallVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    waterfallVbo.allocate(fftLength * height() * 3); ///
    waterfallVbo.release();
        computeThread->start();
	initialized = true;
qDebug() << "initializeGL done";
}

void MyWaterfallWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
        glBindTexture(GL_TEXTURE_2D, waterfallTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
}

void MyWaterfallWidget::setData(const std::vector<float> &xData, const std::vector<float> &yData, double xMin, double xMax, int fftLength, bool secondGraph) {
    this->xData = xData;
    this->yData = yData;
    this->xMin = xMin;
    this->xMax = xMax;
    this->fftLength = fftLength;
    this->secondGraph = secondGraph;
    yMin = *std::min_element(yData.begin(), yData.end());
    yMax = *std::max_element(yData.begin(), yData.end());

    waterfallVbo.bind();
    waterfallVbo.write(0, lineData, fftLength * 3); ///
    waterfallVbo.release();
    computeThread->quit();
    computeThread->wait();
	computeThread->start();

}

void MyWaterfallWidget::computeLineData() {
	QMutexLocker locker(&mutex);
    for (int id = 0; id < fftLength && id < xData.size(); ++id) {
        float intensity = yData[(id + fftLength / 2) % fftLength];
        QColor color = valueToColor(intensity);
        int x1 = static_cast<int>((xData[id] - xMin) * width() / (xMax - xMin));
        if (x1 >= 0 && x1 < fftLength) {
        lineData[x1 * 3 + 0] = static_cast<unsigned char>(color.red());
        lineData[x1 * 3 + 1] = static_cast<unsigned char>(color.green());
        lineData[x1 * 3 + 2] = static_cast<unsigned char>(color.blue());
        //id=id+16;
	}
	//qDebug() << "x1:" << x1 << "fftLength:" << fftLength << "lineData:" << (void*)lineData;

    }
    
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
}

void MyWaterfallWidget::paintGL() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	if (secondGraph == true) {
	
	glViewport(0, 0, width(), height());
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(xMin, xMax, 0, height(), -1, 1);
    glColor3f(1.0f, 1.0f, 1.0f);
	glBegin(GL_LINE_STRIP);
	for (int i = 0; i < fftLength && i < xData.size(); ++i) {
    float intensityS = yData[(i + fftLength / 2) % fftLength]; 
    QColor colors = valueToColors(intensityS);
    glColor3f(colors.redF(), colors.greenF(), colors.blueF());
    float yPos = (intensityS - yMin) / (yMax - yMin) * (height() * 3 / 4);
    glVertex2f(xData[i], yPos); 
	}
	glEnd();
	} else {
    //waterfall
  
    glViewport(0, 0, width(), height());
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width(), 0, height(), -1, 1);
   
	glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, waterfallTexture);
     glColor3f(1.0f, 1.0f, 1.0f);
    waterfallVbo.bind();
	std::vector<unsigned char> tempBuffer(fftLength * (height() - 1) * 3);///
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, tempBuffer.data());
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 1, width(), height() - 1, GL_RGB, GL_UNSIGNED_BYTE, tempBuffer.data());
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width(), 1, GL_RGB, GL_UNSIGNED_BYTE, lineData);
        glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0, 0);               // Bottom-left corner
    glTexCoord2f(1.0f, 1.0f); glVertex2f(width(), 0);         // Bottom-right corner
    glTexCoord2f(1.0f, 0.0f); glVertex2f(width(), height());  // Top-left corner
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0, height());  	  // Top-right corner
        glEnd();
        glBindTexture(GL_TEXTURE_2D, 0);
        waterfallVbo.release();
	}
}

QColor MyWaterfallWidget::valueToColor(float value) {
    value = qBound(0.0f, value, 1.0f);
    int r, g, b;
    if (value < 0.35f) {
        float ratio = value / 0.2f;
        r = 0;
        g = 0;
        b = static_cast<int>(255 * ratio);
    } else if (value < 0.65f) {
        float ratio = (value - 0.4f) / 0.2f;
        r = 0;
        g = static_cast<int>(255 * (1 - ratio));
        b = 255;
    } else if (value < 0.75f) {
        float ratio = (value - 0.65f) / 0.2f;
        r = 0;
        g = 255;
        b = static_cast<int>(255 * (1 - ratio));
    } else if (value < 0.84f) {
        float ratio = (value - 0.75f) / 0.2f;
        r = static_cast<int>(255 * ratio);
        g = 255;
        b = 0;
    } else if (value < 0.92f) {
        float ratio = (value - 0.84f) / 0.2f;
        r = 255;
        g = static_cast<int>(255 * (1 - ratio));
        b = 0;
    } else {
        float ratio = (value - 0.92f) / 0.2f;
        r = 255;
        g = static_cast<int>(128 * (1 - ratio));
        b = 0;
    }
    return QColor(r, g, b);
}

 QColor MyWaterfallWidget::valueToColors(float value) {
    value = qBound(0.0f, value, 1.0f);
    int r, g, b;
    if (value < 0.12f) {
        float ratio = value / 0.2f;
        r = 0;
        g = 0;
        b = static_cast<int>(255 * ratio);
    } else if (value < 0.26f) {
        float ratio = (value - 0.12f) / 0.2f;
        r = 0;
        g = static_cast<int>(255 * (1 - ratio));
        b = 255;
    } else if (value < 0.40f) {
        float ratio = (value - 0.28f) / 0.2f;
        r = 0;
        g = 255;
        b = static_cast<int>(255 * (1 - ratio));
    } else if (value < 0.54f) {
        float ratio = (value - 0.40f) / 0.2f;
        r = static_cast<int>(255 * ratio);
        g = 255;
        b = 0;
    } else if (value < 0.68f) {
        float ratio = (value - 0.54f) / 0.2f;
        r = 255;
        g = static_cast<int>(255 * (1 - 0.5f * ratio));
        b = 0;    
    } else if (value < 0.86f) {
        float ratio = (value - 0.68f) / 0.2f;
        r = 255;
        g = static_cast<int>(128 * (1 - 0.5f * ratio));
        b = 0;
    } else {
        float ratio = (value - 0.86f) / 0.2f;
        r = 255;
        g = 0;
        b = static_cast<int>(255 * (1 - ratio));
    }
    return QColor(r, g, b);
}

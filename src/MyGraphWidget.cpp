#include "MyGraphWidget.h"
#include <QOpenGLBuffer>
#include <QColor>
#include <QPainter>
#include <QDebug>
#include <QThread>
#include <QWheelEvent>

MyGraphWidget::MyGraphWidget(QWidget *parent)
    : QOpenGLWidget(parent), xMin(60000000), xMax(140000000), yMin(0), yMax(100), fftLength(65536), initialized(false){

}

MyGraphWidget::~MyGraphWidget() {

}

void MyGraphWidget::initializeGL() {
	qDebug() << "attempting initializeOpenGLFunctions...";
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f); 

	vbo.create();
    vbo.bind();
    vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    vbo.release();
	initialized = true;

qDebug() << "initializeGL done";
}

void MyGraphWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void MyGraphWidget::setData(const std::vector<float> &xData, const std::vector<float> &yData, double xMin, double xMax, int fftLength) {
    this->xData = xData;
    this->yData = yData;
    this->xMin = xMin;
    this->xMax = xMax;
    this->fftLength = fftLength;
    yMin = *std::min_element(yData.begin(), yData.end());
    yMax = *std::max_element(yData.begin(), yData.end());

	std::vector<float> vertexData;
    for (int i = 0; i < fftLength && i < xData.size(); ++i) {
        vertexData.push_back(xData[i]);
        vertexData.push_back(yData[(i + fftLength / 2) % fftLength]);
    }
    vbo.bind();
    vbo.allocate(vertexData.data(), static_cast<int>(vertexData.size() * sizeof(float)));
    vbo.release();
	update();
}


void MyGraphWidget::paintGL() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
    //spectrum
    glViewport(0, 0, width(), height());
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(xMin, xMax, yMin, yMax, -1, 1);
    
    vbo.bind();
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, nullptr);
    glColor3f(0.0f, 1.0f, 1.0f);
    glDrawArrays(GL_LINE_STRIP, 0, fftLength);
    glDisableClientState(GL_VERTEX_ARRAY);
    vbo.release();
    

}

QColor MyGraphWidget::valueToColor(float value) {
    value = qBound(0.0f, value, 1.0f);
    int r, g, b;
    if (value < 0.16f) {
        float ratio = value / 0.2f;
        r = 0;
        g = 0;
        b = static_cast<int>(255 * ratio);
    } else if (value < 0.33f) {
        float ratio = (value - 0.16f) / 0.2f;
        r = 0;
        g = static_cast<int>(255 * (1 - ratio));
        b = 255;
    } else if (value < 0.5f) {
        float ratio = (value - 0.33f) / 0.2f;
        r = 0;
        g = 255;
        b = static_cast<int>(255 * (1 - ratio));
    } else if (value < 0.66f) {
        float ratio = (value - 0.5f) / 0.2f;
        r = static_cast<int>(255 * ratio);
        g = 255;
        b = 0;
    } else if (value < 0.83f) {
        float ratio = (value - 0.66f) / 0.2f;
        r = 255;
        g = static_cast<int>(255 * (1 - 0.5f * ratio));
        b = 0;
    } else {
        float ratio = (value - 0.83f) / 0.2f;
        r = 255;
        g = static_cast<int>(128 * (1 - ratio));
        b = 0;
    }
    return QColor(r, g, b);
}

void MyGraphWidget::wheelEvent(QWheelEvent *event) {
    emit scaleChanged(event->angleDelta().y() > 0 ? 1 : -1);
    event->accept();
}

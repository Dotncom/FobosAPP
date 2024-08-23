#ifndef MYGRAPHWIDGET_H
#define MYGRAPHWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QWheelEvent>
#include <vector>
#include <QColor>



class MyGraphWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit MyGraphWidget(QWidget *parent = nullptr);
    ~MyGraphWidget();
    bool initialized;
    void setData(const std::vector<float> &xData, const std::vector<float> &yData, double minFrequency, double maxFrequency, int fftLength);
    void computeLineData(); 
signals:
    void scaleChanged(int delta);
protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
	void wheelEvent(QWheelEvent *event) override;
private:
QOpenGLBuffer vbo;

    QColor valueToColor(float value);
    std::vector<float> xData;
    std::vector<float> yData;
    float yMin, yMax;
    double xMin, xMax;
    int fftLength;

};

#endif // MYGRAPHWIDGET_H

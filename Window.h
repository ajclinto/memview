#ifndef Window_H
#define Window_H

#include <QtGui>
#include <QGLWidget>
#include <QtOpenGL>
#include "Math.h"
#include "GLImage.h"
#include "StopWatch.h"
#include "MemoryState.h"
#include "DisplayLayout.h"
#include <queue>

class MemViewWidget;
class MemViewScroll;
class Loader;

class Window : public QMainWindow {
    Q_OBJECT

public:
	     Window(int argc, char *argv[]);
    virtual ~Window();

private:
    QMenu		*myFileMenu;
    QAction		*myQuit;

    static const int	 theVisCount = 3;

    QMenu		*myVisMenu;
    QActionGroup	*myVisGroup;
    QAction		*myVis[theVisCount];

    MemViewWidget	*myMemView;
    MemViewScroll	*myScrollArea;
    QGridLayout		*myLayout;
};

// A scroll area to contain the memory view.  We'll pass off control over
// the vertical scrollbar to MemViewWidget.
class MemViewScroll : public QAbstractScrollArea {
public:
    MemViewScroll(QWidget *parent)
	: QAbstractScrollArea(parent) {}

    // Viewport events need to be passed directly to the viewport.
    bool    viewportEvent(QEvent *) { return false; }
};

// A widget to render the memory visualization.
class MemViewWidget : public QGLWidget {
    Q_OBJECT

public:
	     MemViewWidget(int argc, char *argv[],
			    QWidget *parent,
			    QScrollBar *vscrollbar,
			    QScrollBar *hscrollbar,
			    QStatusBar *status);
    virtual ~MemViewWidget();

    void	paint(QPaintEvent *event)
		{ paintEvent(event); }
protected:
    void	initializeGL();
    void	resizeGL(int width, int height);
    void	paintGL();
    void	resizeEvent(QResizeEvent *event);

    void	mousePressEvent(QMouseEvent *event);
    void	mouseMoveEvent(QMouseEvent *event);
    void	mouseReleaseEvent(QMouseEvent *event);
    void	wheelEvent(QWheelEvent *event);

    void	timerEvent(QTimerEvent *event);

    void	changeZoom(int zoom);

private slots:
    void    linear();
    void    block();
    void    hilbert();

private:
    GLImage<uint32>	 myImage;
    QScrollBar		*myVScrollBar;
    QScrollBar		*myHScrollBar;
    QStatusBar		*myStatusBar;

    QGLShaderProgram	*myProgram;
    GLuint		 myTexture;
    GLuint		 myPixelBuffer;

    DisplayLayout	 myDisplay;
    MemoryState		*myState;
    MemoryState		*myZoomState;
    Loader		*myLoader;
    uint32		 myPrevTime;
    int			 myZoom;

    struct Velocity {
	Velocity(double a, double b, double t) : x(a), y(b), time(t) {}
	Velocity operator+(const Velocity &v) const
	{
	    return Velocity(v.x + x, v.y + y, v.time + time);
	}
	Velocity &operator+=(const Velocity &v)
	{
	    x += v.x;
	    y += v.y;
	    time += v.time;
	    return *this;
	}
	Velocity &operator*=(double a)
	{
	    x *= a;
	    y *= a;
	    time *= a;
	    return *this;
	}

	double x;
	double y;
	double time;
    };

    StopWatch	 myStopWatch;
    StopWatch	 myPaintInterval;
    QPoint	 myMousePos;
    std::queue<Velocity> myVelocity;
    bool	 myDragging;
};

#endif

#ifndef NOTEPAD_H

#include <QtGui>
#include <QGLWidget>
#include "Math.h"
#include "GLImage.h"

class MemoryState;
class MemViewWidget;

class Window : public QMainWindow {
    Q_OBJECT

public:
	     Window(int argc, char *argv[]);
    virtual ~Window();

private:
    QMenu		*myFileMenu;
    QAction		*myQuit;

    static const int	 theVisCount = 2;

    QMenu		*myVisMenu;
    QActionGroup	*myVisGroup;
    QAction		*myVis[theVisCount];

    MemViewWidget	*myMemView;
};

// A widget to render the memory visualization.
class MemViewWidget : public QGLWidget {
    Q_OBJECT

public:
	     MemViewWidget(int argc, char *argv[]);
    virtual ~MemViewWidget();

protected:
    void	paintGL();
    void	resizeEvent(QResizeEvent *event);

private slots:
    void    linear();
    void    block();
    void    tick();

private:
    GLImage	 myImage;
    QTimer	*myTimer;

    MemoryState	*myState;
};

#endif

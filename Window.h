#ifndef NOTEPAD_H

#include <QtGui>

class MemoryState;
class MemViewWidget;

class Window : public QMainWindow {
    Q_OBJECT

public:
	     Window(int argc, char *argv[]);
    virtual ~Window();

private slots:
    void    quit();

private:
    QMenu		*myFileMenu;
    QAction		*myQuit;

    QMenu		*myVisualizationMenu;
    QAction		*myLinear;
    QAction		*myRecursiveBlock;

    MemViewWidget	*myMemView;
};

// A widget to render the memory visualization.
class MemViewWidget : public QWidget {
    Q_OBJECT

public:
	     MemViewWidget(int argc, char *argv[]);
    virtual ~MemViewWidget();

protected:
    void	paintEvent(QPaintEvent *event);
    void	resizeEvent(QResizeEvent *event);

private slots:
    void    linear();
    void    block();
    void    tick();

private:
    QImage	 myImage;
    QTimer	*myTimer;

    MemoryState	*myState;
};

#endif

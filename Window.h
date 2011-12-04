#ifndef NOTEPAD_H

#include <QtGui>

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

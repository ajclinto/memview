#ifndef NOTEPAD_H

#include <QtGui>

class MemoryState;

class Window : public QMainWindow {
    Q_OBJECT

public:
	     Window();
    virtual ~Window();

private slots:
    void    quit();
    void    tick();

private:
    QImage	 myImage;
    QLabel	*myLabel;
    QAction	*myQuit;
    QMenu	*myFileMenu;
    QTimer	*myTimer;

    MemoryState	*myState;
};

#endif

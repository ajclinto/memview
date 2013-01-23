/*
   This file is part of memview, a real-time memory trace visualization
   application.

   Copyright (C) 2013 Andrew Clinton

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

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
class StackTraceMap;

class Window : public QMainWindow {
    Q_OBJECT

public:
	     Window(int argc, char *argv[]);
    virtual ~Window();

private:
    QMenu		*myFileMenu;
    QAction		*myQuit;

    QMenu		*myLayoutMenu;

    static const int	 theVisCount = 3;
    QActionGroup	*myVisGroup;
    QAction		*myVis[theVisCount];

    static const int	 theLayoutCount = 2;
    QActionGroup	*myLayoutGroup;
    QAction		*myLayout[theLayoutCount];

    QMenu		*myDisplayMenu;

    static const int	 theDisplayCount = 2;
    QActionGroup	*myDisplayGroup;
    QAction		*myDisplay[theDisplayCount];

    MemViewWidget	*myMemView;
    MemViewScroll	*myScrollArea;
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

    bool	event(QEvent *event);

    void	resizeEvent(QResizeEvent *event);

    void	mousePressEvent(QMouseEvent *event);
    void	mouseMoveEvent(QMouseEvent *event);
    void	mouseReleaseEvent(QMouseEvent *event);
    void	wheelEvent(QWheelEvent *event);

    void	timerEvent(QTimerEvent *event);

    void	resizeImage(int zoom);
    void	changeZoom(int zoom);
    QPoint	zoomPos(QPoint pos, int zoom) const;

private slots:
    void    linear();
    void    block();
    void    hilbert();

    void    compact();
    void    full();

    void    rwdisplay();
    void    threaddisplay();

private:
    GLImage<uint32>	 myImage;
    QScrollBar		*myVScrollBar;
    QScrollBar		*myHScrollBar;
    QStatusBar		*myStatusBar;
    std::string		 myPath;

    QGLShaderProgram	*myProgram;
    GLuint		 myTexture;
    GLuint		 myPixelBuffer;

    DisplayLayout	 myDisplay;
    MemoryState		*myState;
    MemoryState		*myZoomState;
    StackTraceMap	*myStackTrace;
    Loader		*myLoader;
    QString		 myEventInfo;
    uint64		 myPrevEvents;
    int			 myZoom;
    int			 myFastTimer;
    int			 mySlowTimer;
    int			 myDisplayMode;

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
    StopWatch	 myEventTimer;
    QPoint	 myMousePos;
    std::queue<Velocity> myVelocity;
    bool	 myDragging;
};

#endif

#include "Window.h"
#include "Color.h"
#include "MemoryState.h"
#include "Loader.h"
#include <fstream>

#define USE_PBUFFER

static const QSize	theDefaultSize(800, 600);

Window::Window(int argc, char *argv[])
{
    myQuit = new QAction(tr("&Quit"), this);

    connect(myQuit, SIGNAL(triggered()), qApp, SLOT(quit()));

    myFileMenu = menuBar()->addMenu(tr("&File"));
    myFileMenu->addSeparator();
    myFileMenu->addAction(myQuit);

    static const char	*theVisNames[theVisCount] = {
	"&Linear",
	"&Recursive Block",
	"&Hilbert Curve"
    };

    myVisGroup = new QActionGroup(this);
    myVisMenu = menuBar()->addMenu(tr("&Visualization"));
    for (int i = 0; i < theVisCount; i++)
    {
	myVis[i] = new QAction(tr(theVisNames[i]), myVisGroup);
	myVis[i]->setCheckable(true);
	myVisMenu->addAction(myVis[i]);
    }
    myVis[2]->setChecked(true);

    myScrollArea = new MemViewScroll(this);

    setStatusBar(statusBar());

    myMemView = new MemViewWidget(argc, argv,
	    myScrollArea,
	    myScrollArea->verticalScrollBar(),
	    myScrollArea->horizontalScrollBar(),
	    statusBar());

    connect(myVis[0], SIGNAL(triggered()), myMemView, SLOT(linear()));
    connect(myVis[1], SIGNAL(triggered()), myMemView, SLOT(block()));
    connect(myVis[2], SIGNAL(triggered()), myMemView, SLOT(hilbert()));

    setWindowTitle("Memview");

    myScrollArea->setViewport(myMemView);
    setCentralWidget(myScrollArea);

    resize(theDefaultSize);
}

Window::~Window()
{
}

//
// MemViewWidget
//

MemViewWidget::MemViewWidget(int argc, char *argv[],
	QWidget *parent,
	QScrollBar *vscrollbar,
	QScrollBar *hscrollbar,
	QStatusBar *status)
    : QGLWidget(QGLFormat(QGL::NoDepthBuffer), parent)
    , myVScrollBar(vscrollbar)
    , myHScrollBar(hscrollbar)
    , myStatusBar(status)
    , myTexture(0)
    , myPixelBuffer(0)
    , myZoom(0)
    , myStopWatch(false)
    , myPaintInterval(false)
    , myDragging(false)
{
    // We need mouse events even when no buttons are held down, for status
    // bar updates
    setMouseTracking(true);

    // Use a fixed-width font for the status bar
    QFont	font("Monospace");
    font.setStyleHint(QFont::TypeWriter);
    myStatusBar->setFont(font);

    const char	*ignore = extractOption(argc, argv, "--ignore-bits=");
    int		 ignorebits = ignore ? atoi(ignore) : 2;

    myState = new MemoryState(ignorebits);
    myZoomState = myState;
    myLoader = new Loader(myState);

    if (myLoader->openPipe(argc, argv))
    {
	// Start loading data in a new thread
	myLoader->start();
    }

    myPrevTime = MemoryState::theStale;

    startTimer(30);

    myPaintInterval.start();
}

MemViewWidget::~MemViewWidget()
{
    delete myLoader;
}

void
MemViewWidget::linear()
{
    myDisplay.setVisualization(DisplayLayout::LINEAR);
    update();
}

void
MemViewWidget::block()
{
    myDisplay.setVisualization(DisplayLayout::BLOCK);
    changeZoom(myZoom);
    update();
}

void
MemViewWidget::hilbert()
{
    myDisplay.setVisualization(DisplayLayout::HILBERT);
    changeZoom(myZoom);
    update();
}

// Load a file into a buffer.  The buffer is owned by the caller, and
// should be freed with delete[].
static char *
loadTextFile(const char *filename)
{
    std::ifstream   is(filename);
    if (!is.good())
	return 0;

    is.seekg(0, std::ios::end);
    long length = is.tellg();
    is.seekg(0, std::ios::beg);

    if (!length)
	return 0;

    char *buffer = new char[length+1];

    is.read(buffer, length);
    buffer[length] = '\0';

    return buffer;
}

void
MemViewWidget::initializeGL()
{
    // We're doing our own dithering.  Though having this enabled didn't
    // seem to produce any dithering.
    glDisable(GL_DITHER);

    glActiveTexture(GL_TEXTURE0);
    glGenBuffers(1, &myPixelBuffer);

    glGenTextures(1, &myTexture);
    glBindTexture(GL_TEXTURE_RECTANGLE, myTexture);

    const GLuint type = GL_NEAREST;
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, type);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, type);

    QGLShader *vshader = new QGLShader(QGLShader::Vertex, this);
    const char *vsrc = loadTextFile("shader.vert");
    vshader->compileSourceCode(vsrc);
    delete [] vsrc;

    QGLShader *fshader = new QGLShader(QGLShader::Fragment, this);
    const char *fsrc = loadTextFile("shader.frag");
    fshader->compileSourceCode(fsrc);
    delete [] fsrc;

    myProgram = new QGLShaderProgram(this);
    myProgram->addShader(vshader);
    myProgram->addShader(fshader);
    myProgram->link();

    myProgram->bind();

    myProgram->setUniformValue("theState", 0);
    myProgram->setUniformValue("theStale", MemoryState::theStale);
    myProgram->setUniformValue("theHalfLife", MemoryState::theHalfLife);
}

void
MemViewWidget::resizeGL(int width, int height)
{
    glViewport(0, 0, (GLint)width, (GLint)height);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    resizeImage(myZoom);
}

void
MemViewWidget::resizeImage(int zoom)
{
    int w = width();
    int h = height();

    if (zoom < 0)
    {
	w >>= (-zoom) >> 1;
	h >>= (-zoom) >> 1;
    }

    myVScrollBar->setPageStep(h);
    myHScrollBar->setPageStep(w);

#ifdef USE_PBUFFER
    myImage.setSize(w, h);
#else
    myImage.resize(w, h);
#endif

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, myPixelBuffer);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, myImage.bytes(), 0, GL_STREAM_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

static void
setScrollMax(QScrollBar *scroll, int size, bool with_margin = true)
{
    int margin = with_margin ? (scroll->pageStep() >> 1) : 0;
    int nmax = SYSmax(size - scroll->pageStep() + margin, 0);

    scroll->setMaximum(nmax);
    scroll->setMinimum(-margin);
}

void
MemViewWidget::paintGL()
{
#if 0
    StopWatch	timer;
    fprintf(stderr, "interval %f time ", myPaintInterval.lap());
#endif

#ifdef USE_PBUFFER
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, myPixelBuffer);

    myImage.setData((uint32 *)
	    glMapBufferARB(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY));
#endif

    myDisplay.update(*myState, *myZoomState,
	    myImage.width(), SYSmax(myZoom, 0));
    myDisplay.fillImage(myImage, *myZoomState,
	    myHScrollBar->value(),
	    myVScrollBar->value());

#ifdef USE_PBUFFER
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_R32UI,
	    myImage.width(), myImage.height(), 0, GL_RED_INTEGER,
	    GL_UNSIGNED_INT, 0 /* offset in PBO */);
#else
    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_R32UI,
	    myImage.width(), myImage.height(), 0, GL_RED_INTEGER,
	    GL_UNSIGNED_INT, myImage.data());
#endif

    myProgram->setUniformValue("theTime", myState->getTime());

    setScrollMax(myVScrollBar, myDisplay.height());
    setScrollMax(myHScrollBar, myDisplay.width(),
	    myDisplay.getVisualization() != DisplayLayout::LINEAR);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0, 0.0); glVertex3i(-1, -1, -1);
    glTexCoord2f(1.0, 0.0); glVertex3i(1, -1, -1);
    glTexCoord2f(1.0, 1.0); glVertex3i(1, 1, -1);
    glTexCoord2f(0.0, 1.0); glVertex3i(-1, 1, -1);
    glEnd();

    update();
}

void
MemViewWidget::resizeEvent(QResizeEvent *event)
{
    QGLWidget::resizeEvent(event);
    update();
}

QPoint
MemViewWidget::zoomPos(QPoint pos, int zoom) const
{
    if (zoom < 0)
    {
	pos.rx() *= myImage.width();
	pos.rx() /= width();
	pos.ry() *= myImage.height();
	pos.ry() /= height();
    }
    return pos;
}

void
MemViewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
	myStopWatch.start();
	myDragging = true;
	myVelocity = std::queue<Velocity>();
    }

    myMousePos = event->pos();
}

void
MemViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
    {
	double  time = myStopWatch.elapsed();
	QPoint  dir = zoomPos(myMousePos - event->pos(), myZoom);
	myHScrollBar->setValue(myHScrollBar->value() + dir.x());
	myVScrollBar->setValue(myVScrollBar->value() + dir.y());

	if (myVelocity.size() >= 5)
	    myVelocity.pop();
	myVelocity.push(Velocity(dir.x(), dir.y(), time));
    }
    else
    {
#if 1
	QPoint  pos = zoomPos(event->pos(), myZoom);
	uint64 qaddr = myDisplay.queryPixelAddress(*myZoomState,
		myHScrollBar->value() + pos.x(),
		myVScrollBar->value() + pos.y());

	QString	message;
	myZoomState->printStatusInfo(message, qaddr);

	if (message.isEmpty())
	    myStatusBar->clearMessage();
	else
	    myStatusBar->showMessage(message);
#endif
    }

    myMousePos = event->pos();

    update();
}

static const double theDragDelay = 0.1;

void
MemViewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
	myDragging = false;

	// Compute the average velocity.  We'll require at least 2 samples
	// to avoid spikes.  Only consider samples within a specified time
	// interval.
	int	  size = myVelocity.size();
	double    time = myStopWatch.elapsed() - theDragDelay;
	Velocity  vel(0, 0, 0);
	while (myVelocity.size())
	{
	    if (myVelocity.front().time > time)
		vel += myVelocity.front();
	    else
		size--;
	    myVelocity.pop();
	}

	if (size > 1)
	{
	    vel *= 1.0 / theDragDelay;
	    myVelocity.push(vel);
	}

	myStopWatch.start();
    }
}

void
MemViewWidget::wheelEvent(QWheelEvent *event)
{
    const bool	linear = myDisplay.getVisualization() == DisplayLayout::LINEAR;
    const int	inc = linear ? 1 : 2;

    int	zoom = myZoom;

    if (event->delta() < 0)
    {
	if (zoom < 0)
	    zoom += 2;
	else
	    zoom += inc;
    }
    else if (event->delta() > 0)
    {
	if (zoom <= 0)
	    zoom -= 2;
	else
	    zoom -= inc;
    }

    changeZoom(zoom);
}

static void
minScroll(QScrollBar *scroll, int x, int size, bool zoomout)
{
    int ox = x;
    x += scroll->value();

    if (zoomout)
	x >>= 1;
    else
    {
	x <<= 1;
	size <<= 1;
    }

    x -= ox;

    setScrollMax(scroll, size);
    scroll->setValue(x);
}

static void
magScroll(QScrollBar *scroll, int x, int size, bool zoomout)
{
    if (zoomout)
    {
	x >>= 1;
	x = scroll->value() - x;
    }
    else
    {
	x += scroll->value();
    }

    setScrollMax(scroll, size);
    scroll->setValue(x);
}

static void
magScrollLinear(QScrollBar *scroll, int x, int size, bool zoomout)
{
    // This is intended only to approximate the correct homing, since the
    // full re-layout that occurs in linear mode can cause blocks to
    // reposition arbitrarily.
    if (zoomout)
    {
	x += scroll->value();
	x >>= 1;
	x = scroll->value() - x;
	size >>= 1;
    }
    else
    {
	x += scroll->value() << 1;
	size <<= 1;
    }

    setScrollMax(scroll, size);
    scroll->setValue(x);
}

void
MemViewWidget::changeZoom(int zoom)
{
    const bool	linear = myDisplay.getVisualization() == DisplayLayout::LINEAR;

    // Zoom in increments of 2 for block display
    if (!linear)
	zoom &= ~1;

    // Zoom in increments of 2 for magnification
    if (zoom < 0)
	zoom = -((-zoom) & ~1);

    zoom = SYSclamp(zoom, -16, 28);

    if (zoom != myZoom)
    {
	if (zoom > 0)
	{
	    myZoomState = new MemoryState(
		    myLoader->getBaseState()->getIgnoreBits()+zoom);
	    myLoader->setZoomState(myZoomState);
	}
	else
	{
	    myLoader->clearZoomState();
	    myZoomState = myLoader->getBaseState();
	}

	const bool zoomout = zoom > myZoom;
	QPoint zpos = myMousePos;

	if (zoom < 0 || myZoom < 0)
	{
	    // This makes a GL call
	    resizeImage(zoom);

	    // This needs to be computed based on the new zoom value
	    zpos = zoomPos(zpos, zoom);
	    if (!linear)
	    {
		magScroll(myHScrollBar, zpos.x(), myDisplay.width(), zoomout);
		magScroll(myVScrollBar, zpos.y(), myDisplay.height(), zoomout);
	    }
	    else
	    {
		magScrollLinear(myVScrollBar, zpos.y(), myDisplay.height(), zoomout);
	    }
	}
	else
	{
	    // If the loader isn't running, start it just to do the downres
	    // for us.
	    if (!myLoader->isRunning())
		myLoader->start();

	    if (!linear)
		minScroll(myHScrollBar, zpos.x(), myDisplay.width(), zoomout);
	    minScroll(myVScrollBar, zpos.y(), myDisplay.height(), zoomout);
	}

	myZoom = zoom;
    }
}

void
shortenDrag(double &val, double delta)
{
    delta *= 2.0;
    if (val > 0)
    {
	val -= delta*val;
	val = SYSmax(val, 0.0);
    }
    else if (val < 0)
    {
	val -= delta*val;
	val = SYSmin(val, 0.0);
    }
}

void
MemViewWidget::timerEvent(QTimerEvent *)
{
    if (!myDragging && myVelocity.size())
    {
	Velocity   &vel = myVelocity.front();
	double	    time = myStopWatch.lap();
	int	    drag[2];

	drag[0] = (int)(vel.x * time + 0.5F);
	drag[1] = (int)(vel.y * time + 0.5F);

	shortenDrag(vel.x, time);
	shortenDrag(vel.y, time);

	myHScrollBar->setValue(myHScrollBar->value() + drag[0]);
	myVScrollBar->setValue(myVScrollBar->value() + drag[1]);

	if (drag[0] || drag[1])
	{
	    update();
	}
    }

    if (myState->getTime() != myPrevTime)
    {
	update();
	myPrevTime = myState->getTime();
    }
}


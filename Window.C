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

#include "Window.h"
#include "Color.h"
#include "MemoryState.h"
#include "Loader.h"
#include <fstream>
#include <sys/ptrace.h>
#include <sys/wait.h>

#define USE_PBUFFER

static const QSize	theDefaultSize(800, 600);

LogSlider::LogSlider(const char *name, int maxlogval, int deflogval)
{
    mySlider = new QSlider(Qt::Horizontal);
    mySlider->setRange(0, maxlogval);
    mySlider->setSingleStep(1);
    mySlider->setPageStep(5);
    mySlider->setTickPosition(QSlider::TicksBothSides);
    mySlider->setTracking(true);
    mySlider->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    mySlider->setFixedWidth(300);

    myLabel = new QLabel(name);
    myNumber = new QLabel;
    myNumber->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Ignored);
    myNumber->setFixedWidth(50);
    myNumber->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QHBoxLayout	*layout = new QHBoxLayout;

    layout->addWidget(myLabel);
    layout->addWidget(mySlider);
    layout->addWidget(myNumber);

    setLayout(layout);

    connect(mySlider, SIGNAL(valueChanged(int)), this, SLOT(fromLog(int)));

    mySlider->setValue(deflogval);
    fromLog(mySlider->value());
}

void
LogSlider::setLogValue(int value)
{
    mySlider->setValue(value);
}

void LogSlider::fromLog(int value)
{
    value = 1 << value;
    myNumber->setNum(value);
    emit valueChanged(value);
}


Window::Window(int argc, char *argv[])
{
    const char	*batchsize = extractOption(argc, argv, "--batch-size=");

    myScrollArea = new MemViewScroll(this);

    setStatusBar(statusBar());

    myMemView = new MemViewWidget(argc, argv,
	    myScrollArea,
	    myScrollArea->verticalScrollBar(),
	    myScrollArea->horizontalScrollBar(),
	    statusBar());

    myQuit = new QAction(tr("&Quit"), this);

    connect(myQuit, SIGNAL(triggered()), qApp, SLOT(quit()));

    myFileMenu = menuBar()->addMenu(tr("&File"));
    myFileMenu->addSeparator();
    myFileMenu->addAction(myQuit);

    myLayoutMenu = menuBar()->addMenu(tr("&Layout"));

    static const char	*theVisNames[theVisCount] = {
	"&Hilbert Curve",
	"&Recursive Block",
	"&Linear",
    };

    myVisGroup = createActionGroup(
	    myLayoutMenu, theVisNames, myVis, theVisCount, 0);

    myLayoutMenu->addSeparator();

    static const char	*theLayoutNames[theLayoutCount] = {
	"&Compact",
	"&Full Size",
    };

    myLayoutGroup = createActionGroup(
	    myLayoutMenu, theLayoutNames, myLayout, theLayoutCount, 0);

    connect(myVis[0], SIGNAL(triggered()), myMemView, SLOT(hilbert()));
    connect(myVis[1], SIGNAL(triggered()), myMemView, SLOT(block()));
    connect(myVis[2], SIGNAL(triggered()), myMemView, SLOT(linear()));

    connect(myLayout[0], SIGNAL(triggered()), myMemView, SLOT(compact()));
    connect(myLayout[1], SIGNAL(triggered()), myMemView, SLOT(full()));

    static const char	*theDisplayNames[theDisplayCount] = {
	"&Read/Write",
	"&Thread Id",
	"&Data Type",
	"&Mapped Regions",
	"&Stack Traces",
    };

    myDisplayMenu = menuBar()->addMenu(tr("&Display"));
    myDisplayGroup = createActionGroup(
	    myDisplayMenu, theDisplayNames, myDisplay, theDisplayCount, 0);

    myDisplayMenu->addSeparator();

    myDisplayDimmer = new QAction(tr("&Limit Brightness"), this);
    myDisplayDimmer->setCheckable(true);
    myDisplayMenu->addAction(myDisplayDimmer);

    myDisplayShowToolBar = new QAction(tr("&Show Toolbar"), this);
    myDisplayShowToolBar->setCheckable(true);
    myDisplayMenu->addAction(myDisplayShowToolBar);

    connect(myDisplayGroup, SIGNAL(triggered(QAction *)),
	    myMemView, SLOT(display(QAction *)));

    connect(myDisplayDimmer, SIGNAL(triggered()),
	    myMemView, SLOT(dimmer()));

    connect(myDisplayShowToolBar, SIGNAL(toggled(bool)),
	    this, SLOT(toolbar(bool)));

    // This menu should be ordered the same as the MV_Data* defines in
    // mv_ipc.h
    static const char	*theDataTypeNames[theDataTypeCount] = {
	"&Auto-Detect Types",
	"&32-bit Integer",
	"&64-bit Integer",
	"&32-bit Float",
	"&64-bit Float",
	"&Ascii String",
    };

    myDataTypeMenu = menuBar()->addMenu(tr("&Data Type"));
    myDataTypeGroup = createActionGroup(
	    myDataTypeMenu, theDataTypeNames, myDataType, theDataTypeCount, 0);

    connect(myDataTypeGroup, SIGNAL(triggered(QAction *)),
	    myMemView, SLOT(datatype(QAction *)));


    setWindowTitle("Memview");

    myScrollArea->setViewport(myMemView);
    setCentralWidget(myScrollArea);

    myToolBar = new QToolBar("Tools");
    myToolBar->setAllowedAreas(Qt::TopToolBarArea | Qt::BottomToolBarArea);

    LogSlider *slider = new LogSlider("Batch Size", 15, 15);

    myToolBar->addWidget(slider);

    connect(slider, SIGNAL(valueChanged(int)), myMemView, SLOT(batchSize(int)));

    if (batchsize)
    {
	int value = (int)(log((double)atoi(batchsize))/log(2.0));
	slider->setLogValue(value);
    }
}

Window::~Window()
{
}

QSize
Window::sizeHint() const
{
    return theDefaultSize;
}

void
Window::toolbar(bool value)
{
    if (value)
    {
	addToolBar(Qt::TopToolBarArea, myToolBar);
	myToolBar->show();
    }
    else
	removeToolBar(myToolBar);
}

QActionGroup *
Window::createActionGroup(
	QMenu *menu,
	const char *names[],
	QAction *actions[],
	int count,
	int def_action)
{
    QActionGroup *group = new QActionGroup(this);
    for (int i = 0; i < count; i++)
    {
	actions[i] = new QAction(tr(names[i]), group);
	actions[i]->setCheckable(true);
	menu->addAction(actions[i]);
    }
    actions[def_action]->setChecked(true);
    return group;
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
    , myColorTexture(0)
    , myPixelBuffer(0)
    , myPrevEvents(0)
    , myZoom(0)
    , myDisplayMode(0)
    , myDisplayDimmer(0)
    , myDataType(-1)
    , myStopWatch(false)
    , myPaintInterval(false)
    , myEventTimer(false)
    , myDragging(false)
{
    // Extract the path to the executable
    myPath = argv[0];
    size_t pos = myPath.rfind('/');
    if (pos != std::string::npos)
	myPath.resize(pos+1);
    else
	myPath = "";

    // Skip the program name
    argc -= 1;
    argv += 1;

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
    myStackTrace = new StackTraceMap;
    myStackSelection = 0;
    myMMapMap = new MMapMap;
    myLoader = new Loader(myState, myStackTrace, myMMapMap, myPath);

    if (myLoader->openPipe(argc, argv))
    {
	// Start loading data in a new thread
	myLoader->start();
    }

    myFastTimer = startTimer(30);
    mySlowTimer = startTimer(500);

    myPaintInterval.start();
    myEventTimer.start();
}

MemViewWidget::~MemViewWidget()
{
    delete myLoader;
    delete myState;
    delete myStackTrace;
    delete myMMapMap;
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

void
MemViewWidget::compact()
{
    myDisplay.setCompact(true);
    changeZoom(myZoom);
    update();
}

void
MemViewWidget::full()
{
    myDisplay.setCompact(false);
    changeZoom(myZoom);
    update();
}

void MemViewWidget::display(QAction *action)
{
    myDisplayMode = action->actionGroup()->actions().indexOf(action);
}

void MemViewWidget::dimmer() { myDisplayDimmer = !myDisplayDimmer; }

void MemViewWidget::datatype(QAction *action)
{
    // Subtract 1 so that auto-detect types is -1
    myDataType = action->actionGroup()->actions().indexOf(action) - 1;
}

void MemViewWidget::batchSize(int value)
{
    myLoader->setBlockSize(value);
}

// Load a file into a buffer.  The buffer is owned by the caller, and
// should be freed with delete[].
static char *
loadTextFile(const char *filename, const std::vector<std::string> &paths)
{
    std::ifstream   is;
    for (auto it = paths.begin(); it != paths.end(); ++it)
    {
	std::string path = *it + filename;

	is.open(path.c_str());
	if (is.good())
	    break;
    }
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

// 512 size texture to accomodate the 500 possible threads supported by
// valgrind.
static const int theColorBits = 9;
static const int theColorSize = (1 << theColorBits);

static int
rinverse(int val)
{
    // Radical inverse specialized for 16 bits
    int tmp = val;
    tmp = ((tmp & 0xAAAA) >> 1) | ((tmp & 0x5555) << 1);
    tmp = ((tmp & 0xCCCC) >> 2) | ((tmp & 0x3333) << 2);
    tmp = ((tmp & 0xF0F0) >> 4) | ((tmp & 0x0F0F) << 4);
    tmp = ((tmp & 0xFF00) >> 8) | ((tmp & 0x00FF) << 8);
    return tmp >> (16-theColorBits);
}

static void
fillThreadColors(GLImage<uint32> &colors)
{
    const int width = theColorSize;
    const float s = 0.75;
    const float v = 1;

    Color clr;
    colors.resize(width, 1);
    for (int i = 0; i < width; i++)
    {
	int idx = rinverse(i);
	float h = idx / (float)(width-1);

	clr.fromHSV(h, s, v);
	colors.setPixel(i, 0, clr.toInt32());
    }
}

void
MemViewWidget::initializeGL()
{
    // We're doing our own dithering.  Though having this enabled didn't
    // seem to produce any dithering.
    glDisable(GL_DITHER);

    const GLuint type = GL_NEAREST;

    // Build a texture to store colors used by the shader
    glActiveTexture(GL_TEXTURE1);
    glGenTextures(1, &myColorTexture);
    glBindTexture(GL_TEXTURE_1D, myColorTexture);

    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, type);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, type);

    GLImage<uint32> colors;
    fillThreadColors(colors);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA,
	    colors.width(), 0, GL_RGBA,
	    GL_UNSIGNED_BYTE, colors.data());

    // Create the memory state texture
    glGenBuffers(1, &myPixelBuffer);

    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &myTexture);
    glBindTexture(GL_TEXTURE_RECTANGLE, myTexture);

    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, type);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, type);

    std::vector<std::string> paths;
    paths.push_back("");
    paths.push_back(myPath);
    paths.push_back("/usr/share/memview/");

    QGLShader *vshader = new QGLShader(QGLShader::Vertex, this);
    const char *vsrc = loadTextFile("shader.vert", paths);
    vshader->compileSourceCode(vsrc);
    delete [] vsrc;

    QGLShader *fshader = new QGLShader(QGLShader::Fragment, this);
    const char *fsrc = loadTextFile("shader.frag", paths);
    fshader->compileSourceCode(fsrc);
    delete [] fsrc;

    myProgram = new QGLShaderProgram(this);
    myProgram->addShader(vshader);
    myProgram->addShader(fshader);
    myProgram->link();
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
	w >>= (-zoom) >> 1; w = SYSmax(w, 1);
	h >>= (-zoom) >> 1; h = SYSmax(h, 1);
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
setScrollMax(QScrollBar *scroll, int64 size, bool with_margin = true)
{
    int64 margin = 0;

    // Allow up to half the state to be hidden outside the window
    if (with_margin)
	margin = scroll->pageStep() -
	    (SYSmin(size, (int64)scroll->pageStep()) >> 1);

    int64 nmax = SYSmax(size - scroll->pageStep() + margin, 0ll);

    scroll->setMaximum(SYSclamp32(nmax));
    scroll->setMinimum(SYSclamp32(-margin));
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

    myDisplay.update(*myState, *myMMapMap, width(), myImage.width(), myZoom);
    switch (myDisplayMode)
    {
    case 3:
	myDisplay.fillImage(myImage, IntervalSource<MMapInfo>(
		    *myMMapMap, 0, myZoomState->getIgnoreBits()),
		myHScrollBar->value(),
		myVScrollBar->value());
	break;
    case 4:
	myDisplay.fillImage(myImage, IntervalSource<StackInfo>(
		    *myStackTrace, myStackSelection,
		    myZoomState->getIgnoreBits()),
		myHScrollBar->value(),
		myVScrollBar->value());
	break;
    default:
	myDisplay.fillImage(myImage, StateSource(*myZoomState),
		myHScrollBar->value(),
		myVScrollBar->value());
	break;
    }

#ifdef USE_PBUFFER
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
#endif

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_RECTANGLE, myTexture);

#ifdef USE_PBUFFER
    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_R32UI,
	    myImage.width(), myImage.height(), 0, GL_RED_INTEGER,
	    GL_UNSIGNED_INT, 0 /* offset in PBO */);
#else
    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_R32UI,
	    myImage.width(), myImage.height(), 0, GL_RED_INTEGER,
	    GL_UNSIGNED_INT, myImage.data());
#endif

    // Unbind the buffer - this is required for text rendering to work
    // correctly.
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    myProgram->bind();

    myProgram->setUniformValue("theState", 0);
    myProgram->setUniformValue("theColors", 1);

    myProgram->setUniformValue("theStale", MemoryState::theStale);
    myProgram->setUniformValue("theHalfLife", MemoryState::theHalfLife);
    myProgram->setUniformValue("theDisplayMode", myDisplayMode);
    myProgram->setUniformValue("theDisplayDimmer", myDisplayDimmer);

    myProgram->setUniformValue("theTime", myState->getTime());

    myProgram->setUniformValue("theWindowResX", width());
    myProgram->setUniformValue("theWindowResY", height());

    myProgram->setUniformValue("theDisplayOffX", (int)myHScrollBar->value());
    myProgram->setUniformValue("theDisplayOffY", (int)myVScrollBar->value());
    myProgram->setUniformValue("theDisplayResX", (int)myDisplay.width());
    myProgram->setUniformValue("theDisplayResY", (int)myDisplay.height());

    setScrollMax(myVScrollBar, myDisplay.height());
    setScrollMax(myHScrollBar, myDisplay.width(),
	    myDisplay.getVisualization() != DisplayLayout::LINEAR);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0, 0.0); glVertex3i(-1, -1, -1);
    glTexCoord2f(1.0, 0.0); glVertex3i(1, -1, -1);
    glTexCoord2f(1.0, 1.0); glVertex3i(1, 1, -1);
    glTexCoord2f(0.0, 1.0); glVertex3i(-1, 1, -1);
    glEnd();

    myProgram->release();

    // Render memory contents as text if we're at a sufficient zoom level
    paintText();

    update();
}

static inline bool
isFloatType(int datatype)
{
    switch (datatype)
    {
	case MV_DataFlt32:
	case MV_DataFlt64:
	    return true;
    }
    return false;
}

static inline int
getAlignBits(int datatype)
{
    switch (datatype)
    {
	case MV_DataInt64:
	case MV_DataFlt64:
	    return 3;
    }
    return 2;
}

template <typename FT, typename IT>
static inline FT
intToFloat(IT val)
{
    union {
	FT  fval;
	IT  ival;
    } uval;
    uval.ival = val;
    return uval.fval;
}

// Peek at a 64-bit value in the tracee
static bool
peekData(pid_t pid, uint64 qaddr, uint64 &buf64)
{
    long peekval = ptrace(PTRACE_PEEKDATA,
	    pid, (void *)qaddr, NULL);
    if (peekval == -1)
	return false;

    buf64 = (uint64)peekval;
    if (sizeof(long) == 4)
    {
	// long is a different size on 32-bit platforms...
	peekval = ptrace(PTRACE_PEEKDATA,
		pid, (void *)(qaddr + 4), NULL);
	if (peekval == -1)
	    return false;

	buf64 |= (uint64)peekval << 32;
    }

    return true;
}

struct Text {
    int x;
    int y;
    QString str;
};

void
MemViewWidget::paintText()
{
    const int xmargin = 4;

    int pwidth = width() / myImage.width();
    int pheight = height() / myImage.height();

    QFont	    font;
    QFontMetrics    metrics(font);

    if (pheight < 2*metrics.height())
	return;

    // Use ptrace to stop the process and inspect data
    pid_t pid = myLoader->getChild();
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL))
	return;

    // Wait for the process to stop on a signal
    waitpid(pid, 0, 0);

    std::vector<Text> text_list;

    for (int i = 0; i < myImage.height(); i++)
    {
	for (int j = 0; j < myImage.width(); j++)
	{
	    uint64 qaddr = myDisplay.queryPixelAddress(*myState,
		    myHScrollBar->value() + j,
		    myVScrollBar->value() + i);
	    if (!qaddr)
		continue;

	    // Either use the specified data type or if it's -1, get the
	    // type from the value
	    int	datatype = myDataType;
	    if (datatype < 0)
	    {
		uint64 off;
		auto page = myState->getPage(qaddr, off);

		if (page.exists())
		    datatype = page.state(off).dtype();
		else
		    datatype = MV_DataInt32;
	    }

	    const uint64 min_align_bits = getAlignBits(datatype);
	    const uint64 min_align = 1 << min_align_bits;

	    qaddr <<= myState->getIgnoreBits();

	    if (qaddr & (min_align-1))
		continue;

	    // Get the value.  If the address wasn't mapped, peekData()
	    // will return false.
	    uint64 val;
	    if (!peekData(pid, qaddr, val))
		continue;

	    int x = (j*width())/myImage.width() + xmargin;
	    int y = (i*height())/myImage.height() +
		(pheight + metrics.height())/2;

	    QString str;
	    if (min_align_bits == 2)
	    {
		val &= 0xFFFFFFFF;

		if (datatype == MV_DataChar8)
		{
		    char	cstr[4];

		    bool valid = true;
		    for (int i = 0; i < 4; i++)
		    {
			cstr[i] = (char)((val >> (i*8)) & 0xFF);
			valid &= isascii(cstr[i]);
		    }

		    if (valid)
			str.sprintf("\"%c%c%c%c\"",
				cstr[0],
				cstr[1],
				cstr[2],
				cstr[3]);
		    else
			str.sprintf("%x", (uint32)val);
		}
		else
		{
		    if (isFloatType(datatype))
			str.sprintf("%f", intToFloat<float>((uint32)val));
		    else
			str.sprintf("%x", (uint32)val);
		}
	    }
	    else
	    {
		if (isFloatType(datatype))
		    str.sprintf("%g", intToFloat<double>(val));
		else
		    str.sprintf("%llx", val);
	    }

	    // Shorten the text so that it fits within the desired width.
	    // This will add the "..." if it's too long.
	    str = metrics.elidedText(
		    str, Qt::ElideRight, pwidth - 2*xmargin);

	    text_list.push_back(Text{x, y, str});
	}
    }

    // Detach - this will restart the process
    ptrace(PTRACE_DETACH, pid, NULL, NULL);

    // Render text after restarting the process so that we're only slowing
    // down draw time (not execution)
    for (auto it = text_list.begin(); it != text_list.end(); ++it)
	renderText(it->x, it->y, it->str, font);
}

bool
MemViewWidget::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip)
    {
        QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);

	if (myStackSelection)
	    QToolTip::showText(helpEvent->globalPos(), myStackString.c_str());
	else
	    QToolTip::hideText();

        return true;
    }
    return QWidget::event(event);
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
	myDragRemainder = QPoint(0, 0);
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
	QPoint  dir = myMousePos - event->pos();

	panBy(dir);

	if (myVelocity.size() >= 5)
	    myVelocity.pop();
	myVelocity.push(Velocity(dir.x(), dir.y(), time));
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
minScroll(QScrollBar *scroll, int64 x, int64 size, bool zoomout)
{
    int64 ox = x;
    x += scroll->value();

    if (zoomout)
    {
	x += 1;
	x >>= 1;
    }
    else
    {
	x <<= 1;
	size <<= 1;
    }

    x -= ox;

    setScrollMax(scroll, size);
    scroll->setValue(SYSclamp32(x));
}

static void
magScroll(QScrollBar *scroll, int64 x, int64 size, bool zoomout,
	int64 winsize, int64 psize, int64 nsize)
{
    if (zoomout)
    {
	x = (x*(psize+1)) / winsize;
	x = scroll->value() - x;
    }
    else
    {
	x = (x*(nsize+1)) / winsize;
	x += scroll->value();
    }

    setScrollMax(scroll, size);
    scroll->setValue(SYSclamp32(x));
}

static void
magScrollLinear(QScrollBar *scroll, int64 x,
	int64 winheight, int64 size,
	int64 pwidth, int64 pheight,
	int64 nwidth, int64 nheight)
{
    x = (((x*pheight)/winheight + scroll->value())*pwidth)/nwidth -
	(x*nheight)/winheight;
    size = (size * pwidth) / nwidth;

    setScrollMax(scroll, size);
    scroll->setValue(SYSclamp32(x));
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

    zoom = SYSclamp(zoom, -16, 30);

    if (zoom != myZoom)
    {
	if (zoom > 0)
	{
	    myZoomState = new MemoryState(myState->getIgnoreBits()+zoom);
	    myLoader->setZoomState(myZoomState);
	}
	else
	{
	    myLoader->clearZoomState();
	    myZoomState = myState;
	}

	const bool zoomout = zoom > myZoom;
	QPoint zpos = myMousePos;

	if (zoom < 0 || myZoom < 0)
	{
	    int64 pwidth = myImage.width();
	    int64 pheight = myImage.height();

	    // This makes a GL call
	    resizeImage(zoom);

	    if (!linear)
	    {
		magScroll(myHScrollBar, zpos.x(), myDisplay.width(), zoomout,
			width(), pwidth, myImage.width());
		magScroll(myVScrollBar, zpos.y(), myDisplay.height(), zoomout,
			height(), pheight, myImage.height());
	    }
	    else
	    {
		magScrollLinear(myVScrollBar,
			zpos.y(), height(), myDisplay.height(),
			pwidth, pheight, myImage.width(), myImage.height());
	    }
	}
	else
	{
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
MemViewWidget::timerEvent(QTimerEvent *event)
{
    // Fast timer
    if (event->timerId() == myFastTimer)
    {
	if (!myDragging && myVelocity.size())
	{
	    Velocity   &vel = myVelocity.front();
	    double	time = myStopWatch.lap();
	    QPoint	dir((int)(vel.x * time + 0.5F),
			    (int)(vel.y * time + 0.5F));

	    if (panBy(dir))
	    {
		update();
	    }

	    shortenDrag(vel.x, time);
	    shortenDrag(vel.y, time);
	}
    }
    else if (event->timerId() == mySlowTimer)
    {
	uint64	total_events = myLoader->getTotalEvents();

	myEventInfo.sprintf("%lld events", total_events);

	if (!myLoader->isComplete())
	{
	    double	time = myEventTimer.lap();
	    double	rate = (total_events - myPrevEvents) / time;
	    QString	str;

	    if (rate > 5e8)
		str.sprintf(" (%.1fGev/s)", rate / 1e9);
	    else if (rate > 5e5)
		str.sprintf(" (%.1fMev/s)", rate / 1e6);
	    else if (rate > 5e2)
		str.sprintf(" (%.1fKev/s)", rate / 1e3);
	    else
		str.sprintf(" (%.1fev/s)", rate);

	    myEventInfo.append(str);

	    myPrevEvents = total_events;
	}

    }

    // This frequent status update seems to be fairly costly
    QPoint  pos = zoomPos(myMousePos, myZoom);
    uint64 qaddr = myDisplay.queryPixelAddress(*myZoomState,
	    myHScrollBar->value() + pos.x(),
	    myVScrollBar->value() + pos.y());

    QString	message(myEventInfo);

    myZoomState->appendAddressInfo(message, qaddr, *myMMapMap);

    // Append the zoom level.  Add spaces to right-justify it.
    int		width = myStatusBar->width() - myStatusBar->height();
    QString	zoominfo;

    if (myZoom > 0)
	zoominfo.sprintf("\t\t%.2gx", sqrt(1.0 / (1 << myZoom)));
    else
	zoominfo.sprintf("\t\t%dx", 1 << (-myZoom >> 1));

    int	nspaces = width / myStatusBar->fontMetrics().width(' ');
    nspaces -= message.size();

    zoominfo = zoominfo.rightJustified(nspaces);

    message.append(zoominfo);

    if (message.isEmpty())
	myStatusBar->clearMessage();
    else
	myStatusBar->showMessage(message);

    // Find and stash the closest stack trace
    uint64 off;
    auto page = myZoomState->getPage(qaddr, off);

    if (qaddr && page.exists() && page.state(off).time())
    {
	qaddr <<= myZoomState->getIgnoreBits();

	StackTraceMapReader reader(*myStackTrace);
	auto it = reader.findClosest(qaddr);
	if (it == reader.end())
	{
	    myStackSelection = 0;
	}
	else
	{
	    myStackSelection = it.start();
	    myStackString = it.value().myStr;
	}
    }
    else
    {
	myStackString = "";
	myStackSelection = 0;
    }
}

bool
MemViewWidget::panBy(QPoint dir)
{
    myDragRemainder += dir;

    QPoint  zrem = zoomPos(myDragRemainder, myZoom);

    myDragRemainder.rx() -= (zrem.x()*width())/myImage.width();
    myDragRemainder.ry() -= (zrem.y()*height())/myImage.height();

    myHScrollBar->setValue(myHScrollBar->value() + zrem.x());
    myVScrollBar->setValue(myVScrollBar->value() + zrem.y());

    return zrem.x() || zrem.y();
}

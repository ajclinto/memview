#include "Window.h"
#include "Color.h"
#include "MemoryState.h"
#include <fstream>

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

    myState = new MemoryState;
    myState->openPipe(argc, argv);
    myPrevTime = MemoryState::theStale;

    startTimer(30);

    myPaintInterval.start();
}

MemViewWidget::~MemViewWidget()
{
    delete myState;
}

void
MemViewWidget::linear()
{
    myState->setVisualization(MemoryState::LINEAR);
    update();
}

void
MemViewWidget::block()
{
    myState->setVisualization(MemoryState::BLOCK);
    update();
}

void
MemViewWidget::hilbert()
{
    myState->setVisualization(MemoryState::HILBERT);
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
    glBindTexture(GL_TEXTURE_2D, myTexture);

    const GLuint type = GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, type);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, type);

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
    glLoadIdentity ();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    myImage.setSize(width, height);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, myPixelBuffer);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, myImage.bytes(), 0, GL_STREAM_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void
MemViewWidget::paintGL()
{
#if 0
    StopWatch	timer;
    fprintf(stderr, "interval %f time ", myPaintInterval.lap());
#endif

    int vdelta = myVScrollBar->value() - myAnchor.myAbsoluteOffset;
    int hdelta = myHScrollBar->value() - myAnchor.myColumn;

    // Adjust the position due to scrolling
    myAnchor.myAnchorOffset += vdelta;
    myAnchor.myColumn += hdelta;

    // Set the query mouse position
    myAnchor.myQuery = myMousePos;
    myAnchor.myQueryAddr = 0;

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, myPixelBuffer);

    myImage.setData((uint32 *)
	    glMapBufferARB(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY));
    myState->fillImage(myImage, myAnchor);
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI,
	    myImage.width(), myImage.height(), 0, GL_RED_INTEGER,
	    GL_UNSIGNED_INT, 0 /* offset in PBO */);

    myProgram->setUniformValue("theTime", myState->myTime);

    int nmax = SYSmax(myAnchor.myHeight - myVScrollBar->pageStep(), 0);
    myVScrollBar->setMaximum(nmax);
    myVScrollBar->setValue(myAnchor.myAbsoluteOffset);

    nmax = SYSmax(myAnchor.myWidth - myHScrollBar->pageStep(), 0);
    myHScrollBar->setMaximum(nmax);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0, 0.0); glVertex3i(-1, -1, -1);
    glTexCoord2f(1.0, 0.0); glVertex3i(1, -1, -1);
    glTexCoord2f(1.0, 1.0); glVertex3i(1, 1, -1);
    glTexCoord2f(0.0, 1.0); glVertex3i(-1, 1, -1);
    glEnd();

    // This should really be placed somewhere more appropriate
    QString	message;
    myState->printStatusInfo(message, myAnchor.myQueryAddr);

    if (message.isEmpty())
	myStatusBar->clearMessage();
    else
	myStatusBar->showMessage(message);
}

void
MemViewWidget::resizeEvent(QResizeEvent *event)
{
    if (size().width() != myImage.width() ||
	size().height() != myImage.height())
    {
	int w = size().width();
	int h = size().height();

	myVScrollBar->setPageStep(h);
	myHScrollBar->setPageStep(w);

	QGLWidget::resizeEvent(event);
	update();
    }
}

void
MemViewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
	myStopWatch.start();
	myDragDir = QPoint(0, 0);
	myVelocity[0] = 0;
	myVelocity[1] = 0;
	myDragging = true;
    }
    myMousePos = event->pos();
}

void
MemViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
    {
	double  time = myStopWatch.lap();
	myDragDir = myMousePos - event->pos();
	myHScrollBar->setValue(myHScrollBar->value() + myDragDir.x());
	myVScrollBar->setValue(myVScrollBar->value() + myDragDir.y());
	myVelocity[0] = myDragDir.x() / time;
	myVelocity[1] = myDragDir.y() / time;
    }
    myMousePos = event->pos();
}

void
MemViewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
	myDragging = false;
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
    if (!myDragging)
    {
	double  time = myStopWatch.lap();
	int	drag[2];

	drag[0] = (int)(myVelocity[0] * time + 0.5F);
	drag[1] = (int)(myVelocity[1] * time + 0.5F);

	shortenDrag(myVelocity[0], time);
	shortenDrag(myVelocity[1], time);

	myHScrollBar->setValue(myHScrollBar->value() + drag[0]);
	myVScrollBar->setValue(myVScrollBar->value() + drag[1]);

	if (drag[0] || drag[1] || myState->myTime != myPrevTime)
	{
	    update();

	    myPrevTime = myState->myTime;
	}
    }
    else
    {
	myVelocity[0] = 0;
	myVelocity[1] = 0;

	update();
    }
}


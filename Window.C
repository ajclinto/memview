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

    setStatusBar(new QStatusBar(this));

    myMemView = new MemViewWidget(argc, argv,
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

static void
fillLut(uint32 *lut, const Color &hi, const Color &lo, uint32 size)
{
    const uint32	lcutoff = (int)(0.47*size);
    const uint32	hcutoff = (int)(0.90*size);
    Color		vals[4];

    vals[0] = lo * (0.02/ lo.luminance());
    vals[1] = lo * (0.15 / lo.luminance());
    vals[2] = hi * (0.5 / hi.luminance());
    vals[3] = hi * (2.0 / hi.luminance());

    for (uint32 i = 0; i < size; i++)
    {
	Color	val;
	if (i >= hcutoff)
	    val = vals[2].lerp(vals[3], (i-hcutoff)/(float)(size-1-hcutoff));
	else if (i >= lcutoff)
	    val = vals[1].lerp(vals[2], (i-lcutoff)/(float)(hcutoff-lcutoff));
	else
	    val = vals[0].lerp(vals[1], i/(float)lcutoff);
	lut[i] = val.toInt32();
    }
}

MemViewWidget::MemViewWidget(int argc, char *argv[],
	QScrollBar *vscrollbar,
	QScrollBar *hscrollbar,
	QStatusBar *status)
    : myVScrollBar(vscrollbar)
    , myHScrollBar(hscrollbar)
    , myStatusBar(status)
    , myTexture(0)
    , myList(0)
    , myPixelBuffer(0)
    , myStopWatch(false)
    , myDragging(false)
{
    // We need mouse events even when no buttons are held down, for status
    // bar updates
    setMouseTracking(true);

    // Use a fixed-width font for the status bar
    QFont	font("Monospace");
    font.setStyleHint(QFont::TypeWriter);
    myStatusBar->setFont(font);

    // Create color lookup textures
    const Color clrs[4][2] = {
	{Color(0.2, 1.0, 0.2), Color(0.1, 0.1, 0.5)}, // Read
	{Color(1.0, 0.7, 0.2), Color(0.3, 0.1, 0.1)}, // Write
	{Color(0.3, 0.2, 0.8), Color(0.3, 0.1, 0.4)}, // Instr
	{Color(0.3, 0.3, 0.3), Color(0.1, 0.1, 0.1)}  // Alloc
    };

    for (int i = 0; i < theLutCount; i++)
	fillLut(myLut[i], clrs[i][0], clrs[i][1], theLutSize);

    myTimer = new QTimer;
    connect(myTimer, SIGNAL(timeout()), this, SLOT(tick()));

    myState = new MemoryState;
    myState->openPipe(argc, argv);

    myTimer->start(30);
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
    glGenTextures(theLutCount, myLutTexture);
    for (int i = 0; i < theLutCount; i++)
    {
	glActiveTexture(GL_TEXTURE0+i+1);
	glBindTexture(GL_TEXTURE_1D, myLutTexture[i]);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8,
		theLutSize, 0, GL_BGRA,
		GL_UNSIGNED_BYTE, myLut[i]);
    }

    glActiveTexture(GL_TEXTURE0);
    glGenBuffers(1, &myPixelBuffer);

    glGenTextures(1, &myTexture);
    glBindTexture(GL_TEXTURE_2D, myTexture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    myList = glGenLists(1);
    glNewList(myList, GL_COMPILE);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0, 0.0); glVertex3i(-1, -1, -1);
    glTexCoord2f(1.0, 0.0); glVertex3i(1, -1, -1);
    glTexCoord2f(1.0, 1.0); glVertex3i(1, 1, -1);
    glTexCoord2f(0.0, 1.0); glVertex3i(-1, 1, -1);
    glEnd();

    glEndList();

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
    myProgram->setUniformValue("theRLut", 1);
    myProgram->setUniformValue("theWLut", 2);
    myProgram->setUniformValue("theILut", 3);
    myProgram->setUniformValue("theALut", 4);
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
    //StopWatch	timer;

    // Adjust the position due to scrolling
    myAnchor.myAnchorOffset += myVScrollBar->value() -
	myAnchor.myAbsoluteOffset;
    myAnchor.myColumn += myHScrollBar->value() - myAnchor.myColumn;

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

    glCallList(myList);

    int nmax = SYSmax(myAnchor.myHeight - myVScrollBar->pageStep(), 0);
    myVScrollBar->setMaximum(nmax);
    myVScrollBar->setValue(myAnchor.myAbsoluteOffset);

    nmax = SYSmax(myAnchor.myWidth - myHScrollBar->pageStep(), 0);
    myHScrollBar->setMaximum(nmax);

    QString	message;
    myState->printStatusInfo(message, myAnchor.myQueryAddr);
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
MemViewWidget::tick()
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
    }
    else
    {
	myVelocity[0] = 0;
	myVelocity[1] = 0;
    }

    update();
}


#include "Window.h"
#include "MemoryState.h"

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

    myMemView = new MemViewWidget(argc, argv,
	    myScrollArea->verticalScrollBar(),
	    myScrollArea->horizontalScrollBar());

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
	QScrollBar *vscrollbar, QScrollBar *hscrollbar)
    : myVScrollBar(vscrollbar)
    , myHScrollBar(hscrollbar)
    , myTexture(0)
    , myList(0)
    , myStopWatch(false)
    , myDragging(false)
{
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

void
MemViewWidget::initializeGL()
{
#if defined(USE_SHADERS)
    glGenTextures(1, &myTexture);
    glBindTexture(GL_TEXTURE_2D, myTexture);

    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

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
    const char *vsrc =
        "varying mediump vec2 texc;\n"
        "void main(void)\n"
        "{\n"
        "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
        "    texc = vec2(gl_MultiTexCoord0);\n"
        "}\n";
    vshader->compileSourceCode(vsrc);

    QGLShader *fshader = new QGLShader(QGLShader::Fragment, this);
    const char *fsrc =
        "uniform sampler2D texture;\n"
        "varying mediump vec2 texc;\n"
        "void main(void)\n"
        "{\n"
        "    gl_FragColor = texture2D(texture, texc);\n"
        //"    gl_FragColor = vec4(texc.x, texc.y, 0, 1);\n"
        "}\n";
    fshader->compileSourceCode(fsrc);

    myProgram = new QGLShaderProgram(this);
    myProgram->addShader(vshader);
    myProgram->addShader(fshader);
    myProgram->link();

    myProgram->bind();

    myProgram->setUniformValue("texture", 0);
#endif
}

void
MemViewWidget::resizeGL(int width, int height)
{
    glViewport(0, 0, (GLint)width, (GLint)height);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity ();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
}

void
MemViewWidget::paintGL()
{
    //StopWatch	timer;

    // Adjust the position due to scrolling
    myAnchor.myAnchorOffset += myVScrollBar->value() -
	myAnchor.myAbsoluteOffset;
    myAnchor.myColumn += myHScrollBar->value() - myAnchor.myColumn;

    myState->fillImage(myImage, myAnchor);

#if !defined(USE_SHADERS)
    glDrawPixels(myImage.width(), myImage.height(), GL_BGRA,
	    GL_UNSIGNED_BYTE, myImage.data());
#else
    glTexImage2D(GL_TEXTURE_2D, 0, 3,
	    myImage.width(), myImage.height(), 0, GL_BGRA,
	    GL_UNSIGNED_BYTE, myImage.data());

    glCallList(myList);
#endif

    int nmax = SYSmax(myAnchor.myHeight - myVScrollBar->pageStep(), 0);
    myVScrollBar->setMaximum(nmax);
    myVScrollBar->setValue(myAnchor.myAbsoluteOffset);

    nmax = SYSmax(myAnchor.myWidth - myHScrollBar->pageStep(), 0);
    myHScrollBar->setMaximum(nmax);
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
	myImage.resize(w, h);
	update();

	QGLWidget::resizeEvent(event);
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
	myDragPos = event->pos();
	myDragging = true;
    }
}

void
MemViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
    {
	double  time = myStopWatch.lap();
	myDragDir = myDragPos - event->pos();
	myHScrollBar->setValue(myHScrollBar->value() + myDragDir.x());
	myVScrollBar->setValue(myVScrollBar->value() + myDragDir.y());
	myVelocity[0] = myDragDir.x() / time;
	myVelocity[1] = myDragDir.y() / time;
	myDragPos = event->pos();
    }
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


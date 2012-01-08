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
MemViewWidget::paintGL()
{
    //StopWatch	timer;

    // Adjust the position due to scrolling
    myAnchor.myAnchorOffset += myVScrollBar->value() -
	myAnchor.myAbsoluteOffset;
    myAnchor.myColumn += myHScrollBar->value() - myAnchor.myColumn;

    myState->fillImage(myImage, myAnchor);

    glDrawPixels(myImage.width(), myImage.height(), GL_BGRA,
	    GL_UNSIGNED_BYTE, myImage.data());

    int nmax = SYSmax(myAnchor.myHeight - myVScrollBar->pageStep(), 0);
    myVScrollBar->setMaximum(nmax);
    myVScrollBar->setValue(myAnchor.myAbsoluteOffset);

    nmax = SYSmax(myAnchor.myWidth - myHScrollBar->pageStep(), 0);
    myHScrollBar->setMaximum(nmax);
}

void
MemViewWidget::resizeEvent(QResizeEvent *)
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
    }
}

void
MemViewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
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
	myDragDir = myDragPos - event->pos();
	myHScrollBar->setValue(myHScrollBar->value() + myDragDir.x());
	myVScrollBar->setValue(myVScrollBar->value() + myDragDir.y());
	myVelocity[0] += myDragDir.x();
	myVelocity[1] += myDragDir.y();
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

#if 0
double
shortenDrag(double val, double delta)
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
    return val;
}
#endif

void
MemViewWidget::tick()
{
    if (!myDragging)
    {
	myHScrollBar->setValue(myHScrollBar->value() + myDragDir.x());
	myVScrollBar->setValue(myVScrollBar->value() + myDragDir.y());
    }
    else
    {
	myVelocity[0] = 0;
	myVelocity[1] = 0;
    }
#if 0
    double  time = myStopWatch.lap();

    // Account for the drag direction
    myHScrollBar->setValue(myHScrollBar->value() + myDragDir.x());
    myVScrollBar->setValue(myVScrollBar->value() + myDragDir.y());

    myDragDir[0] = shortenDrag(myDragDir[0], time);
    myDragDir[1] = shortenDrag(myDragDir[1], time);
#endif

    update();
}


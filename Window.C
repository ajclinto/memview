#include "Window.h"
#include "MemoryState.h"
#include "StopWatch.h"

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
    myVis[1]->setChecked(true);

    myScrollArea = new MemViewScroll(this);

    myMemView = new MemViewWidget(argc, argv,
	    myScrollArea->verticalScrollBar());

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

MemViewWidget::MemViewWidget(int argc, char *argv[], QScrollBar *scrollbar)
    : myScrollBar(scrollbar)
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
    StopWatch	timer;

    // Adjust the position due to scrolling
    myAnchor.myAnchorOffset += myScrollBar->value() -
	myAnchor.myAbsoluteOffset;

    myState->fillImage(myImage, myAnchor);

    glDrawPixels(myImage.width(), myImage.height(), GL_BGRA,
	    GL_UNSIGNED_BYTE, myImage.data());

    int nmax = SYSmax(myAnchor.myHeight - myScrollBar->pageStep(), 0);
    myScrollBar->setMaximum(nmax);
    myScrollBar->setValue(myAnchor.myAbsoluteOffset);
}

void
MemViewWidget::resizeEvent(QResizeEvent *)
{
    if (size().width() != myImage.width() ||
	size().height() != myImage.height())
    {
	int w = size().width();
	int h = size().height();

	myScrollBar->setPageStep(h);
	myImage.resize(w, h);
	update();
    }
}

void
MemViewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
	myDragPos = event->pos();
}

void
MemViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
    {
	myScrollBar->setValue(myScrollBar->value() +
		myDragPos.y() - event->pos().y());
	myDragPos = event->pos();
    }
}

void
MemViewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
	myScrollBar->setValue(myScrollBar->value() +
		event->pos().y() - myDragPos.y());
}

void
MemViewWidget::tick()
{
    update();
}


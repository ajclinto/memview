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
	"&Recursive Block"
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

    myMemView = new MemViewWidget(argc, argv);
    setCentralWidget(myMemView);

    connect(myVis[0], SIGNAL(triggered()), myMemView, SLOT(linear()));
    connect(myVis[1], SIGNAL(triggered()), myMemView, SLOT(block()));

    setWindowTitle("Memview");

    resize(theDefaultSize);
}

Window::~Window()
{
}

//
// MemViewWidget
//

MemViewWidget::MemViewWidget(int argc, char *argv[])
    : myImage(theDefaultSize, QImage::Format_ARGB32_Premultiplied)
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
MemViewWidget::paintEvent(QPaintEvent *)
{
    QPainter	painter(this);

    myState->fillImage(myImage);

    painter.drawPixmap(QPoint(), QPixmap::fromImage(myImage));
}

void
MemViewWidget::resizeEvent(QResizeEvent *)
{
    if (size() != myImage.size())
    {
	myImage = QImage(size(), QImage::Format_ARGB32_Premultiplied);
	update();
    }
}

void
MemViewWidget::tick()
{
    // If we failed to load from the pipe, the proces has terminated - so
    // there's no need to continue providing real time updates.
    if (!myState->loadFromPipe(10000000))
	myTimer->stop();

    update();
}


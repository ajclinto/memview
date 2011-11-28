#include "Window.h"
#include "MemoryState.h"

static const QSize	theDefaultSize(800, 600);

Window::Window()
{
    myQuit = new QAction(tr("&Quit"), this);

    connect(myQuit, SIGNAL(triggered()), qApp, SLOT(quit()));

    myFileMenu = menuBar()->addMenu(tr("&File"));
    myFileMenu->addSeparator();
    myFileMenu->addAction(myQuit);

    myMemView = new MemViewWidget;
    setCentralWidget(myMemView);

    setWindowTitle("Memview");

    resize(theDefaultSize);
}

Window::~Window()
{
}

void
Window::quit()
{
    QMessageBox	message;
    message.setWindowTitle(tr("Window"));
    message.setText(tr("Do you really want to quit?"));
    message.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    message.setDefaultButton(QMessageBox::No);
    if (message.exec() == QMessageBox::Yes)
	qApp->quit();
}

//
// MemViewWidget
//

MemViewWidget::MemViewWidget()
    : myImage(theDefaultSize, QImage::Format_ARGB32_Premultiplied)
{
    myTimer = new QTimer;
    connect(myTimer, SIGNAL(timeout()), this, SLOT(tick()));

    myState = new MemoryState;
    myState->openPipe("/home/andrew/projects/sorts/shell/shell");

    myTimer->start(30);
}

MemViewWidget::~MemViewWidget()
{
    delete myState;
}

void
MemViewWidget::paintEvent(QPaintEvent *)
{
    QPainter	painter(this);

    // If we failed to load from the pipe, the proces has terminated - so
    // there's no need to continue providing real time updates.
    if (!myState->loadFromPipe(10000))
	myTimer->stop();

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
    update();
}


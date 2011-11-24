#include "Window.h"
#include "MemoryState.h"

static const QSize	theImageSize(400, 400);

Window::Window()
    : myImage(theImageSize, QImage::Format_ARGB32_Premultiplied)
{
    myQuit = new QAction(tr("&Quit"), this);

    connect(myQuit, SIGNAL(triggered()), qApp, SLOT(quit()));

    myFileMenu = menuBar()->addMenu(tr("&File"));
    myFileMenu->addSeparator();
    myFileMenu->addAction(myQuit);

    myTimer = new QTimer;
    connect(myTimer, SIGNAL(timeout()), this, SLOT(tick()));

    myLabel = new QLabel(tr("Image"));
    myLabel->setPixmap(QPixmap::fromImage(myImage));

    setCentralWidget(myLabel);

    setWindowTitle("Bitmap");

    myState = new MemoryState;
    myState->openPipe("/home/andrew/projects/sorts/shell/shell");

    myTimer->start(100);
}

Window::~Window()
{
    delete myState;
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

void
Window::tick()
{
    if (myState->loadFromPipe(10000))
    {
	myState->fillImage(myImage);
	myLabel->setPixmap(QPixmap::fromImage(myImage));
    }
}


#include "Window.h"

int main(int argc, char *argv[])
{
    QApplication	app(argc, argv);

    if (argc <= 1)
    {
	fprintf(stderr, "Usage: memview [--ignore-bits=n] [your-program] [your-program-options]\n");
	return 1;
    }

    // Skip the program name
    Window	window(argc-1, argv+1);

    window.show();
    return app.exec();
}

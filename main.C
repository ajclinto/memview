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

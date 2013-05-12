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

static void
usage()
{
    fprintf(stderr, "Usage: memview [--ignore-bits=n] [valgrind-options] your-program [your-program-options]\n");
    fprintf(stderr, "\t--ignore-bits=n\n"
	"\t\tDrop the n least significant bits in memory addresses.\n"
	"\t\tThis option can be used to optimize memory use. [2]\n");
    fprintf(stderr, "\t--batch-size=n\n"
	"\t\tTake a stack trace sample after every n events.\n"
	"\t\tThis value must be between 1 and 32768. [32768]\n");
    fprintf(stderr, "\t--tool=[memview|lackey]\n"
	"\t\tBy default, memview will use the 'memview' valgrind\n"
	"\t\ttool.  If you have an unpatched valgrind, you can force the\n"
	"\t\tuse of 'lackey' with this option - however performance will be\n"
	"\t\tpoor.  Stack traces and memory allocations are unsupported\n"
	"\t\twith lackey.\n");
}

int main(int argc, char *argv[])
{
    int		  myargc = argc;
    char	**myargv = (char **)alloca(argc*sizeof(char *));

    // Create a copy of the command-line arguments since qt may strip out
    // arguments that are also used by the guest program.
    for (int i = 0; i < argc; i++)
	myargv[i] = argv[i];

    QApplication	app(argc, argv);

    if (myargc <= 1)
    {
	usage();
	return 1;
    }

    Window	window(myargc, myargv);

    window.show();
    return app.exec();
}

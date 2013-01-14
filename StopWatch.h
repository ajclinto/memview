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

#ifndef StopWatch_H
#define StopWatch_H

#include <stdio.h>
#include <time.h>

class StopWatch {
public:
     StopWatch(bool print = true) : myPrint(print) { start(); }
    ~StopWatch()
    {
	if (myPrint)
	    fprintf(stderr, "%f\n", lap());
    }

    void start()
    {
	myStart = myLap = time();
    }
    double lap()
    {
	double	cur = time();
	double	val = cur - myLap;
	myLap = cur;
	return val;
    }
    double elapsed() const
    {
	return time()-myStart;
    }

private:
    double time() const
    {
	timespec	ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec + (1e-9 * (double)ts.tv_nsec);
    }

private:
    double	myStart;
    double	myLap;
    bool	myPrint;
};

#endif

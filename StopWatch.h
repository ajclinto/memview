#ifndef StopWatch_H
#define StopWatch_H

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
    double stop()
    {
	return time()-myStart;
    }

private:
    double time()
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

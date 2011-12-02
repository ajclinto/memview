#ifndef StopWatch_H

class StopWatch {
public:
     StopWatch() { start(); }
    ~StopWatch() { fprintf(stderr, "%f\n", lap()); }

    void start()
    {
	myStart = time();
    }
    double lap()
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
};

#endif

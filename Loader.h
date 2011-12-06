#ifndef Loader_H
#define Loader_H

#include <QtGui>
#include <sys/types.h>
#include <signal.h>
#include "Math.h"

class MemoryState;

class Loader : public QThread {
public:
     Loader(MemoryState *state);
    ~Loader();

    bool	openPipe(int argc, char *argv[]);

protected:
    void	run();

private:
    bool	loadFromLackey(int max_read);
    bool	loadFromTrace();

private:
    MemoryState	*myState;

    // Child process
    pid_t	 myChild;
    FILE	*myPipe;

    bool	 myBinary;
    bool	 myAbort;
};

#endif

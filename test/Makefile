# Copied from the compile line in the parent directory
CXXFLAGS = -g -O3 -std=c++0x -Wall -W -D_REENTRANT -DQT_WEBKIT -DQT_NO_DEBUG -DQT_CORE_LIB -I/usr/share/qt4/mkspecs/linux-g++-64 -I. -I/usr/include/qt4/QtCore -I/usr/include/qt4

LDFLAGS = -lQtCore

top: interval array

interval: interval.C ../IntervalMap.h
	g++ $(CXXFLAGS) $(@).C -o $@ $(LDFLAGS)

array: array.C ../SparseArray.h
	g++ $(CXXFLAGS) $(@).C -o $@ $(LDFLAGS)

clean:
	rm -f interval

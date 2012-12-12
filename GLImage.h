#ifndef GLIMAGE_H
#define GLIMAGE_H

#include <stdlib.h>
#include <string.h>

template <typename T>
class GLImage {
public:
    GLImage()
	: myData(0)
	, myWidth(0)
	, myHeight(0)
	, myOwnData(false) {}
    ~GLImage()
    {
	if (myOwnData)
	    delete [] myData;
    }

    void resize(int width, int height)
    {
	if (myWidth != width ||
	    myHeight != height)
	{
	    if (myOwnData)
	    {
		delete [] myData;
		myData = 0;
	    }

	    myWidth = width;
	    myHeight = height;

	    if (myWidth && myHeight)
	    {
		myData = new T[myWidth*myHeight];
		myOwnData = true;
	    }
	}
    }

    // Interface to allow external ownership
    void setSize(int width, int height)
    {
	myWidth = width;
	myHeight = height;
	myOwnData = false;
    }
    void setData(T *data)
    {
	myData = data;
	myOwnData = false;
    }

    int width() const	{ return myWidth; }
    int height() const	{ return myHeight; }
    size_t  bytes() const   { return myWidth*myHeight*sizeof(T); }
    const T *data() const	{ return myData; }

    void fill(T val)
    {
	for (int i = 0; i < myWidth*myHeight; i++)
	    myData[i] = val;
    }
    void zero()
    {
	memset(myData, 0, myWidth*myHeight*sizeof(T));
    }
    void setPixel(int x, int y, T val)
    {
	myData[(myHeight-y-1)*myWidth+x] = val;
    }
    T *getScanline(int y)
    {
	return &myData[(myHeight-y-1)*myWidth];
    }

private:
    T		*myData;
    int		 myWidth;
    int		 myHeight;
    bool	 myOwnData;
};

#endif

#ifndef GLIMAGE_H
#define GLIMAGE_H

class GLImage {
public:
    GLImage()
	: myData(0)
	, myWidth(0)
	, myHeight(0) {}

    void resize(int width, int height)
    {
	if (myWidth != width ||
	    myHeight != height)
	{
	    delete [] myData;
	    myData = 0;
	    myWidth = width;
	    myHeight = height;
	}

	if (!myData)
	    myData = new uint32[myWidth*myHeight];
    }

    int width() const	{ return myWidth; }
    int height() const	{ return myHeight; }
    const uint32 *data() const	{ return myData; }

    void fill(uint32 val)
    {
	for (int i = 0; i < myWidth*myHeight; i++)
	    myData[i] = val;
    }
    void setPixel(int r, int c, uint32 val)
    {
	myData[(myHeight-r-1)*myWidth+c] = val;
    }

private:
    uint32	*myData;
    int		 myWidth;
    int		 myHeight;
};

#endif

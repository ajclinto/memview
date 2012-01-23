#ifndef GLIMAGE_H
#define GLIMAGE_H

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
		myData = new uint32[myWidth*myHeight];
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
    void setData(uint32 *data)
    {
	myData = data;
	myOwnData = false;
    }

    int width() const	{ return myWidth; }
    int height() const	{ return myHeight; }
    size_t  bytes() const   { return myWidth*myHeight*sizeof(uint32); }
    const uint32 *data() const	{ return myData; }

    void fill(uint32 val)
    {
	for (int i = 0; i < myWidth*myHeight; i++)
	    myData[i] = val;
    }
    void invert()
    {
	for (int i = 0; i < myWidth*myHeight; i++)
	    myData[i] = ~myData[i] | 0xFF000000;
    }
    void setPixel(int r, int c, uint32 val)
    {
	myData[(myHeight-r-1)*myWidth+c] = val;
    }

private:
    uint32	*myData;
    int		 myWidth;
    int		 myHeight;
    bool	 myOwnData;
};

#endif

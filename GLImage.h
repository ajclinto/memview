#ifndef GLIMAGE_H
#define GLIMAGE_H

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
    void invert()
    {
	for (int i = 0; i < myWidth*myHeight; i++)
	    myData[i] = ~myData[i] | 0xFF000000;
    }
    void setPixel(int r, int c, T val)
    {
	myData[(myHeight-r-1)*myWidth+c] = val;
    }

private:
    T	*myData;
    int		 myWidth;
    int		 myHeight;
    bool	 myOwnData;
};

#endif

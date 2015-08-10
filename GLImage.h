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

    int width() const       { return myWidth; }
    int height() const      { return myHeight; }
    size_t  bytes() const   { return myWidth*myHeight*sizeof(T); }
    const T *data() const   { return myData; }

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
    T           *myData;
    int          myWidth;
    int          myHeight;
    bool         myOwnData;
};

#endif

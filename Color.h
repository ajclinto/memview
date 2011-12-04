#ifndef COLOR_H

class Color {
public:
    Color() {}
    Color(float r, float g, float b)
	: myR(r), myG(g), myB(b) {}

    uint32	toInt32() const
		{
		    return 0xFF000000 |
			(ftoc(myB)) |
			(ftoc(myG) << 8) |
			(ftoc(myR) << 16);
		}

    float	luminance() const
		{
		    return 0.3*myR + 0.6*myG + 0.1*myB;
		}

    Color	operator+(const Color &rhs) const
		{
		    return Color(
			    myR+rhs.myR,
			    myG+rhs.myG,
			    myB+rhs.myB);
		}
    Color	operator*(float scale) const
		{
		    return Color(myR*scale, myG*scale, myB*scale);
		}
    Color	lerp(const Color &rhs, float bias) const
		{
		    return Color(
			    SYSlerp(myR, rhs.myR, bias),
			    SYSlerp(myG, rhs.myG, bias),
			    SYSlerp(myB, rhs.myB, bias));
		}

private:
    static uint32 ftoc(float v)
		{
		    v *= 255.0F;
		    return SYSclamp((int)v, 0, 255);
		}

private:
    float	myR;
    float	myG;
    float	myB;
};

#endif

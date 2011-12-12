#ifndef Color_H
#define Color_H

#include <QColor>

class Color {
public:
    Color() {}
    Color(float r, float g, float b)
	: myR(r), myG(g), myB(b) {}
    Color(uint32 val)
    {
	myB = (val & 0xFF) / 255.0F; val >>= 8;
	myG = (val & 0xFF) / 255.0F; val >>= 8;
	myR = (val & 0xFF) / 255.0F;
    }

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
#if 0
		    return Color(
			    SYSlerp(myR, rhs.myR, bias),
			    SYSlerp(myG, rhs.myG, bias),
			    SYSlerp(myB, rhs.myB, bias));
#else
#if 0
		    QColor  c1(QColor(toInt32()).toHsl());
		    QColor  c2(QColor(rhs.toInt32()).toHsl());

		    // Lerp in HSL space
		    float   h = lerpHue((float)c1.hslHueF(),
					(float)c2.hslHueF(), bias);
		    float   s = SYSlerp((float)c1.hslSaturationF(),
					(float)c2.hslSaturationF(), bias);
		    float   l = SYSlerp((float)c1.lightnessF(),
					(float)c2.lightnessF(), bias);

		    QColor  c3(QColor::fromHslF(h, s, l));
#else
		    QColor  c1(QColor(toInt32()).toHsv());
		    QColor  c2(QColor(rhs.toInt32()).toHsv());

		    // Lerp in HSV space
		    float   h = lerpHue((float)c1.hsvHueF(),
					(float)c2.hsvHueF(), bias);
		    float   s = SYSlerp((float)c1.hsvSaturationF(),
					(float)c2.hsvSaturationF(), bias);
		    float   l = SYSlerp((float)c1.lightnessF(),
					(float)c2.lightnessF(), bias);

		    QColor  c3(QColor::fromHsvF(h, s, l));
#endif

		    return Color(c3.redF(), c3.greenF(), c3.blueF());
#endif
		}

private:
    static uint32 ftoc(float v)
		{
		    v *= 255.0F;
		    return SYSclamp((int)v, 0, 255);
		}

    static float    lerpHue(float h1, float h2, float bias)
		    {
			h1 = SYSclamp(h1, 0.0F, 1.0F);
			h2 = SYSclamp(h2, 0.0F, 1.0F);
			if (h2 > h1 + 0.5F)
			    h1 += 1;
			else if (h1 > h2 + 0.5F)
			    h2 += 1;

			float   h = SYSlerp(h1, h2, bias);
			if (h > 1) h -= 1;
			return h;
		    }


private:
    float	myR;
    float	myG;
    float	myB;
};

#endif

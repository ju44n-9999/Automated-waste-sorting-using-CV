#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// All channels returned as RGB uint8. Same white letterbox and half-pixel rule as Python.
inline uint8_t waste_pixel(const uint8_t *rgb, int width, int height, int size,
                          int x, int y, int channel, int mode) {
    if (mode == 0) {
        int side = std::min(width, height);
        int sx = (width-side)/2+x*side/size, sy = (height-side)/2+y*side/size;
        return rgb[(sy*width+sx)*3+channel];
    }
    float scale = float(size)/std::max(width, height);
    int nw = std::max(1, int(std::floor(width*scale+0.5f)));
    int nh = std::max(1, int(std::floor(height*scale+0.5f)));
    int ox = (size-nw)/2, oy = (size-nh)/2;
    if (x<ox || y<oy || x>=ox+nw || y>=oy+nh) return 255;
    float sx = std::clamp((x-ox+0.5f)*width/nw-0.5f, 0.0f, float(width-1));
    float sy = std::clamp((y-oy+0.5f)*height/nh-0.5f, 0.0f, float(height-1));
    int x0=int(sx), y0=int(sy), x1=std::min(x0+1,width-1), y1=std::min(y0+1,height-1);
    float wx=sx-x0, wy=sy-y0;
    float top=rgb[(y0*width+x0)*3+channel]*(1-wx)+rgb[(y0*width+x1)*3+channel]*wx;
    float bottom=rgb[(y1*width+x0)*3+channel]*(1-wx)+rgb[(y1*width+x1)*3+channel]*wx;
    return uint8_t(std::clamp(int(std::floor(top*(1-wy)+bottom*wy+0.5f)),0,255));
}

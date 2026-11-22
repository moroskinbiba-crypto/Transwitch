#pragma once
#include <cstddef>
namespace desknx::ui {
template <typename Emit>
inline std::size_t forEachDottedCellRect(int x,int y,int width,int height,Emit&& emit) {
    if(width<=0||height<=0)return 0;
    std::size_t count=0;
    for(int xx=x;xx<x+width;xx+=12) {
        emit(xx,y,6,2);emit(xx,y+height-2,6,2);count+=2;
    }
    for(int yy=y;yy<y+height;yy+=12) {
        emit(x,yy,2,6);emit(x+width-2,yy,2,6);count+=2;
    }
    return count;
}
} // namespace desknx::ui

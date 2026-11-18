#pragma once
#include <algorithm>
namespace desknx::library_grid {
constexpr int Columns=3, VisibleRows=3, VisibleItems=Columns*VisibleRows;
inline int clampIndex(int index,int count){return count<=0?0:std::max(0,std::min(index,count-1));}
inline int move(int index,int count,int dx,int dy){
 if(count<=0)return 0; index=clampIndex(index,count);
 const int row=index/Columns,column=index%Columns;
 const int nextColumn=std::max(0,std::min(Columns-1,column+dx));
 return clampIndex(row*Columns+nextColumn+dy*Columns,count);
}
inline int offsetForSelection(int index,int count){
 if(count<=VisibleItems)return 0;
 const int row=clampIndex(index,count)/Columns;
 const int maxFirstRow=std::max(0,(count-1)/Columns-(VisibleRows-1));
 return std::min(std::max(0,row-(VisibleRows-1)),maxFirstRow)*Columns;
}
inline int indexAt(int offset,int row,int column,int count){
 if(row<0||row>=VisibleRows||column<0||column>=Columns)return -1;
 const int index=offset+row*Columns+column;return index>=0&&index<count?index:-1;
}
} // namespace desknx::library_grid

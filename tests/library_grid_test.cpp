#include "../include/desknx/LibraryGrid.hpp"
#include <cassert>
int main(){
 using namespace desknx::library_grid;
 assert(VisibleItems==9);
 assert(move(0,20,-1,0)==0); assert(move(2,20,1,0)==2);
 assert(move(2,20,-1,0)==1); assert(move(1,20,0,1)==4);
 assert(move(1,20,0,-1)==0); assert(move(19,20,0,1)==19);
 assert(offsetForSelection(0,30)==0); assert(offsetForSelection(8,30)==0);
 assert(offsetForSelection(9,30)==3); assert(offsetForSelection(17,30)==9);
 assert(offsetForSelection(29,30)==21); assert(offsetForSelection(9,10)==3);
 assert(indexAt(3,0,0,10)==3); assert(indexAt(3,2,2,10)==-1);
 assert(indexAt(0,2,2,9)==8); return 0;
}

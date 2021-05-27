#include <stdio.h>

#include "libsmoothie.h"

int main(){
    
    smoothie_create("D:\\VXAPPS\\Battle Axe.vxapp\\content\\smoothie.map", "C:\\vxtmp\\11111111", "C:\\vxsave\\11111111");

    printf("Press Any Key to Continue\n");  
    getchar();    
    
    smoothie_destroy("C:\\vxtmp\\11111111");

    return 0;
}
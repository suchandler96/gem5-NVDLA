#include <stdio.h>
#include <stdlib.h>

#ifdef ENABLE_PARSEC_HOOKS
#include "hooks.h"

#endif
//#include "m5ops.h" inc ase to not use parsec hooks

//#define ARRAYSIZE 16000000
#define CACHESIZE 64*1024*8

void subtract_arrays(int *restrict a, int *restrict b,
                     int *restrict c, int size)
{

    for (int i = 0; i < size; i+=1)
    {
        a[i] = b[i] - c[i];
    }
}

int main(int argc, char *argv[])
{
    int size = 0;
    char *a,*b;
    size = atoi(argv[1]);
    a = aligned_alloc(sizeof(int)*16,sizeof(int)*size);
    b = aligned_alloc(sizeof(int)*16,sizeof(int)*size);
    // Create more misses
    //char *ptr = (char *)a;
    /*for (int i = 0; i < size; i+=1)
    {
        ptr[i] = i%128;
        b[i] = i;
    }*/

#ifdef ENABLE_PARSEC_HOOKS
    __parsec_roi_begin();
#endif

    #ifdef ENABLE_PARSEC_HOOKS
    __start_accel(a,size,a);
    #endif

     //m5_start_accel(a,size,a);

    for (int i = 0; i < size; i+=1)
    {
        b[i] = i;
    }

    #ifdef ENABLE_PARSEC_HOOKS
    __wait_accel(a,size);
    #endif



#ifdef ENABLE_PARSEC_HOOKS
    __parsec_roi_end();
#endif

    return 0;
}

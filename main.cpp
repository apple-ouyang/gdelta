//
// Created by lenovo on 2019/12/10.
//

#include <cstdio>
#include <cstdlib>
#include "gdelta.h"
#include <fcntl.h>
#include <unistd.h>
#include "cstring"
#include <stdint.h>
#define  MBSIZE 1024*1024

char * InputFile= (char*)"/home/thl/data/chunk/3177803";
char * BaseFile = (char*)"/home/thl/data/chunk/3180462";


int man()
{
    uint8_t * inp = (uint8_t *)malloc(10*MBSIZE); //input
    uint8_t * bas = (uint8_t *)malloc(10*MBSIZE);
    uint8_t * del = (uint8_t *)malloc(10*MBSIZE);
    uint8_t * res = (uint8_t *)malloc(10*MBSIZE);
    int F1= open ("/home/thl/test/deltabuf",O_RDONLY, 0777);
    if(F1<=0){
        printf("OpenFile Fail:%s\n","/home/thl/test/deltabuf");
        exit(0);
    }
    int F2= open ("/home/thl/test/basebuf",O_RDONLY, 0777);
    if(F2<=0){
        printf("OpenFile Fail:%s\n","/home/thl/test/basebuf");
        exit(0);
    }
    int F3= open ("/home/thl/test/truebuf",O_RDONLY, 0777);
    if(F3<=0){
        printf("OpenFile Fail:%s\n","/home/thl/test/truebuf");
        exit(0);
    }

    uint32_t Isize = 0, Bsize=0, dsize=0,rsize=0;

    while( int chunkLen = read(F1,(char* )(del+dsize),MBSIZE))
    {
        dsize += chunkLen;
    }
    while( int chunkLen = read(F2,(char* )(bas+Bsize),MBSIZE))
    {
        Bsize += chunkLen;
    }
    while( int chunkLen = read(F3,(char* )(inp+Isize),MBSIZE))
    {
        Isize += chunkLen;
    }
    printf("delta:%d\n",dsize);
    printf("Base:%d\n",Bsize);
    printf("Isize:%d\n",Isize);
    initematrix();
    int r2 = gdecode( (uint8_t*)del,  (uint32_t)dsize,
                      ( uint8_t*)bas, (uint32_t) Bsize,
                      ( uint8_t*)res, (uint32_t *) &rsize);

    if(Isize != rsize){
        printf("restore size (%d) ERROR!\r\n", rsize);

    }

    for(uint32_t i=0;i<rsize;i++)
    {
        if(inp[i]!=res[i])
        {
            printf("xx:%d\n",i);
            exit(0);
        }
    }
    if(memcmp(inp,res,Isize)!=0){
        printf("delta error!!!\n");
    }

    return 0;
}


int main()
{
    uint8_t * inp = (uint8_t *)malloc(10*MBSIZE); //input
    uint8_t * bas = (uint8_t *)malloc(10*MBSIZE);
    uint8_t * del = (uint8_t *)malloc(10*MBSIZE);
    uint8_t * res = (uint8_t *)malloc(10*MBSIZE);
    int F1= open (InputFile,O_RDONLY, 0777);
    if(F1<=0){
        printf("OpenFile Fail:%s\n",InputFile);
        exit(0);
    }
    int F2= open (BaseFile,O_RDONLY, 0777);
    if(F2<=0){
        printf("OpenFile Fail:%s\n",BaseFile);
        exit(0);
    }
    uint32_t Isize = 0, Bsize=0, dsize=0,rsize=0;

    while( int chunkLen = read(F1,(char* )(inp+Isize),MBSIZE))
    {
        Isize += chunkLen;
    }


    while( int chunkLen = read(F2,(char* )(bas+Bsize),MBSIZE))
    {
        Bsize += chunkLen;
    }

    printf("Input:%d\n",Isize);
    printf("Base:%d\n",Bsize);
    dsize = 0;

    /**     初始化参数，调用一次即可    **/
    initematrix();

    /**     初始化参数，调用一次即可    **/

    int b = gencode((uint8_t *) inp, Isize,
                              (uint8_t *) bas, Bsize,
                              (uint8_t *) del, (uint32_t *) &dsize
                             );

    printf("dsize:%d\n",dsize);

    int r2 = gdecode( (uint8_t*)del,  (uint32_t)dsize,
                                ( uint8_t*)bas, (uint32_t) Bsize,
                                ( uint8_t*)res, (uint32_t *) &rsize);


    if(Isize != rsize){
        printf("restore size (%d) ERROR!\r\n", rsize);

    }

    if(memcmp(inp,res,Isize)!=0){
        printf("delta error!!!\n");
    }
    FILE* F4 = fopen("/home/thl/test/rebuf","w");
    fwrite(res,1,rsize,F4);
    fclose(F4);

    return 0;
}
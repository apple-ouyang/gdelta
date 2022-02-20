//
// Created by THL on 2020/7/9.
//

#ifndef GDELTA_GDELTA_H
#define GDELTA_GDELTA_H

#include <stdint.h>


void initematrix();

int gencode( uint8_t* newBuf, u_int32_t newSize,
                       uint8_t* baseBuf, u_int32_t baseSize,
                       uint8_t* deltaBuf, u_int32_t *deltaSize);


int gdecode( uint8_t* deltaBuf, u_int32_t deltaSize,
                          uint8_t* baseBuf, u_int32_t baseSize,
                          uint8_t* outBuf, u_int32_t *outSize);





#endif //GDELTA_GDELTA_H

//
// Created by THL on 2020/7/9.
//

#ifndef GDELTA_GDELTA_H
#define GDELTA_GDELTA_H

#include <stdint.h>


int gencode(uint8_t *newBuf, uint32_t newSize, uint8_t *baseBuf,
            uint32_t baseSize, uint8_t *deltaBuf, uint32_t *deltaSize);

int gdecode(uint8_t *deltaBuf, uint32_t deltaSize, uint8_t *baseBuf,
            uint32_t baseSize, uint8_t *outBuf, uint32_t *outSize);

#endif // GDELTA_GDELTA_H

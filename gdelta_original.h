//
// Created by THL on 2020/7/9.
//

#ifndef GDELTA_ORIGINAL_H
#define GDELTA_ORIGINAL_H
#include <stdint.h>

int gencode_original(uint8_t *newBuf, uint32_t newSize, uint8_t *baseBuf,
            uint32_t baseSize, uint8_t *deltaBuf, uint32_t *deltaSize);

int gdecode_original(uint8_t *deltaBuf, uint32_t deltaSize, uint8_t *baseBuf,
            uint32_t baseSize, uint8_t *outBuf, uint32_t *outSize);

#endif // GDELTA_ORIGINAL_H

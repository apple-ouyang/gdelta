//
// Created by THL on 2020/7/9.
//

#include <cstdio>
#include <cstring>
#include <stdint.h>
#include <sys/time.h>

#include "gdelta.h"
#include "gear_matrix.h"

#define MBSIZE 1024 * 1024
#define FPTYPE uint64_t
#define STRLOOK 16
#define STRLSTEP 2

#define PRINT_PERF 1

#pragma pack(push,1)
typedef struct {
  uint8_t flag: 2;
  uint8_t length: 6;
} FlagLengthB8;

typedef struct {
  uint16_t flag: 2;
  uint16_t length: 14;
} FlagLengthB16;

template<typename var>
struct DeltaUnit
{
  var flag_length;
};

template<typename var>
struct DeltaUnitOffset
{
  var flag_length;
  uint16_t nOffset; // Unused in LITERAL variants
};
#pragma pack(pop)

static_assert(sizeof(DeltaUnitOffset<FlagLengthB8>) == 3, "Expected DeltaUnit<B8> to be 3 bytes");
static_assert(sizeof(DeltaUnitOffset<FlagLengthB16>) == 4, "Expected DeltaUnit<B16> to be 4 bytes");
static_assert(sizeof(DeltaUnit<FlagLengthB8>) == 1, "Expected DeltaUnit<B8> to be 1 bytes");
static_assert(sizeof(DeltaUnit<FlagLengthB16>) == 2, "Expected DeltaUnit<B16> to be 2 bytes");

enum UnitFlag {
  B16_OFFSET = 0b00,
  B8_OFFSET = 0b01,
  B16_LITERAL = 0b10,
  B8_LITERAL = 0b11,

  UF_BITMASK = 0b11
};


template<typename T>
inline void unit_set_flag(T* unit, UnitFlag flag) {
  unit->flag_length.flag = flag;
}

template<typename T>
inline void unit_set_length(T* unit, uint16_t length) {
  unit->flag_length.length = length;
}

UnitFlag unit_get_flag_raw(uint8_t *record) {
  uint8_t flag = *record & UF_BITMASK;
  return (UnitFlag)flag;
}

template<typename T>
inline uint16_t unit_get_length(T* unit) {
  return unit->flag_length.length;
}

int GFixSizeChunking(unsigned char *data, int len, int begflag, int begsize,
                     uint32_t *hash_table, int mask) {

  if (len < STRLOOK)
    return 0;
  int i = 0;
  int movebitlength = 0;
  if (sizeof(FPTYPE) * 8 % STRLOOK == 0)
    movebitlength = sizeof(FPTYPE) * 8 / STRLOOK;
  else
    movebitlength = sizeof(FPTYPE) * 8 / STRLOOK + 1;
  FPTYPE fingerprint = 0;

  /** GEAR **/

  for (; i < STRLOOK; i++) {
    fingerprint = (fingerprint << (movebitlength)) + GEARmx[data[i]];
  }

  i -= STRLOOK;
  FPTYPE index = 0;
  int numChunks = len - STRLOOK + 1;

  int flag = 0;
  if (begflag) {
    while (i < numChunks) {
      if (flag == STRLSTEP) {
        flag = 0;
        index = (fingerprint) >> (sizeof(FPTYPE) * 8 - mask);
        if (hash_table[index] == 0) {
          hash_table[index] = i + begsize;
        } else {
          index = fingerprint >> (sizeof(FPTYPE) * 8 - mask);
          hash_table[index] = i + begsize;
        }
      }
      /** GEAR **/

      fingerprint =
          (fingerprint << (movebitlength)) + GEARmx[data[i + STRLOOK]];

      i++;
      flag++;
    }
  } else {
    while (i < numChunks) {
      if (flag == STRLSTEP) {

        flag = 0;

        index = (fingerprint) >> (sizeof(FPTYPE) * 8 - mask);
        if (hash_table[index] == 0) {
          hash_table[index] = i;

        } else {
          index = fingerprint >> (sizeof(FPTYPE) * 8 - mask);
          hash_table[index] = i;
        }
      }
      /** GEAR **/

      fingerprint =
          (fingerprint << (movebitlength)) + GEARmx[data[i + STRLOOK]];

      i++;
      flag++;
    }
  }

  return 0;
}

int gencode(uint8_t *newBuf, uint32_t newSize, uint8_t *baseBuf,
            uint32_t baseSize, uint8_t *deltaBuf, uint32_t *deltaSize) {
  /* detect the head and tail of one chunk */
  uint32_t beg = 0, end = 0, begSize = 0, endSize = 0;
  uint32_t data_length = 0;
  uint32_t inst_length = 0;
  uint8_t databuf[MBSIZE];
  uint8_t instbuf[MBSIZE];
  if (newSize >= 64 * 1024 || baseSize >= 64 * 1024) {
    fprintf(stderr, "Gdelta not support size >= 64KB.\n");
  }

  while (begSize + 7 < baseSize && begSize + 7 < newSize) {
    if (*(uint64_t *)(baseBuf + begSize) == *(uint64_t *)(newBuf + begSize)) {
      begSize += 8;
    } else
      break;
  }
  while (begSize < baseSize && begSize < newSize) {
    if (baseBuf[begSize] == newBuf[begSize]) {
      begSize++;
    } else
      break;
  }

  if (begSize > 16)
    beg = 1;
  else
    begSize = 0;

  while (endSize + 7 < baseSize && endSize + 7 < newSize) {
    if (*(uint64_t *)(baseBuf + baseSize - endSize - 8) ==
        *(uint64_t *)(newBuf + newSize - endSize - 8)) {
      endSize += 8;
    } else
      break;
  }
  while (endSize < baseSize && endSize < newSize) {
    if (baseBuf[baseSize - endSize - 1] == newBuf[newSize - endSize - 1]) {
      endSize++;
    } else
      break;
  }

  if (begSize + endSize > newSize)
    endSize = newSize - begSize;

  if (endSize > 16)
    end = 1;
  else
    endSize = 0;
  /* end of detect */

  if (begSize + endSize >= baseSize) {
    DeltaUnitOffset<FlagLengthB16> record1;
    DeltaUnit<FlagLengthB16> record2;
    DeltaUnitOffset<FlagLengthB8> record3;
    //DeltaUnit<FlagLengthB8> record4;

    uint32_t deltaLen = 0;
    if (beg) {

      if (begSize < 64) {
        unit_set_flag(&record3, B8_OFFSET);
        record3.nOffset = 0;
        unit_set_length(&record3, begSize);

        memcpy(deltaBuf + deltaLen, &record3.flag_length, 1);
        deltaLen += 1;
        memcpy(deltaBuf + deltaLen, &record3.nOffset, 2);
        deltaLen += 2;
        memcpy(instbuf + inst_length, &record3.flag_length, 1);
        inst_length += 1;
        memcpy(instbuf + inst_length, &record3.nOffset, 2);
        inst_length += 2;
      } else if (begSize < 16384) {
        unit_set_flag(&record1, B16_OFFSET);
        record1.nOffset = 0;
        unit_set_length(&record1, begSize);

        memcpy(deltaBuf + deltaLen, &record1, sizeof(DeltaUnitOffset<FlagLengthB16>));
        memcpy(instbuf + inst_length, &record1, sizeof(DeltaUnitOffset<FlagLengthB16>));
        deltaLen += sizeof(DeltaUnitOffset<FlagLengthB16>);
        inst_length += sizeof(DeltaUnitOffset<FlagLengthB16>);
      } else { // TODO: > 16383

        int matchlen = begSize;
        int offset = 0;
        while (matchlen > 16383) {
          unit_set_flag(&record1, B16_OFFSET);
          record1.nOffset = offset;
          unit_set_length(&record1, 16383);
          offset += 16383;
          matchlen -= 16383;
          memcpy(instbuf + inst_length, &record1, sizeof(DeltaUnitOffset<FlagLengthB16>));
          inst_length += sizeof(DeltaUnitOffset<FlagLengthB16>);
        }
        if (matchlen) {
          unit_set_flag(&record1, B16_OFFSET);
          record1.nOffset = offset;
          unit_set_length(&record1, matchlen);
          memcpy(instbuf + inst_length, &record1, sizeof(DeltaUnitOffset<FlagLengthB16>));
          inst_length += sizeof(DeltaUnitOffset<FlagLengthB16>);
        }
      }
    }
    if (newSize - begSize - endSize > 0) {

      int litlen = newSize - begSize - endSize;
      int copylen = 0;
      while (litlen > 16383) {
        unit_set_flag(&record2, B16_LITERAL);
        unit_set_length(&record2, 16383);
        memcpy(instbuf + inst_length, &record2, sizeof(DeltaUnit<FlagLengthB16>));
        inst_length += sizeof(DeltaUnit<FlagLengthB16>);
        memcpy(databuf + data_length, newBuf + begSize + copylen, 16383);
        litlen -= 16383;
        data_length += 16383;
        copylen += 16383;
      }
      if (litlen) {
        unit_set_flag(&record2, B16_LITERAL);
        unit_set_length(&record2, litlen);

        memcpy(instbuf + inst_length, &record2, sizeof(DeltaUnit<FlagLengthB16>));
        inst_length += sizeof(DeltaUnit<FlagLengthB16>);
        memcpy(databuf + data_length, newBuf + begSize + copylen, litlen);
        data_length += litlen;
      }
    }
    if (end) {
      int matchlen = endSize;
      int offset = baseSize - endSize;
      while (matchlen > 16383) {
        unit_set_flag(&record1, B16_OFFSET);
        record1.nOffset = offset;
        unit_set_length(&record1, 16383);
        offset += 16383;
        matchlen -= 16383;
        memcpy(instbuf + inst_length, &record1, sizeof(DeltaUnitOffset<FlagLengthB16>));
        inst_length += sizeof(DeltaUnitOffset<FlagLengthB16>);
      }
      if (matchlen) {
        unit_set_flag(&record1, B16_OFFSET);
        record1.nOffset = offset;
        unit_set_length(&record1, matchlen);
        memcpy(instbuf + inst_length, &record1, sizeof(DeltaUnitOffset<FlagLengthB16>));
        inst_length += sizeof(DeltaUnitOffset<FlagLengthB16>);
      }
    }

    int instlen = 0;
    if (1) {

      deltaLen = 0;
      uint16_t tmp = inst_length + sizeof(uint16_t);
      instlen += sizeof(uint16_t);

      memcpy(deltaBuf + deltaLen, &tmp, sizeof(uint16_t));
      deltaLen += sizeof(uint16_t);

      memcpy(deltaBuf + deltaLen, instbuf, inst_length);
      deltaLen += inst_length;
      instlen += inst_length;
      memcpy(deltaBuf + deltaLen, databuf, data_length);
      deltaLen += data_length;
    } else {
      fprintf(stderr, "wrong instruction and data \n");
    }

    *deltaSize = deltaLen;

    return instlen;
  }

  /* chunk the baseFile */

  uint32_t deltaLen = 0;


  int tmp = (baseSize - begSize - endSize) + 10;

  int bit;
  for (bit = 0; tmp; bit++) {
    tmp >>= 1;
  }

  uint64_t xxsize = 0XFFFFFFFFFFFFFFFF >> (64 - bit); // mask

  // uint32_t hash_size=0XFFFFFFFF>>(32-tmp);
  uint32_t hash_size = xxsize + 1;

  //    uint32_t *hash_table = (uint32_t *) malloc(sizeof(uint32_t) *
  //    hash_size); memset(hash_table, 0, sizeof(uint32_t) * hash_size);
  uint32_t hash_table[hash_size];

  memset(hash_table, 0, sizeof(uint32_t) * hash_size);
#if PRINT_PERF
  struct timeval t0, t1;
  gettimeofday(&t0, NULL);
#endif

  GFixSizeChunking(baseBuf + begSize, baseSize - begSize - endSize, beg,
                   begSize, hash_table, bit);
#if PRINT_PERF
  gettimeofday(&t1, NULL);

  fprintf(stderr, "size:%d\n",baseSize - begSize - endSize);
  fprintf(stderr, "hash size:%d\n",hash_size);
  fprintf(stderr, "rolling hash:%.3fMB/s\n", (double)(baseSize - begSize - endSize)/1024/1024/((t1.tv_sec-t0.tv_sec) *1000000 + t1.tv_usec - t0.tv_usec)*1000000);
  fprintf(stderr, "rooling hash:%lu\n", (t1.tv_sec-t0.tv_sec)*1000000 + t1.tv_usec - t0.tv_usec);

  gettimeofday(&t0, NULL);
  fprintf(stderr, "hash table :%lu\n", (t0.tv_sec-t1.tv_sec) *1000000 + t0.tv_usec - t1.tv_usec);
#endif
  /* end of inserting */

  uint32_t inputPos = begSize;
  uint32_t cursor;
  uint32_t length;
  FPTYPE hash;
  // DeltaRecord *psDupSubCnk = NULL;
  DeltaUnitOffset<FlagLengthB16> record1;
  DeltaUnit<FlagLengthB16> record2;
  DeltaUnitOffset<FlagLengthB8> record3;
  DeltaUnit<FlagLengthB8> record4;
  unit_set_flag(&record1, B16_OFFSET);
  unit_set_flag(&record2, B16_LITERAL);
  unit_set_flag(&record3, B8_OFFSET);
  unit_set_flag(&record4, B8_LITERAL);
  int unmatch64flag = 0;
  int flag = 0; /* to represent the last record in the deltaBuf,
       1 for DeltaUnit1, 2 for DeltaUnit2 */

  int movebitlength = 0;
  if (sizeof(FPTYPE) * 8 % STRLOOK == 0)
    movebitlength = sizeof(FPTYPE) * 8 / STRLOOK;
  else
    movebitlength = sizeof(FPTYPE) * 8 / STRLOOK + 1;
  if (beg) {
    if (begSize < 64) {
      record3.nOffset = 0;
      unit_set_length(&record3, begSize);
      memcpy(instbuf + inst_length, &record3.flag_length, 1);
      inst_length += 1;
      memcpy(instbuf + inst_length, &record3.nOffset, 2);
      inst_length += 2;
      flag = 1;
    } else if (begSize < 16384) {
      record1.nOffset = 0;
      unit_set_length(&record1, begSize);
      memcpy(instbuf + inst_length, &record1, sizeof(DeltaUnitOffset<FlagLengthB16>));
      inst_length += sizeof(DeltaUnitOffset<FlagLengthB16>);
      flag = 1;
    } else {
      int matchlen = begSize;
      int offset = 0;
      flag = 1;
      while (matchlen > 16383) {
        unit_set_flag(&record1, B16_OFFSET);
        record1.nOffset = offset;
        unit_set_length(&record1, 16383);
        offset += 16383;
        matchlen -= 16383;
        memcpy(instbuf + inst_length, &record1, sizeof(DeltaUnitOffset<FlagLengthB16>));
        inst_length += sizeof(DeltaUnitOffset<FlagLengthB16>);
      }
      if (matchlen) {
        unit_set_flag(&record1, B16_OFFSET);
        record1.nOffset = offset;
        unit_set_length(&record1, matchlen);
        memcpy(instbuf + inst_length, &record1, sizeof(DeltaUnitOffset<FlagLengthB16>));
        inst_length += sizeof(DeltaUnitOffset<FlagLengthB16>);
      }
    }
  }

  FPTYPE fingerprint = 0;
  for (uint32_t i = 0; i < STRLOOK && i < newSize - endSize - inputPos; i++) {
    fingerprint =
        (fingerprint << (movebitlength)) + GEARmx[(newBuf + inputPos)[i]];
  }

  int mathflag = 0;
  uint32_t handlebytes = begSize;
  //int find2 = 0;
  int find1 = 0;
  while (inputPos + STRLOOK <= newSize - endSize) {

    if (newSize - endSize - inputPos < STRLOOK) {
      cursor = inputPos + (newSize - endSize);
      length = newSize - endSize - inputPos;
    } else {
      cursor = inputPos + STRLOOK;
      length = STRLOOK;
    }
    hash = fingerprint;
    int index1 = hash >> (sizeof(FPTYPE) * 8 - bit);

    int offset;
    int index;
    if (hash_table[index1] != 0 &&
        memcmp(newBuf + inputPos, baseBuf + hash_table[index1], length) == 0) {
      mathflag = 1;
      find1++;
      index = index1;
      offset = hash_table[index];
    }

    /* lookup */
    if (mathflag) {
      if (1) {
        if (flag == B16_LITERAL) {

          if (unit_get_length(&record2) <= 63) {
            unmatch64flag = 1;
            unit_set_length(&record4, unit_get_length(&record2));
            memcpy(instbuf + inst_length - sizeof(DeltaUnit<FlagLengthB16>), &record4,
                   sizeof(DeltaUnit<FlagLengthB8>));

            inst_length -= 1;
          } else {
            memcpy(instbuf + inst_length - sizeof(DeltaUnit<FlagLengthB16>), &record2,
                   sizeof(DeltaUnit<FlagLengthB16>));
          }
        }

        int j = 0;
        mathflag = 1;
#if 1 /* 8-bytes optimization */
        while (offset + length + j + 7 < baseSize - endSize &&
               cursor + j + 7 < newSize - endSize) {
          if (*(uint64_t *)(baseBuf + offset + length + j) ==
              *(uint64_t *)(newBuf + cursor + j)) {
            j += 8;
          } else
            break;
        }
        while (offset + length + j < baseSize - endSize &&
               cursor + j < newSize - endSize) {
          if (baseBuf[offset + length + j] == newBuf[cursor + j]) {
            j++;
          } else
            break;
        }
#endif
        cursor += j;

        // TODO:!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Cancel
        // fast_set_lengthv3(&record1, cursor - inputPos,0);

        int matchlen = cursor - inputPos;
        handlebytes += cursor - inputPos;
        record1.nOffset = offset;

        /* detect backward */
        int k = 0;
        if (flag == B16_LITERAL) {
          while (k + 1 <= offset && k + 1 <= unit_get_length(&record2)) {
            if (baseBuf[offset - (k + 1)] == newBuf[inputPos - (k + 1)])
              k++;
            else
              break;
          }
        }

        if (k > 0) {
          //                    deltaLen -= fast_get_lengthv3(&record2);
          //                    deltaLen -= sizeof(DeltaUnit2);
          data_length -= unit_get_length(&record2);

          if (unmatch64flag) {
            inst_length -= sizeof(DeltaUnit<FlagLengthB8>);
          } else {
            inst_length -= sizeof(DeltaUnit<FlagLengthB16>);
          }
          unmatch64flag = 0;

          unit_set_length(&record2, unit_get_length(&record2) - k);

          if (unit_get_length(&record2) > 0) {

            if (unit_get_length(&record2) >= 64) {
              memcpy(instbuf + inst_length, &record2, sizeof(DeltaUnit<FlagLengthB16>));
              inst_length += sizeof(DeltaUnit<FlagLengthB16>);
            } else {
              unit_set_length(&record4, unit_get_length(&record2));
              memcpy(instbuf + inst_length, &record4, sizeof(DeltaUnit<FlagLengthB8>));
              inst_length += sizeof(DeltaUnit<FlagLengthB8>);
            }

            data_length += unit_get_length(&record2);
          }

          matchlen += k;
          record1.nOffset -= k;
        }

        if (matchlen < 64) {
          record3.nOffset = record1.nOffset;
          unit_set_length(&record3, matchlen);
          memcpy(instbuf + inst_length, &record3.flag_length, 1);
          inst_length += 1;
          memcpy(instbuf + inst_length, &record3.nOffset, 2);
          inst_length += 2;
        } else if (matchlen < 16384) {
          unit_set_length(&record1, matchlen);
          memcpy(instbuf + inst_length, &record1, sizeof(DeltaUnitOffset<FlagLengthB16>));
          inst_length += sizeof(DeltaUnitOffset<FlagLengthB16>);
        } else {
          offset = record1.nOffset;
          while (matchlen > 16383) {
            record1.nOffset = offset;
            unit_set_length(&record1, 16383);
            offset += 16383;
            matchlen -= 16383;
            memcpy(instbuf + inst_length, &record1, sizeof(DeltaUnitOffset<FlagLengthB16>));
            inst_length += sizeof(DeltaUnitOffset<FlagLengthB16>);
          }
          if (matchlen) {
            record1.nOffset = offset;
            unit_set_length(&record1, matchlen);
            memcpy(instbuf + inst_length, &record1, sizeof(DeltaUnitOffset<FlagLengthB16>));
            inst_length += sizeof(DeltaUnitOffset<FlagLengthB16>);
          }
        }
        unmatch64flag = 0;
        flag = 1;
      } else {
        // printf("Spooky Hash Error!!!!!!!!!!!!!!!!!!\n");
        goto handle_hash_error;
      }
    } else {
    handle_hash_error:
      if (flag == B16_LITERAL) {

        if (unit_get_length(&record2) < 16383) {
          memcpy(databuf + data_length, newBuf + inputPos, 1);
          data_length += 1;
          handlebytes += 1;
          uint16_t lentmp = unit_get_length(&record2);
          unit_set_length(&record2, lentmp + 1);
        } else {
          memcpy(instbuf + inst_length - sizeof(DeltaUnit<FlagLengthB16>), &record2,
                 sizeof(DeltaUnit<FlagLengthB16>));
          handlebytes += 1;
          unit_set_length(&record2, 1);
          memcpy(instbuf + inst_length, &record2, sizeof(DeltaUnit<FlagLengthB16>));
          inst_length += sizeof(DeltaUnit<FlagLengthB16>);
          memcpy(databuf + data_length, newBuf + inputPos, 1);
          data_length += 1;
        }

      } else {
        handlebytes += 1;
        unit_set_length(&record2, 1);
        memcpy(instbuf + inst_length, &record2, sizeof(DeltaUnit<FlagLengthB16>));
        inst_length += sizeof(DeltaUnit<FlagLengthB16>);
        memcpy(databuf + data_length, newBuf + inputPos, 1);
        data_length += 1;
        flag = 2;
      }
    }
    if (mathflag) {
      //            for (int j = inputPos + STRLOOK; j < cursor + STRLOOK &&
      //            cursor + STRLOOK < newSize - endSize; j++) {
      //                fingerprint = (fingerprint << (movebitlength)) +
      //                GEAR[newBuf[j]];
      //            }
      for (uint32_t j = cursor;
           j < cursor + STRLOOK && cursor + STRLOOK < newSize - endSize; j++) {
        fingerprint = (fingerprint << (movebitlength)) + GEARmx[newBuf[j]];
      }

      inputPos = cursor;
    } else {

      if (inputPos + STRLOOK < newSize - endSize)
        fingerprint = (fingerprint << (movebitlength)) +
                      GEARmx[newBuf[inputPos + STRLOOK]];
      inputPos++;
    }
    mathflag = 0;
    //        printf("datalen:%d\n",data_length);
  }

#if PRINT_PERF
  gettimeofday(&t1, NULL);
  fprintf(stderr, "look up:%lu\n", (t1.tv_sec-t0.tv_sec) *1000000 + t1.tv_usec - t0.tv_usec); 
  fprintf(stderr, "look up:%.3fMB/s\n", (double)(baseSize - begSize - endSize)/1024/1024/((t1.tv_sec-t0.tv_sec) *1000000 + t1.tv_usec - t0.tv_usec)*1000000);
#endif

  if (flag == B16_LITERAL) {

    //        memcpy(deltaBuf + deltaLen, newBuf + handlebytes, newSize -
    //        endSize - handlebytes);
    memcpy(databuf + data_length, newBuf + handlebytes,
           newSize - endSize - handlebytes);
    //        deltaLen += newSize - endSize - handlebytes;
    data_length += (newSize - endSize - handlebytes);

    int litlen =
        unit_get_length(&record2) + (newSize - endSize - handlebytes);
    if (litlen < 16384) {
      unit_set_length(&record2, litlen);
      memcpy(instbuf + inst_length - sizeof(DeltaUnit<FlagLengthB16>), &record2,
             sizeof(DeltaUnit<FlagLengthB16>));
    } else {
      unit_set_length(&record2, 16383);
      memcpy(instbuf + inst_length - sizeof(DeltaUnit<FlagLengthB16>), &record2,
             sizeof(DeltaUnit<FlagLengthB16>));
      unit_set_length(&record2, litlen - 16383);
      memcpy(instbuf + inst_length, &record2, sizeof(DeltaUnit<FlagLengthB16>));
      inst_length += sizeof(DeltaUnit<FlagLengthB16>);
    }

  } else {
    if (newSize - endSize - handlebytes) {
      unit_set_length(&record2, newSize - endSize - handlebytes);

      //            memcpy(deltaBuf + deltaLen, &record2,
      //            sizeof(DeltaUnit2));
      memcpy(instbuf + inst_length, &record2, sizeof(DeltaUnit<FlagLengthB16>));
      //            deltaLen += sizeof(DeltaUnit2);
      inst_length += sizeof(DeltaUnit<FlagLengthB16>);

      //            memcpy(deltaBuf + deltaLen, newBuf + inputPos, newSize -
      //            endSize - handlebytes);
      memcpy(databuf + data_length, newBuf + inputPos,
             newSize - endSize - handlebytes);
      //            deltaLen += newSize - endSize - handlebytes;
      data_length += newSize - endSize - handlebytes;
    }
  }

  if (end) {
    int matchlen = endSize;
    int offset = baseSize - endSize;
    while (matchlen > 16383) {
      unit_set_flag(&record1, B16_OFFSET);
      record1.nOffset = offset;
      unit_set_length(&record1, 16383);
      offset += 16383;
      matchlen -= 16383;
      memcpy(instbuf + inst_length, &record1, sizeof(DeltaUnitOffset<FlagLengthB16>));
      inst_length += sizeof(DeltaUnitOffset<FlagLengthB16>);
    }
    if (matchlen) {
      record1.nOffset = offset;
      unit_set_length(&record1, matchlen);
      memcpy(instbuf + inst_length, &record1, sizeof(DeltaUnitOffset<FlagLengthB16>));
      inst_length += sizeof(DeltaUnitOffset<FlagLengthB16>);
    }
  }
  int inslen = 0;

  if (1) {

    deltaLen = 0;
    tmp = inst_length + sizeof(uint16_t);
    memcpy(deltaBuf + deltaLen, &tmp, sizeof(uint16_t));
    deltaLen += sizeof(uint16_t);
    inslen += sizeof(uint16_t);
    memcpy(deltaBuf + deltaLen, instbuf, inst_length);
    deltaLen += inst_length;
    inslen += inst_length;
    memcpy(deltaBuf + deltaLen, databuf, data_length);
    deltaLen += data_length;
  } else {
    fprintf(stderr, "wrong instruction and data \n");
  }

  *deltaSize = deltaLen;
  return inslen;
}

int gdecode(uint8_t *deltaBuf,  uint32_t, uint8_t *baseBuf,
            uint32_t, uint8_t *outBuf, uint32_t *outSize) {

  /* datalength is the cursor of outBuf, and readLength deltaBuf */
  uint32_t dataLength = 0, readLength = sizeof(uint16_t);

  uint32_t addatalenth = 0;
  memcpy(&addatalenth, deltaBuf, sizeof(uint16_t));
  uint32_t instructionlenth = addatalenth;

  int matchnum = 0;
  uint32_t matchlength = 0;
  uint32_t unmatchlength = 0;
  int unmatchnum = 0;
  while (1) {
    uint16_t flag = unit_get_flag_raw(deltaBuf + readLength);

    if (flag == B16_OFFSET) { // Matched Offset Literal 16b length
      matchnum++;
      DeltaUnitOffset<FlagLengthB16> record;
      memcpy(&record, deltaBuf + readLength, sizeof(DeltaUnitOffset<FlagLengthB16>));

      readLength += sizeof(DeltaUnitOffset<FlagLengthB16>);

      matchlength += unit_get_length(&record);

      memcpy(outBuf + dataLength, baseBuf + record.nOffset,
             unit_get_length(&record));

      // printf("match length:%d\n",get_length(&record));
      dataLength += unit_get_length(&record);
    } else if (flag == B16_LITERAL) { // Unmatched Literal 16b length
      unmatchnum++;
      DeltaUnit<FlagLengthB16> record;
      memcpy(&record, deltaBuf + readLength, sizeof(DeltaUnit<FlagLengthB16>));

      readLength += sizeof(DeltaUnit<FlagLengthB16>);

      unmatchlength += unit_get_length(&record);

      memcpy(outBuf + dataLength, deltaBuf + addatalenth,
             unit_get_length(&record));

      // printf("unmatch length:%d\n",get_length(&record));
      addatalenth += unit_get_length(&record);
      dataLength += unit_get_length(&record);
    } else if (flag == B8_OFFSET) { // Matched Offset Literal 8b length

      matchnum++;
      DeltaUnitOffset<FlagLengthB8> record;
      memcpy(&record.flag_length, deltaBuf + readLength, 1);
      readLength += 1;
      memcpy(&record.nOffset, deltaBuf + readLength, 2);
      readLength += 2;

      // printf("offset: %d\n",record.nOffset);

      matchlength += unit_get_length(&record);

      memcpy(outBuf + dataLength, baseBuf + record.nOffset,
             unit_get_length(&record));

      // printf("match length:%d\n",get_length(&record));
      dataLength += unit_get_length(&record);
    } else if (flag == B8_LITERAL) { // Unmatched Literal 8b length
      unmatchnum++;
      DeltaUnit<FlagLengthB8> record;
      memcpy(&record, deltaBuf + readLength, sizeof(DeltaUnit<FlagLengthB8>));

      readLength += sizeof(DeltaUnit<FlagLengthB8>);

      memcpy(outBuf + dataLength, deltaBuf + addatalenth,
             unit_get_length(&record));

      // printf("unmatch length:%d\n",get_length(&record));
      addatalenth += unit_get_length(&record);
      dataLength += unit_get_length(&record);
      unmatchlength += unit_get_length(&record);
    }

    if (readLength >= instructionlenth) {
      break;
    }
  }

  // printf("decode data len = %d.\r\n", dataLength);

  *outSize = dataLength;
  return dataLength;
}

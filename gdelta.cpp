//
// Created by THL on 2020/7/9.
//

#include <cstdio>
#include <cstring>
#include <stdint.h>
#include <cstdlib>

#ifdef _MSC_VER
#include <compat/msvc.h>
#else
#include <ctime>
#endif

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
  uint16_t nOffset;
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

typedef struct {
  uint8_t *buf;
  uint64_t cursor;
  uint64_t length;
} BufferStreamDescriptor;

template<typename T>
void write_field(BufferStreamDescriptor &buffer, const T& field)
{
  memcpy(buffer.buf + buffer.cursor, &field, sizeof(T));
  buffer.cursor += sizeof(T);
  // TODO: check bounds (buffer->length)?
}

void stream_into(BufferStreamDescriptor &dest, BufferStreamDescriptor &src, size_t length) {
  memcpy(dest.buf + dest.cursor, src.buf + src.cursor, length);
  dest.cursor += length;
  src.cursor += length;
}

void write_concat_buffer(BufferStreamDescriptor &dest, const BufferStreamDescriptor &src) {
  memcpy(dest.buf + dest.cursor, src.buf, src.cursor);
  dest.cursor += src.cursor;
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
  uint8_t databuf[MBSIZE];
  uint8_t instbuf[MBSIZE];
  if (newSize >= 64 * 1024 || baseSize >= 64 * 1024) {
    fprintf(stderr, "Gdelta not support size >= 64KB.\n");
  }

  while (begSize + sizeof(uint64_t) <= baseSize && begSize + sizeof(uint64_t) <= newSize) {
    if (*(uint64_t *)(baseBuf + begSize) == *(uint64_t *)(newBuf + begSize)) {
      begSize += sizeof(uint64_t);
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

  while (endSize + sizeof(uint64_t) <= baseSize && endSize + sizeof(uint64_t) <= newSize) {
    if (*(uint64_t *)(baseBuf + baseSize - endSize - sizeof(uint64_t)) ==
        *(uint64_t *)(newBuf + newSize - endSize - sizeof(uint64_t))) {
      endSize += sizeof(uint64_t);
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

  BufferStreamDescriptor deltaStream = {deltaBuf, 0, *deltaSize};
  BufferStreamDescriptor instStream = {instbuf,  0, sizeof(instbuf)};
  BufferStreamDescriptor dataStream = {databuf,  0, sizeof(databuf)};
  BufferStreamDescriptor newStream = {newBuf,  begSize, newSize};

  if (begSize + endSize >= baseSize) { // TODO: test this path
    DeltaUnitOffset<FlagLengthB16> record1;
    DeltaUnit<FlagLengthB16> record2;
    DeltaUnitOffset<FlagLengthB8> record3;
    //DeltaUnit<FlagLengthB8> record4;

    if (beg) {
      if (begSize < 64) {
        unit_set_flag(&record3, B8_OFFSET);
        record3.nOffset = 0;
        unit_set_length(&record3, begSize);

	write_field(deltaStream, record3.flag_length);
	write_field(deltaStream, record3.nOffset);

	write_field(instStream, record3.flag_length);
	write_field(instStream, record3.nOffset);
      } else if (begSize <= 16383) {
        unit_set_flag(&record1, B16_OFFSET);
        record1.nOffset = 0;
        unit_set_length(&record1, begSize);

	write_field(deltaStream, record1);
	write_field(instStream, record1);
      } else { // TODO: > 16383

        int matchlen = begSize;
        int offset = 0;
        while (matchlen > 16383) {
          unit_set_flag(&record1, B16_OFFSET);
          record1.nOffset = offset;
          unit_set_length(&record1, 16383);
          offset += 16383;
          matchlen -= 16383;
	  write_field(instStream, record1);
        }
        if (matchlen) {
          unit_set_flag(&record1, B16_OFFSET);
          record1.nOffset = offset;
          unit_set_length(&record1, matchlen);
	  write_field(instStream, record1);
        }
      }
    }
    if (newSize - begSize - endSize > 0) {
      int litlen = newSize - begSize - endSize;
      while (litlen > 16383) {
        unit_set_flag(&record2, B16_LITERAL);
        unit_set_length(&record2, 16383);
	write_field(instStream, record2);
	stream_into(dataStream, newStream, 16383);
        litlen -= 16383;
      }
      if (litlen) {
        unit_set_flag(&record2, B16_LITERAL);
        unit_set_length(&record2, litlen);

	write_field(instStream, record2);
	stream_into(dataStream, newStream, litlen);
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
	write_field(instStream, record1);
      }
      if (matchlen) {
        unit_set_flag(&record1, B16_OFFSET);
        record1.nOffset = offset;
        unit_set_length(&record1, matchlen);
	write_field(instStream, record1);
      }
    }

    int instlen = sizeof(uint16_t) * 2 + instStream.cursor;

    // TODO: overwrites BegSize < 64 path in original code??
    //deltaStream.cursor = 0;
    //dataStream.cursor = 0;
    //instStream.cursor = 0;

    uint16_t tmp = instStream.cursor + sizeof(uint16_t);
    write_field(deltaStream, tmp);
    write_concat_buffer(deltaStream, instStream);
    write_concat_buffer(deltaStream, dataStream);

    *deltaSize = sizeof(uint16_t) + instStream.cursor + dataStream.cursor;
    return instlen;
  }

  /* chunk the baseFile */
  int tmp = (baseSize - begSize - endSize) + 10;
  int bit;
  for (bit = 0; tmp; bit++) tmp >>= 1;
  uint64_t xxsize = 0XFFFFFFFFFFFFFFFF >> (64 - bit); // mask
  uint32_t hash_size = xxsize + 1;
  uint32_t *hash_table = (uint32_t*)malloc(hash_size * sizeof(uint32_t));
  memset(hash_table, 0, sizeof(uint32_t) * hash_size);

#if PRINT_PERF
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
#endif

  GFixSizeChunking(baseBuf + begSize, baseSize - begSize - endSize, beg,
                   begSize, hash_table, bit);
#if PRINT_PERF

  clock_gettime(CLOCK_MONOTONIC, &t1);

  fprintf(stderr, "size:%d\n",baseSize - begSize - endSize);
  fprintf(stderr, "hash size:%d\n",hash_size);
  fprintf(stderr, "rolling hash:%.3fMB/s\n", (double)(baseSize - begSize - endSize)/1024/1024/((t1.tv_sec-t0.tv_sec) *1000000000 + t1.tv_nsec - t0.tv_nsec)*1000000000);
  fprintf(stderr, "rooling hash:%zd\n", (t1.tv_sec-t0.tv_sec)*1000000000 + t1.tv_nsec - t0.tv_nsec);

  clock_gettime(CLOCK_MONOTONIC, &t0);
  fprintf(stderr, "hash table :%zd\n", (t0.tv_sec-t1.tv_sec) *1000000000 + t0.tv_nsec - t1.tv_nsec);
#endif
  /* end of inserting */

  uint32_t inputPos = begSize;
  uint32_t cursor;
  uint32_t length;
  FPTYPE hash;
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
      write_field(instStream, record3.flag_length);
      write_field(instStream, record3.nOffset);
      flag = 1;
    } else if (begSize < 16384) {
      record1.nOffset = 0;
      unit_set_length(&record1, begSize);
      write_field(instStream, record1);
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
	write_field(instStream, record1);
      }
      if (matchlen) {
        unit_set_flag(&record1, B16_OFFSET);
        record1.nOffset = offset;
        unit_set_length(&record1, matchlen);
	write_field(instStream, record1);
      }
    }
  }

  FPTYPE fingerprint = 0;
  for (uint32_t i = 0; i < STRLOOK && i < newSize - endSize - inputPos; i++) {
    fingerprint = (fingerprint << (movebitlength)) + GEARmx[(newBuf + inputPos)[i]];
  }

  int mathflag = 0;
  uint32_t handlebytes = begSize;
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
    if (hash_table[index1] != 0 && memcmp(newBuf + inputPos, baseBuf + hash_table[index1], length) == 0) {
      mathflag = 1;
      find1++;
      index = index1;
      offset = hash_table[index];
    }

    /* lookup */
    if (mathflag) {
      if (1) {
        if (flag == B16_LITERAL) {
	  instStream.cursor -= sizeof(DeltaUnit<FlagLengthB16>);
          if (unit_get_length(&record2) <= 63) {
            unmatch64flag = 1;
            unit_set_length(&record4, unit_get_length(&record2));
	    write_field(instStream, record4);
          } else {
	    write_field(instStream, record2);
          }
        }

        int j = 0;
        mathflag = 1;
#if 1 /* 8-bytes optimization */
        while (offset + length + j + 7 < baseSize - endSize &&
               cursor + j + 7 < newSize - endSize) {
          if (*(uint64_t *)(baseBuf + offset + length + j) ==
              *(uint64_t *)(newBuf + cursor + j)) {
            j += sizeof(uint64_t);
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
          dataStream.cursor -= unit_get_length(&record2);

          if (unmatch64flag) {
	    instStream.cursor -= sizeof(DeltaUnit<FlagLengthB8>);
          } else {
	    instStream.cursor -= sizeof(DeltaUnit<FlagLengthB16>);
          }
          unmatch64flag = 0;
          unit_set_length(&record2, unit_get_length(&record2) - k);

          if (unit_get_length(&record2) > 0) {
            if (unit_get_length(&record2) >= 64) {
	      write_field(instStream, record2);
            } else {
              unit_set_length(&record4, unit_get_length(&record2));
	      write_field(instStream, record4);
            }

            dataStream.cursor += unit_get_length(&record2);
          }

          matchlen += k;
          record1.nOffset -= k;
        }

        if (matchlen < 64) {
          record3.nOffset = record1.nOffset;
          unit_set_length(&record3, matchlen);
	  write_field(instStream, record3.flag_length);
          write_field(instStream, record3.nOffset);
        } else if (matchlen < 16384) {
          unit_set_length(&record1, matchlen);
	  write_field(instStream, record1);
        } else {
          offset = record1.nOffset;
          while (matchlen > 16383) {
            record1.nOffset = offset;
            unit_set_length(&record1, 16383);
            offset += 16383;
            matchlen -= 16383;
	    write_field(instStream, record1);
          }
          if (matchlen) {
            record1.nOffset = offset;
            unit_set_length(&record1, matchlen);
	    write_field(instStream, record1);
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
          memcpy(dataStream.buf + dataStream.cursor, newBuf + inputPos, 1);
          dataStream.cursor += 1;
          handlebytes += 1;
          uint16_t lentmp = unit_get_length(&record2);
          unit_set_length(&record2, lentmp + 1);
        } else {
	  instStream.cursor -= sizeof(record2);
	  write_field(instStream, record2);
          handlebytes += 1;
          unit_set_length(&record2, 1);
	  write_field(instStream, record2);
          memcpy(dataStream.buf + dataStream.cursor, newBuf + inputPos, 1);
          dataStream.cursor += 1;
        }

      } else {
        handlebytes += 1;
        unit_set_length(&record2, 1);
	write_field(instStream, record2);
        memcpy(dataStream.buf + dataStream.cursor, newBuf + inputPos, 1);
        dataStream.cursor += 1;
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
  }

#if PRINT_PERF
  clock_gettime(CLOCK_MONOTONIC, &t1);
  fprintf(stderr, "look up:%zd\n", (t1.tv_sec-t0.tv_sec) *1000000000 + t1.tv_nsec - t0.tv_nsec); 
  fprintf(stderr, "look up:%.3fMB/s\n", (double)(baseSize - begSize - endSize)/1024/1024/((t1.tv_sec-t0.tv_sec) *1000000000 + t1.tv_nsec - t0.tv_nsec)*1000000000);
#endif

  if (flag == B16_LITERAL) {
    newStream.cursor = handlebytes;
    stream_into(dataStream, newStream, newSize - endSize - handlebytes);

    int litlen = unit_get_length(&record2) + (newSize - endSize - handlebytes);
    if (litlen < 16384) {
      unit_set_length(&record2, litlen);
      instStream.cursor -= sizeof(record2);
      write_field(instStream, record2);

    } else {
      unit_set_length(&record2, 16383);
      instStream.cursor -= sizeof(record2);
      write_field(instStream, record2);
      unit_set_length(&record2, litlen - 16383);
      write_field(instStream, record2);
    }

  } else {
    if (newSize - endSize - handlebytes) {
      unit_set_length(&record2, newSize - endSize - handlebytes);

      write_field(instStream, record2);

      newStream.cursor = inputPos;
      stream_into(dataStream, newStream, newSize - endSize - handlebytes);
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
      write_field(instStream, record1);
    }
    if (matchlen) {
      record1.nOffset = offset;
      unit_set_length(&record1, matchlen);
      write_field(instStream, record1);
    }
  }

  int inslen = 0;
  tmp = instStream.cursor + sizeof(uint16_t);

  deltaStream.cursor = 0;
  write_field(deltaStream, (uint16_t)tmp);
  inslen += sizeof(uint16_t);
  write_concat_buffer(deltaStream, instStream);
  inslen += instStream.cursor;

  write_concat_buffer(deltaStream, dataStream);

  *deltaSize = deltaStream.cursor;
  return inslen;
}

int gdecode(uint8_t *deltaBuf,  uint32_t, uint8_t *baseBuf,
            uint32_t, uint8_t *outBuf, uint32_t *outSize) {

  /* datalength is the cursor of outBuf, and readLength deltaBuf */
  uint32_t dataLength = 0, readLength = sizeof(uint16_t);
  uint32_t addDataLength = 0;
  memcpy(&addDataLength, deltaBuf, sizeof(uint16_t));
  uint32_t instructionlenth = addDataLength;

  while (1) {
    uint16_t flag = unit_get_flag_raw(deltaBuf + readLength);

    if (flag == B16_OFFSET) { // Matched Offset Literal 16b length
      DeltaUnitOffset<FlagLengthB16> record;
      memcpy(&record, deltaBuf + readLength, sizeof(DeltaUnitOffset<FlagLengthB16>));
      readLength += sizeof(DeltaUnitOffset<FlagLengthB16>);

      memcpy(outBuf + dataLength, baseBuf + record.nOffset, unit_get_length(&record));
      dataLength += unit_get_length(&record);
    } else if (flag == B16_LITERAL) { // Unmatched Literal 16b length
      DeltaUnit<FlagLengthB16> record;
      memcpy(&record, deltaBuf + readLength, sizeof(DeltaUnit<FlagLengthB16>));
      readLength += sizeof(DeltaUnit<FlagLengthB16>);

      memcpy(outBuf + dataLength, deltaBuf + addDataLength, unit_get_length(&record));
      addDataLength += unit_get_length(&record);
      dataLength += unit_get_length(&record);
    } else if (flag == B8_OFFSET) { // Matched Offset Literal 8b length
      DeltaUnitOffset<FlagLengthB8> record;
      memcpy(&record.flag_length, deltaBuf + readLength, sizeof(record.flag_length));
      readLength += sizeof(record.flag_length);
      memcpy(&record.nOffset, deltaBuf + readLength, sizeof(record.nOffset));
      readLength += sizeof(record.nOffset);

      memcpy(outBuf + dataLength, baseBuf + record.nOffset, unit_get_length(&record));
      dataLength += unit_get_length(&record);
    } else if (flag == B8_LITERAL) { // Unmatched Literal 8b length
      DeltaUnit<FlagLengthB8> record;
      memcpy(&record, deltaBuf + readLength, sizeof(DeltaUnit<FlagLengthB8>));
      readLength += sizeof(DeltaUnit<FlagLengthB8>);

      memcpy(outBuf + dataLength, deltaBuf + addDataLength, unit_get_length(&record));
      addDataLength += unit_get_length(&record);
      dataLength += unit_get_length(&record);
    }

    if (readLength >= instructionlenth) {
      break;
    }
  }

  *outSize = dataLength;
  return dataLength;
}

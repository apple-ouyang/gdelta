//
// Created by THL on 2020/7/9.
//

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdint.h>

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
#define LEN_LIMIT ((2 << 13) - 1)
#define SHORT_LEN_LIMIT ((2 << 5) - 1)

#define PRINT_PERF 1

#pragma pack(push, 1)
typedef struct {
  uint8_t flag : 2;
  uint8_t length : 6;
} FlagLengthB8;

typedef struct {
  uint16_t flag : 2;
  uint16_t length : 14;
} FlagLengthB16;

template <typename var> struct DeltaUnit { var flag_length; };

template <typename var> struct DeltaUnitOffset {
  var flag_length;
  uint16_t nOffset;
};


/*
 * ABI:
 *
 * VarInt<N>: more 1 | pval N [more| VarInt<7>]
 * DeltaHead: flag 1 | VarInt<6>
 * DeltaUnit: DeltaHead [DeltaHead.flag| VarInt<7>]
 *
 * VarInt <- Val, Offset = Val | VarInt[i].pval << Offset, Offset + VarInt[i]::N
 */
template <uint8_t FLAGLEN> 
struct _DeltaHead {
  uint8_t flag: FLAGLEN;
  uint8_t more: 1;
  uint8_t length: (7-FLAGLEN);
  const static uint8_t lenbits = FLAGLEN;
};

typedef _DeltaHead<1> DeltaHeadUnit;

typedef struct _VarIntPart {
  uint8_t more: 1;
  uint8_t subint: 7;
  const static uint8_t lenbits = 7;
} VarIntPart;

#pragma pack(pop)

typedef struct {
  uint8_t flag;
  uint64_t length;
  uint64_t offset;
} DeltaUnitMem;

// DeltaUnit/FlaggedVarInt: flag: 1, more: 1, len: 6
// VarInt: more: 1, len: 7
static_assert(sizeof(DeltaHeadUnit) == 1, "Expected DeltaHeads to be 1 byte");
static_assert(sizeof(VarIntPart) == 1, "Expected VarInt to be 1 byte");


static_assert(sizeof(DeltaUnitOffset<FlagLengthB8>) == 3,
              "Expected DeltaUnit<B8> to be 3 bytes");
static_assert(sizeof(DeltaUnitOffset<FlagLengthB16>) == 4,
              "Expected DeltaUnit<B16> to be 4 bytes");
static_assert(sizeof(DeltaUnit<FlagLengthB8>) == 1,
              "Expected DeltaUnit<B8> to be 1 bytes");
static_assert(sizeof(DeltaUnit<FlagLengthB16>) == 2,
              "Expected DeltaUnit<B16> to be 2 bytes");

enum UnitFlag {
  B16_OFFSET = 0b00,
  B8_OFFSET = 0b01,
  B16_LITERAL = 0b10,
  B8_LITERAL = 0b11,

  UF_BITMASK = 0b11
};

template <typename T> inline void unit_set_flag(T *unit, UnitFlag flag) {
  unit->flag_length.flag = flag;
}

template <typename T> inline void unit_set_length(T *unit, uint16_t length) {
  unit->flag_length.length = length;
}

UnitFlag unit_get_flag_raw(uint8_t *record) {
  uint8_t flag = *record & UF_BITMASK;
  return (UnitFlag)flag;
}

template <typename T> inline uint16_t unit_get_length(const T &unit) {
  return unit.flag_length.length;
}

typedef struct {
  uint8_t *buf;
  uint64_t cursor;
  uint64_t length;
} BufferStreamDescriptor;

template <typename T>
void write_field(BufferStreamDescriptor &buffer, const T &field) {
  memcpy(buffer.buf + buffer.cursor, &field, sizeof(T));
  buffer.cursor += sizeof(T);
  // TODO: check bounds (buffer->length)?
}


template <typename T>
void read_field(BufferStreamDescriptor &buffer, T& field) {  
  memcpy(&field, buffer.buf + buffer.cursor, sizeof(T));
  buffer.cursor += sizeof(T);
  // TODO: check bounds (buffer->length)?
}

void stream_into(BufferStreamDescriptor &dest, BufferStreamDescriptor &src, size_t length) {
  memcpy(dest.buf + dest.cursor, src.buf + src.cursor, length);
  dest.cursor += length;
  src.cursor += length;
}

void stream_from(BufferStreamDescriptor &dest, BufferStreamDescriptor &src, size_t src_cursor, size_t length) {
  memcpy(dest.buf + dest.cursor, src.buf + src_cursor, length);
  dest.cursor += length;
}

void write_concat_buffer(BufferStreamDescriptor &dest, const BufferStreamDescriptor &src) {
  memcpy(dest.buf + dest.cursor, src.buf, src.cursor);
  dest.cursor += src.cursor;
}

inline uint64_t read_varint(BufferStreamDescriptor& buffer) {
  VarIntPart vi;
  uint64_t val = 0;
  uint8_t offset = 0;
  do {
    read_field(buffer, vi);
    val |= vi.subint << offset;
    offset += VarIntPart::lenbits;
  } while(vi.more);
  return val;
}

inline void read_unit(BufferStreamDescriptor& buffer, DeltaUnitMem& unit) {
  DeltaHeadUnit head;
  read_field(buffer, head);
 
  unit.flag = head.flag;
  unit.length = head.length;
  if (head.more) {
    unit.length = read_varint(buffer) | (unit.length << DeltaHeadUnit::lenbits);
  }
  if (head.flag) {
    unit.offset = read_varint(buffer);
  }
}

const uint8_t varint_mask = ((2 << VarIntPart::lenbits) -1);
const uint8_t head_varint_mask = ((2 << DeltaHeadUnit::lenbits) -1);
inline void write_varint(BufferStreamDescriptor& buffer, uint64_t val) 
{
  VarIntPart vi;
  do {
    vi.subint = val & varint_mask;
    val >>= VarIntPart::lenbits;
    if (val == 0) {
      write_field(buffer, vi);
      break;
    }
    vi.more = 1;
    write_field(buffer, vi);
  } while (1);
}

void write_unit(BufferStreamDescriptor& buffer, const DeltaUnitMem& unit) {
  // TODO: Abort if length 0?

  DeltaHeadUnit head = {unit.flag, unit.length > head_varint_mask, (uint8_t)(unit.length & head_varint_mask)};
  write_field(buffer, head);

  uint64_t remaining_length = unit.length >> DeltaHeadUnit::lenbits;
  write_varint(buffer, remaining_length);
  if (unit.flag) {
    write_varint(buffer, unit.offset);
  }
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
  int _begsize = begflag ? begsize : 0;
  while (i < numChunks) {
    if (flag == STRLSTEP) {
      flag = 0;
      index = (fingerprint) >> (sizeof(FPTYPE) * 8 - mask);
      if (hash_table[index] == 0) {
        hash_table[index] = i + _begsize;
      } else {
        index = fingerprint >> (sizeof(FPTYPE) * 8 - mask);
        hash_table[index] = i + _begsize;
      }
    }
    /** GEAR **/
    fingerprint = (fingerprint << (movebitlength)) + GEARmx[data[i + STRLOOK]];
    i++;
    flag++;
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

  // Find first difference 
  // First in 8 byte blocks and then in 1 byte blocks for speed
  while (begSize + sizeof(uint64_t) <= baseSize &&
         begSize + sizeof(uint64_t) <= newSize &&
         *(uint64_t *)(baseBuf + begSize) == *(uint64_t *)(newBuf + begSize)) {
    begSize += sizeof(uint64_t);
  }

  while (begSize < baseSize && 
         begSize < newSize && 
         baseBuf[begSize] == newBuf[begSize]) {
    begSize++;
  }

  if (begSize > 16)
    beg = 1;
  else
    begSize = 0;

  // Find first difference (from the end)
  while (endSize + sizeof(uint64_t) <= baseSize &&
         endSize + sizeof(uint64_t) <= newSize &&
         *(uint64_t *)(baseBuf + baseSize - endSize - sizeof(uint64_t)) ==
         *(uint64_t *)(newBuf + newSize - endSize - sizeof(uint64_t))) {
    endSize += sizeof(uint64_t);
  }

  while (endSize < baseSize && 
         endSize < newSize && 
         baseBuf[baseSize - endSize - 1] == newBuf[newSize - endSize - 1]) {
    endSize++;
  }

  if (begSize + endSize > newSize)
    endSize = newSize - begSize;

  if (endSize > 16)
    end = 1;
  else
    endSize = 0;
  /* end of detect */

  BufferStreamDescriptor deltaStream = {deltaBuf, 0, *deltaSize};
  BufferStreamDescriptor instStream = {instbuf, 0, sizeof(instbuf)}; // Instruction stream
  BufferStreamDescriptor dataStream = {databuf, 0, sizeof(databuf)};
  BufferStreamDescriptor newStream = {newBuf, begSize, newSize};
  DeltaUnitMem unit = {}; // In-memory represtation of current working unit

  // TODO: Does this ever hit (duplicate data on each side is higher than original data)?
  if (begSize + endSize >= baseSize) { // TODO: test this path
    /*DeltaUnitOffset<FlagLengthB16> record1;
    DeltaUnit<FlagLengthB16> record2;
    DeltaUnitOffset<FlagLengthB8> record3;
    DeltaUnit<FlagLengthB8> record4;*/

    if (beg) {
      // Data at start is from the original file, write instruction to copy from base
      unit.flag = true;
      unit.offset = 0;
      unit.length = begSize;
      write_unit(instStream, unit);

      /*
      if (begSize < SHORT_LEN_LIMIT) {
        unit_set_flag(&record3, B8_OFFSET);
        record3.nOffset = 0;
        unit_set_length(&record3, begSize);

        write_field(deltaStream, record3.flag_length);
        write_field(deltaStream, record3.nOffset);

        write_field(instStream, record3.flag_length);
        write_field(instStream, record3.nOffset);
      } else if (begSize <= LEN_LIMIT) {
        unit_set_flag(&record1, B16_OFFSET);
        record1.nOffset = 0;
        unit_set_length(&record1, begSize);

        write_field(deltaStream, record1);
        write_field(instStream, record1);
      } else { // TODO: > 16383
        int32_t matchlen = begSize;
        int32_t offset = 0;
        while (matchlen > LEN_LIMIT) {
          unit_set_flag(&record1, B16_OFFSET);
          record1.nOffset = offset;
          unit_set_length(&record1, LEN_LIMIT);
          offset += LEN_LIMIT;
          matchlen -= LEN_LIMIT;
          write_field(instStream, record1);
        }
        if (matchlen) {
          unit_set_flag(&record1, B16_OFFSET);
          record1.nOffset = offset;
          unit_set_length(&record1, matchlen);
          write_field(instStream, record1);
        }
      }
      */
    }
    if (newSize - begSize - endSize > 0) {
      int32_t litlen = newSize - begSize - endSize;
      unit.flag = false;
      unit.length = litlen;
      write_unit(instStream, unit);
      stream_into(dataStream, newStream, litlen);


/*      while (litlen > LEN_LIMIT) {
        unit_set_flag(&record2, B16_LITERAL);
        unit_set_length(&record2, LEN_LIMIT);
        write_field(instStream, record2);
        stream_into(dataStream, newStream, LEN_LIMIT);
        litlen -= LEN_LIMIT;
      }
      if (litlen) {
        unit_set_flag(&record2, B16_LITERAL);
        unit_set_length(&record2, litlen);

        write_field(instStream, record2);
        stream_into(dataStream, newStream, litlen);
      }*/
    }
    if (end) {
      int32_t matchlen = endSize;
      int32_t offset = baseSize - endSize;
      unit.flag = true;
      unit.offset = offset;
      unit.length = matchlen;
      write_unit(instStream, unit);
      /*while (matchlen > LEN_LIMIT) {
        unit_set_flag(&record1, B16_OFFSET);
        record1.nOffset = offset;
        unit_set_length(&record1, LEN_LIMIT);
        offset += LEN_LIMIT;
        matchlen -= LEN_LIMIT;
        write_field(instStream, record1);
      }
      if (matchlen) {
        unit_set_flag(&record1, B16_OFFSET);
        record1.nOffset = offset;
        unit_set_length(&record1, matchlen);
        write_field(instStream, record1);
      }*/
    }

    int32_t instlen = sizeof(uint16_t) * 2 + instStream.cursor;

    // TODO: overwrites BegSize < 64 path in original code??
    // deltaStream.cursor = 0;
    // dataStream.cursor = 0;
    // instStream.cursor = 0;

    uint16_t tmp = instStream.cursor + sizeof(uint16_t);
    write_field(deltaStream, tmp);
    write_concat_buffer(deltaStream, instStream);
    write_concat_buffer(deltaStream, dataStream);

    *deltaSize = sizeof(uint16_t) + instStream.cursor + dataStream.cursor;
    return instlen;
  }

  /* chunk the baseFile */
  int32_t tmp = (baseSize - begSize - endSize) + 10;
  int32_t bit;
  for (bit = 0; tmp; bit++)
    tmp >>= 1;
  uint64_t xxsize = 0XFFFFFFFFFFFFFFFF >> (64 - bit); // mask
  uint32_t hash_size = xxsize + 1;
  uint32_t *hash_table = (uint32_t *)malloc(hash_size * sizeof(uint32_t));
  memset(hash_table, 0, sizeof(uint32_t) * hash_size);

#if PRINT_PERF
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
#endif

  GFixSizeChunking(baseBuf + begSize, baseSize - begSize - endSize, beg,
                   begSize, hash_table, bit);
#if PRINT_PERF
  clock_gettime(CLOCK_MONOTONIC, &t1);

  fprintf(stderr, "size:%d\n", baseSize - begSize - endSize);
  fprintf(stderr, "hash size:%d\n", hash_size);
  fprintf(stderr, "rolling hash:%.3fMB/s\n",
          (double)(baseSize - begSize - endSize) / 1024 / 1024 /
              ((t1.tv_sec - t0.tv_sec) * 1000000000 + t1.tv_nsec - t0.tv_nsec) *
              1000000000);
  fprintf(stderr, "rooling hash:%zd\n",
          (t1.tv_sec - t0.tv_sec) * 1000000000 + t1.tv_nsec - t0.tv_nsec);

  clock_gettime(CLOCK_MONOTONIC, &t0);
  fprintf(stderr, "hash table :%zd\n",
          (t0.tv_sec - t1.tv_sec) * 1000000000 + t0.tv_nsec - t1.tv_nsec);
#endif
  /* end of inserting */

  uint32_t inputPos = begSize;
  uint32_t cursor;
/*  DeltaUnitOffset<FlagLengthB16> record1;
  DeltaUnit<FlagLengthB16> record2;
  DeltaUnitOffset<FlagLengthB8> record3;
  DeltaUnit<FlagLengthB8> record4;
  unit_set_flag(&record1, B16_OFFSET);
  unit_set_flag(&record2, B16_LITERAL);
  unit_set_flag(&record3, B8_OFFSET);
  unit_set_flag(&record4, B8_LITERAL);*/
  //int32_t unmatch64flag = 0;
//  int32_t flag = 0; /* to represent the last record in the deltaBuf, 1 for DeltaUnit1, 2 for DeltaUnit2 */
  int32_t movebitlength = 0;
  if (sizeof(FPTYPE) * 8 % STRLOOK == 0)
    movebitlength = sizeof(FPTYPE) * 8 / STRLOOK;
  else
    movebitlength = sizeof(FPTYPE) * 8 / STRLOOK + 1;
  if (beg) {
    // Data at start is from the original file, write instruction to copy from base
    unit.flag = true;
    unit.offset = 0;
    unit.length = begSize;
    write_unit(instStream, unit);
    unit.length = 0; // Mark as written

    /*if (begSize <= SHORT_LEN_LIMIT) {
      record3.nOffset = 0;
      unit_set_length(&record3, begSize);
      write_field(instStream, record3.flag_length);
      write_field(instStream, record3.nOffset);
      flag = 1;
    } else if (begSize <= LEN_LIMIT) {
      record1.nOffset = 0;
      unit_set_length(&record1, begSize);
      write_field(instStream, record1);
      flag = 1;
    } else {
      int32_t matchlen = begSize;
      int32_t offset = 0;
      flag = 1;
      while (matchlen > LEN_LIMIT) {
        unit_set_flag(&record1, B16_OFFSET);
        record1.nOffset = offset;
        unit_set_length(&record1, LEN_LIMIT);
        offset += LEN_LIMIT;
        matchlen -= LEN_LIMIT;
        write_field(instStream, record1);
      }
      if (matchlen) {
        unit_set_flag(&record1, B16_OFFSET);
        record1.nOffset = offset;
        unit_set_length(&record1, matchlen);
        write_field(instStream, record1);
      }
    }
    */
  }

  FPTYPE fingerprint = 0;
  for (uint32_t i = 0; i < STRLOOK && i < newSize - endSize - inputPos; i++) {
    fingerprint = (fingerprint << (movebitlength)) + GEARmx[(newBuf + inputPos)[i]];
  }

  int32_t matchflag = 0;
  uint32_t handlebytes = begSize;
  while (inputPos + STRLOOK <= newSize - endSize) {
    uint32_t length;
    if (newSize - endSize - inputPos < STRLOOK) {
      cursor = inputPos + (newSize - endSize);
      length = newSize - endSize - inputPos;
    } else {
      cursor = inputPos + STRLOOK;
      length = STRLOOK;
    }
    FPTYPE hash = fingerprint;
    int32_t index1 = hash >> (sizeof(FPTYPE) * 8 - bit);
    uint32_t offset = 0;
    if (hash_table[index1] != 0 && memcmp(newBuf + inputPos, baseBuf + hash_table[index1], length) == 0) {
      matchflag = 1;
      offset = hash_table[index1];
    }

    /* New data match found in hashtable/base data; attempt to create copy instruction*/
    if (matchflag) {
      /*if (flag == B16_LITERAL) {
        instStream.cursor -= sizeof(DeltaUnit<FlagLengthB16>);
        if (unit_get_length(record2) <= SHORT_LEN_LIMIT) {
          unmatch64flag = 1;
          unit_set_length(&record4, unit_get_length(record2));
          write_field(instStream, record4); 
        } else {
          write_field(instStream, record2);
        }
      }*/

      // Check how much is possible to copy
      int32_t j = 0;
      // TODO: check if j can be removed
#if 1 /* 8-bytes optimization */
      while (offset + length + j + 7 < baseSize - endSize &&
             cursor + j + 7 < newSize - endSize && 
             *(uint64_t *)(baseBuf + offset + length + j) ==*(uint64_t *)(newBuf + cursor + j)) {
        j += sizeof(uint64_t);
      }
      while (offset + length + j < baseSize - endSize &&
             cursor + j < newSize - endSize &&
             baseBuf[offset + length + j] == newBuf[cursor + j]) {
        j++;
      }
#endif
      cursor += j;


      int32_t matchlen = cursor - inputPos;
      handlebytes += cursor - inputPos;
      uint64_t _offset = offset;


      // Check if switching modes Literal -> Copy, and dump instruction if available
      if (!unit.flag && unit.length) {
        /* Detect if end of previous literal could have been a partial copy*/
        uint32_t k = 0;
        while (k + 1 <= offset && k + 1 <= unit.length) {
          if (baseBuf[offset - (k + 1)] == newBuf[inputPos - (k + 1)])
            k++;
          else
            break;
        }

        if (k > 0) {
          // Reduce literal by the amount covered by the copy
          unit.length -= k;
          // Set up adjusted copy parameters
          matchlen += k;
          _offset -= k;
          // Last few literal bytes can be overwritten, so move cursor back
          dataStream.cursor -= k;
        }

        write_unit(instStream, unit);
        unit.length = 0; // Mark written
      }

      /* Previous literal could have had some copy, rewrite literal to be partially a copy*/
      /*if (k > 0) {
        dataStream.cursor -= unit_get_length(record2);

        if (unmatch64flag) {
          instStream.cursor -= sizeof(DeltaUnit<FlagLengthB8>);
        } else {
          instStream.cursor -= sizeof(DeltaUnit<FlagLengthB16>);
        }
        unmatch64flag = 0;
        unit_set_length(&record2, unit_get_length(record2) - k);

        if (unit_get_length(record2) > 0) {
          if (unit_get_length(record2) > SHORT_LEN_LIMIT) {
            write_field(instStream, record2);
          } else {
            unit_set_length(&record4, unit_get_length(record2));
            write_field(instStream, record4);
          }

          dataStream.cursor += unit_get_length(record2);
        }

        matchlen += k;
        _offset -= k;
      }*/
    
      unit.flag = true;
      unit.offset = _offset;
      unit.length = matchlen;
      write_unit(instStream, unit);
      unit.length = 0; // Mark written

      /*if (matchlen <= SHORT_LEN_LIMIT) {
        record3.nOffset = record1.nOffset;
        unit_set_length(&record3, matchlen);
        write_field(instStream, record3.flag_length);
        write_field(instStream, record3.nOffset);
      } else if (matchlen <= LEN_LIMIT) {
        unit_set_length(&record1, matchlen);
        write_field(instStream, record1);
      } else {
        offset = record1.nOffset;
        while (matchlen > LEN_LIMIT) {
          record1.nOffset = offset;
          unit_set_length(&record1, LEN_LIMIT);
          offset += LEN_LIMIT;
          matchlen -= LEN_LIMIT;
          write_field(instStream, record1);
        }
        if (matchlen) {
          record1.nOffset = offset;
          unit_set_length(&record1, matchlen);
          write_field(instStream, record1);
        }
      }
      unmatch64flag = 0;
      flag = 1;*/
    } else { // No match, need to write additional (literal) data
      /* 
       * Accumulate length one byte at a time (as literal) in unit while no match is found
       * Pre-emptively write to datastream
       */

      unit.flag = false;
      unit.length += 1;
      stream_from(dataStream, newStream, inputPos, 1);
      handlebytes += 1;

      /*if (flag == B16_LITERAL) {
        if (unit_get_length(record2) < LEN_LIMIT) {
          memcpy(dataStream.buf + dataStream.cursor, newBuf + inputPos, 1);
          dataStream.cursor += 1;
          handlebytes += 1;
          uint16_t lentmp = unit_get_length(record2);
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
      }*/
    }

    // Update cursor (inputPos) and fingerprint
    if (matchflag) {
      for (uint32_t j = cursor; j < cursor + STRLOOK && cursor + STRLOOK < newSize - endSize; j++) {
        fingerprint = (fingerprint << (movebitlength)) + GEARmx[newBuf[j]];
      }

      inputPos = cursor;
    } else {
      // TODO: optimization for fingerprint possible?
      if (inputPos + STRLOOK < newSize - endSize)
        fingerprint = (fingerprint << (movebitlength)) + GEARmx[newBuf[inputPos + STRLOOK]];
      inputPos++;
    }
    matchflag = 0;
  }

  // Dump last unit if not written
  /*if (unit.length) {
    write_unit(instStream, unit);
    unit.length = 0; // Mark written
  }*/

#if PRINT_PERF
  clock_gettime(CLOCK_MONOTONIC, &t1);
  fprintf(stderr, "look up:%zd\n",
          (t1.tv_sec - t0.tv_sec) * 1000000000 + t1.tv_nsec - t0.tv_nsec);
  fprintf(stderr, "look up:%.3fMB/s\n",
          (double)(baseSize - begSize - endSize) / 1024 / 1024 /
              ((t1.tv_sec - t0.tv_sec) * 1000000000 + t1.tv_nsec - t0.tv_nsec) *
              1000000000);
#endif

  // TODO: do something if last unit was literal :thinking:
  // If last unit was unwritten literal, update it to use the rest of the data
  if (!unit.flag && unit.length) {
    newStream.cursor = handlebytes;
    stream_into(dataStream, newStream, newSize - endSize - handlebytes);

    unit.length += (newSize - endSize - handlebytes);
    write_unit(instStream, unit);
    unit.length = 0;
  
 /* if (flag == B16_LITERAL) {
    newStream.cursor = handlebytes;
    stream_into(dataStream, newStream, newSize - endSize - handlebytes);

    int32_t litlen = unit_get_length(record2) + (newSize - endSize - handlebytes);
    if (litlen <= LEN_LIMIT) {
      unit_set_length(&record2, litlen);
      instStream.cursor -= sizeof(record2);
      write_field(instStream, record2);

    } else {
      unit_set_length(&record2, LEN_LIMIT);
      instStream.cursor -= sizeof(record2);
      write_field(instStream, record2);
      unit_set_length(&record2, litlen - LEN_LIMIT);
      write_field(instStream, record2);
    }
*/
  } else { // Last unit was Copy, need new instruction
    if (newSize - endSize - handlebytes) {
      newStream.cursor = inputPos;
      stream_into(dataStream, newStream, newSize - endSize - handlebytes);

      unit.flag = false;
      unit.length = newSize - endSize - handlebytes;
      write_unit(instStream, unit);
      unit.length = 0;

      /*unit_set_length(&record2, newSize - endSize - handlebytes);
      write_field(instStream, record2);*/
    }
  }

  if (end) {
    int32_t matchlen = endSize;
    int32_t offset = baseSize - endSize;
     
    unit.flag = true;
    unit.offset = offset;
    unit.length = matchlen;
    write_unit(instStream, unit);
    unit.length = 0;

/*    while (matchlen > LEN_LIMIT) {
      unit_set_flag(&record1, B16_OFFSET);
      record1.nOffset = offset;
      unit_set_length(&record1, LEN_LIMIT);
      offset += LEN_LIMIT;
      matchlen -= LEN_LIMIT;
      write_field(instStream, record1);
    }
    if (matchlen) {
      record1.nOffset = offset;
      unit_set_length(&record1, matchlen);
      write_field(instStream, record1);
    }*/
  }

  deltaStream.cursor = 0;
  write_field(deltaStream, (uint16_t)(instStream.cursor + sizeof(uint16_t)));
  write_concat_buffer(deltaStream, instStream);
  write_concat_buffer(deltaStream, dataStream);
  *deltaSize = deltaStream.cursor;
  return sizeof(uint16_t) + instStream.cursor; // + dataStream?
}

int gdecode(uint8_t *deltaBuf, uint32_t deltaSize, uint8_t *baseBuf, uint32_t baseSize,
            uint8_t *outBuf, uint32_t *outSize) {

  // TODO: convert instructionLength to VarInt
  uint32_t instructionLength = *(uint16_t*)deltaBuf;
  BufferStreamDescriptor deltaStream = {deltaBuf, sizeof(uint16_t), deltaSize}; // Instructions
  BufferStreamDescriptor addDeltaStream = {deltaBuf, instructionLength, deltaSize}; // Data in 
  BufferStreamDescriptor outStream = {outBuf, 0, *outSize};   // Data out
  BufferStreamDescriptor baseStream = {baseBuf, 0, baseSize}; // Data in
  DeltaUnitMem unit = {};

  while (deltaStream.cursor < instructionLength) {
    read_unit(deltaStream, unit);
    if (unit.flag) // Read from original file using offset
      stream_from(outStream, baseStream, unit.offset, unit.length);
    else          // Read from delta file at current cursor
      stream_into(outStream, addDeltaStream, unit.length);
  }

  *outSize = outStream.cursor;
  return outStream.cursor;
}

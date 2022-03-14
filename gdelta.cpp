//
// Created by THL on 2020/7/9.
//

#include <stdint.h>
#include <cstring>
#include <cstdio>
#include <sys/time.h>
#include "md5.h"
typedef unsigned int u_int32_t;
typedef __uint16_t u_int16_t;
typedef uint8_t u_int8_t;
#define MBSIZE 1024*1024
#define FPTYPE uint64_t

typedef struct        /* the least write or read unit of disk */
{
    uint16_t 	flag_length; //flag & length
    /* the first bit for the flag, the other 31 bits for the length */

    uint16_t 	nOffset;
}FastGeltaUnit1;

/* to represent an  string not identical to the base, 4 bytes, flag=1 */
typedef struct        /* the least write or read unit of disk */
{
    uint16_t 	flag_length; //flag & length
    /* the first bit for the flag, the other 31 bits for the length */
}FastGeltaUnit2;



typedef struct        /* the least write or read unit of disk */
{
    uint8_t 	flag_length; //flag & length
    /* the first bit for the flag, the other 31 bits for the length */

    uint16_t 	nOffset;
}FastGeltaUnit3;

/* to represent an  string not identical to the base, 4 bytes, flag=1 */
typedef struct        /* the least write or read unit of disk */
{
    uint8_t 	flag_length; //flag & length
    /* the first bit for the flag, the other 31 bits for the length */
}FastGeltaUnit4;

void Gfast_set_flagv3(void *record, u_int16_t flag)
{

    if (flag == 0) {
        u_int16_t *flag_length = (u_int16_t *) record;

        (*flag_length) = 0x0000; //00 00 0000 0000 0000
    }
    else if(flag == 2)
    {
        u_int16_t *flag_length = (u_int16_t *) record;
        (*flag_length) = 0x0002; //00 00 0000 0000 0010
    }
    else if(flag == 1)
    {
        u_int8_t *flag_length = (u_int8_t *) record;
        (*flag_length) = 0x01; //0000 0001
    }
    else if(flag == 3)
    {
        u_int8_t *flag_length = (u_int8_t *) record;
        (*flag_length) = 0x03; //0000 0011
    }

}

void Gfast_set_lengthv3(void *record, u_int16_t length,u_int16_t flag)
{
    if(flag == 0 || flag==2)
    {
        u_int16_t *flag_length = (u_int16_t *) record;
        u_int16_t musk = (*flag_length) & 0x0003; //0000 0000 0000 0011
        *flag_length = (length << 2) | musk;
        u_int16_t tmp = *flag_length;

    }
    else{
        u_int8_t *flag_length = (u_int8_t *) record;
        u_int8_t musk = (*flag_length) & 0x03 ; //0000 0011
        *flag_length = (length<<2) | musk;
        u_int8_t tmp = *flag_length;
    }
}
u_int8_t Gfast_get_flagv3(void *record)
{
    u_int8_t *flag_length = (u_int8_t *) record ; //大小端的问题？？？要加1反正，2字节存进去,现在不用了
    u_int8_t tmp1 = *flag_length ;
    u_int8_t flag = tmp1   & 0x03; //0 0000 0011
    return flag;  //0000 0011
}

u_int16_t Gfast_get_lengthv3(void *record)
{
    int flag = Gfast_get_flagv3(record);

    if(flag==0 || flag==2)
    {
        u_int16_t *flag_length = (u_int16_t *) record;
        u_int16_t tmp1 = *flag_length;
        //u_int16_t mask= 0x3FFF; //      0011 1111 1111 1111
        u_int16_t tmp = tmp1 >> 2;
        return tmp;
    }
    else
    {
        u_int8_t *flag_length = (u_int8_t *) record;
        u_int8_t tmp1 = *flag_length;
        //  u_int8_t mask= 0x3F; // 0011 1111
        u_int8_t tmp = tmp1 >> 2;
        return tmp;
    }
}

#define STRLOOK 16
#define STRLSTEP 2

uint64_t GEARmx[256];

void initematrix()
{
    const uint32_t SymbolTypes = 256;
    const uint32_t MD5Length = 16;
    const int SeedLength = 64;

    char seed[SeedLength];
    for (uint32_t i = 0; i < SymbolTypes; i++) {
        for (int j = 0; j < SeedLength; j++) {
            seed[j] = i;
        }

        GEARmx[i] = 0;
        char md5_result[MD5Length];
        md5_state_t md5_state;
        md5_init(&md5_state);
        md5_append(&md5_state, (md5_byte_t *) seed, SeedLength);
        md5_finish(&md5_state, (md5_byte_t *) md5_result);

        memcpy(&GEARmx[i], md5_result, sizeof(uint64_t));
    }
}


int GFixSizeChunking(unsigned char *data, int len, int begflag, int begsize, u_int32_t *hash_table, int mask) {

    if (len < STRLOOK) return 0;
    int i = 0;
    int movebitlength = 0;
    if (sizeof(FPTYPE)*8 % STRLOOK == 0) movebitlength = sizeof(FPTYPE)*8 / STRLOOK;
    else movebitlength = sizeof(FPTYPE)*8 / STRLOOK + 1;
    FPTYPE fingerprint = 0;

    /** GEAR **/

    for (; i < STRLOOK; i++) {
        fingerprint = (fingerprint << (movebitlength)) + GEARmx[data[i]];
    }



    i -= STRLOOK;
    FPTYPE index = 0;
    int numChunks = len - STRLOOK + 1;

    float hashnum = (float)numChunks/STRLSTEP;
    float coltime = 0;
    float sametime = 0;
    float colratio = 0;

    int flag = 0;
    if (begflag) {
        while (i < numChunks) {
            if (flag == STRLSTEP) {
                flag = 0;
                index = (fingerprint  ) >> (sizeof(FPTYPE)*8 - mask);
                if(hash_table[index] == 0)
                {
                    hash_table[index] = i + begsize;
                }
                else
                {
                    index = fingerprint >> (sizeof(FPTYPE)*8 - mask);
                    hash_table[index] = i + begsize;

                }

            }
            /** GEAR **/

            fingerprint = (fingerprint << (movebitlength)) + GEARmx[data[i + STRLOOK]];



            i++;
            flag++;
        }
    } else {
        while (i < numChunks) {
            if (flag == STRLSTEP) {

                flag = 0;

                index = (fingerprint ) >> (sizeof(FPTYPE)*8 - mask);
                if(hash_table[index]==0)
                {
                    hash_table[index] = i;

                }
                else
                {
                    index = fingerprint >> (sizeof(FPTYPE)*8 - mask);
                    hash_table[index] = i;

                }

            }
            /** GEAR **/

            fingerprint = (fingerprint << (movebitlength)) + GEARmx[data[i + STRLOOK]];


            i++;
            flag++;

        }
    }

    return 0;
}

int gencode(uint8_t *newBuf, u_int32_t newSize,
                      uint8_t *baseBuf, u_int32_t baseSize,
                      uint8_t *deltaBuf, u_int32_t *deltaSize) {
    /* detect the head and tail of one chunk */

    u_int32_t beg = 0, end = 0, begSize = 0, endSize = 0;
    u_int32_t data_length=0;
    u_int32_t inst_length=0;
    uint8_t databuf[MBSIZE];
    uint8_t instbuf[MBSIZE];
    if(newSize>=64*1024 || baseSize>=64*1024)
    {
        printf("Gdelta not support size >= 64KB.\n");
    }


    while (begSize + 7 < baseSize && begSize + 7 < newSize) {
        if (*(uint64_t *) (baseBuf + begSize) == \
            *(uint64_t *) (newBuf + begSize)) {
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
        if (*(uint64_t *) (baseBuf + baseSize - endSize - 8) == \
            *(uint64_t *) (newBuf + newSize - endSize - 8)) {
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
        FastGeltaUnit1 record1;
        FastGeltaUnit2 record2;

        FastGeltaUnit3 record3;
        FastGeltaUnit4 record4;

        u_int32_t deltaLen = 0;
        if (beg) {

            if(begSize < 64)
            {
                Gfast_set_flagv3(&record3, 1);
                record3.nOffset = 0;
                Gfast_set_lengthv3(&record3, begSize,1);

                memcpy(deltaBuf + deltaLen, &record3.flag_length,1);
                deltaLen+=1;
                memcpy(deltaBuf + deltaLen, &record3.nOffset,2);
                deltaLen +=2;
                memcpy(instbuf + inst_length, &record3.flag_length, 1);
                inst_length +=1;
                memcpy(instbuf + inst_length, &record3.nOffset, 2);
                inst_length +=2;
            }
            else if(begSize <16384)
            {
                Gfast_set_flagv3(&record1, 0);
                record1.nOffset = 0;
                Gfast_set_lengthv3(&record1, begSize,0);

                memcpy(deltaBuf + deltaLen, &record1, sizeof(FastGeltaUnit1));
                memcpy(instbuf + inst_length, &record1, sizeof(FastGeltaUnit1));
                deltaLen += sizeof(FastGeltaUnit1);
                inst_length += sizeof(FastGeltaUnit1);
            }
            else{ //TODO: > 16383

                int matchlen = begSize;
                int offset = 0;
                while(matchlen > 16383)
                {
                    Gfast_set_flagv3(&record1, 0);
                    record1.nOffset = offset;
                    Gfast_set_lengthv3(&record1, 16383,0);
                    offset   += 16383;
                    matchlen -= 16383;
                    memcpy(instbuf + inst_length, &record1, sizeof(FastGeltaUnit1));
                    inst_length += sizeof(FastGeltaUnit1);
                }
                if(matchlen)
                {
                    Gfast_set_flagv3(&record1, 0);
                    record1.nOffset = offset;
                    Gfast_set_lengthv3(&record1, matchlen,0);
                    memcpy(instbuf + inst_length, &record1, sizeof(FastGeltaUnit1));
                    inst_length += sizeof(FastGeltaUnit1);
                }

            }
        }
        if (newSize - begSize - endSize > 0) {

            int litlen = newSize - begSize -endSize;
            int copylen = 0;
            while(litlen > 16383)
            {
                Gfast_set_flagv3(&record2, 2);
                Gfast_set_lengthv3(&record2, 16383,2);
                memcpy(instbuf + inst_length, &record2, sizeof(FastGeltaUnit2));
                inst_length += sizeof(FastGeltaUnit2);
                memcpy(databuf + data_length, newBuf + begSize + copylen, 16383);
                litlen       -= 16383;
                data_length  += 16383;
                copylen      += 16383;
            }
            if(litlen)
            {
                Gfast_set_flagv3(&record2, 2);
                Gfast_set_lengthv3(&record2, litlen,2);

                memcpy(instbuf + inst_length, &record2, sizeof(FastGeltaUnit2));
                inst_length += sizeof(FastGeltaUnit2);
                memcpy(databuf + data_length, newBuf + begSize + copylen, litlen);
                data_length += litlen;
            }



        }
        if (end) {
            int matchlen = endSize;
            int offset = baseSize - endSize;
            while(matchlen > 16383)
            {
                Gfast_set_flagv3(&record1, 0);
                record1.nOffset = offset;
                Gfast_set_lengthv3(&record1, 16383,0);
                offset   += 16383;
                matchlen -= 16383;
                memcpy(instbuf + inst_length, &record1, sizeof(FastGeltaUnit1));
                inst_length += sizeof(FastGeltaUnit1);
            }
            if(matchlen)
            {
                Gfast_set_flagv3(&record1, 0);
                record1.nOffset = offset;
                Gfast_set_lengthv3(&record1, matchlen,0);
                memcpy(instbuf + inst_length, &record1, sizeof(FastGeltaUnit1));
                inst_length +=sizeof(FastGeltaUnit1);
            }
        }

        int instlen = 0;
        if(1)
        {

            deltaLen = 0;
            u_int16_t tmp = inst_length + sizeof(u_int16_t);
            instlen += sizeof(u_int16_t);

            memcpy(deltaBuf+deltaLen,&tmp, sizeof(u_int16_t));
            deltaLen += sizeof(u_int16_t) ;

            memcpy(deltaBuf+deltaLen,instbuf,inst_length);
            deltaLen += inst_length ;
            instlen  += inst_length;
            memcpy(deltaBuf+deltaLen,databuf,data_length);
            deltaLen += data_length;
        }
        else{
            printf("wrong instruction and data \n");
        }

        *deltaSize = deltaLen;

        return instlen;
    }

    /* chunk the baseFile */


    u_int32_t deltaLen = 0;


    struct timeval t0, t1;


    int numBase = 0;
    int tmp = (baseSize - begSize - endSize) + 10;

    int bit;
    for (bit = 0; tmp; bit++) {
        tmp >>= 1;
    }


    uint64_t xxsize = 0XFFFFFFFFFFFFFFFF >> (64 - bit); //mask


    //uint32_t hash_size=0XFFFFFFFF>>(32-tmp);
    uint32_t hash_size = xxsize + 1;


//    u_int32_t *hash_table = (u_int32_t *) malloc(sizeof(u_int32_t) * hash_size);
//    memset(hash_table, 0, sizeof(u_int32_t) * hash_size);
    u_int32_t hash_table[hash_size] ;

    memset(hash_table, 0, sizeof(u_int32_t) * hash_size);
    FPTYPE mask = xxsize;
    gettimeofday(&t0, NULL);

//    vector<int> auxvec;
    numBase = GFixSizeChunking(baseBuf + begSize, baseSize - begSize - endSize, beg, begSize, hash_table, bit);
    //mask=hash_size;
    gettimeofday(&t1, NULL);

//    printf("size:%d\n",baseSize - begSize - endSize);
//    printf("hash size:%d\n",hash_size);
//    printf("rolling hash:%.3fMB/s\n", (double)(baseSize - begSize - endSize)/1024/1024/((t1.tv_sec-t0.tv_sec) *1000000 + t1.tv_usec - t0.tv_usec)*1000000);
//    printf("rooling hash:%lu\n", (t1.tv_sec-t0.tv_sec) *1000000 + t1.tv_usec - t0.tv_usec);

    gettimeofday(&t0, NULL);
    //printf("hash table :%lu\n", (t0.tv_sec-t1.tv_sec) *1000000 + t0.tv_usec - t1.tv_usec);
    /* end of inserting */

    u_int32_t inputPos = begSize;
    u_int32_t writepos;
    u_int32_t cursor;
    u_int32_t length;
    FPTYPE hash;
    //DeltaRecord *psDupSubCnk = NULL;
    FastGeltaUnit1 record1;
    FastGeltaUnit2 record2;
    FastGeltaUnit3 record3;
    FastGeltaUnit4 record4;
    Gfast_set_flagv3(&record1, 0);
    Gfast_set_flagv3(&record2, 2);
    Gfast_set_flagv3(&record3, 1);
    Gfast_set_flagv3(&record4, 3);
    int unmatch64flag = 0;
    int flag = 0;/* to represent the last record in the deltaBuf,
	1 for DeltaUnit1, 2 for DeltaUnit2 */

    int movebitlength = 0;
    if (sizeof(FPTYPE)*8 % STRLOOK == 0) movebitlength = sizeof(FPTYPE)*8 / STRLOOK;
    else movebitlength = sizeof(FPTYPE)*8 / STRLOOK + 1;
    if (beg) {
        if(begSize < 64)
        {
            record3.nOffset = 0;
            Gfast_set_lengthv3(&record3, begSize,1);
            memcpy(instbuf + inst_length, &record3.flag_length, 1);
            inst_length +=1;
            memcpy(instbuf + inst_length, &record3.nOffset, 2);
            inst_length +=2;
            flag = 1;
        }
        else if(begSize < 16384)
        {
            record1.nOffset = 0;
            Gfast_set_lengthv3(&record1, begSize,0);
            memcpy(instbuf + inst_length, &record1, sizeof(FastGeltaUnit1));
            inst_length +=sizeof(FastGeltaUnit1);
            flag = 1;
        }
        else
        {
            int matchlen = begSize;
            int offset = 0;
            flag = 1;
            while(matchlen > 16383)
            {
                Gfast_set_flagv3(&record1, 0);
                record1.nOffset = offset;
                Gfast_set_lengthv3(&record1, 16383,0);
                offset   += 16383;
                matchlen -= 16383;
                memcpy(instbuf + inst_length, &record1, sizeof(FastGeltaUnit1));
                inst_length += sizeof(FastGeltaUnit1);
            }
            if(matchlen)
            {
                Gfast_set_flagv3(&record1, 0);
                record1.nOffset = offset;
                Gfast_set_lengthv3(&record1, matchlen,0);
                memcpy(instbuf + inst_length, &record1, sizeof(FastGeltaUnit1));
                inst_length += sizeof(FastGeltaUnit1);
            }
        }

    }

    FPTYPE fingerprint = 0;
    for (u_int32_t i = 0; i < STRLOOK && i < newSize - endSize - inputPos; i++) {
        fingerprint = (fingerprint << (movebitlength)) + GEARmx[(newBuf + inputPos)[i]];
    }

    int mathflag = 0;
    writepos = inputPos;
    uint32_t handlebytes = begSize;
    int find2 = 0;
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
        int index1 = hash >> (sizeof(FPTYPE)*8 - bit);

        int offset;
        int index;
        if(hash_table[index1] != 0 && memcmp(newBuf + inputPos,baseBuf + hash_table[index1],length) == 0)
        {
            mathflag = 1;
            find1++;
            index = index1;
            offset = hash_table[index];
        }


        /* lookup */
        if (mathflag) {
            if (1) {
                if (flag == 2) {

                    if(Gfast_get_lengthv3(&record2)<=63)
                    {
                        unmatch64flag = 1;
                        Gfast_set_lengthv3(&record4,Gfast_get_lengthv3(&record2),3);
                        memcpy(instbuf + inst_length  - sizeof(FastGeltaUnit2),
                               &record4, sizeof(FastGeltaUnit4));

                        inst_length -= 1;
                    }
                    else{
                        memcpy(instbuf + inst_length  - sizeof(FastGeltaUnit2),
                               &record2, sizeof(FastGeltaUnit2));
                    }

                }

                int j = 0;
                mathflag = 1;
#if 1 /* 8-bytes optimization */
                while (offset + length + j + 7 < baseSize - endSize &&
                       cursor + j + 7 < newSize - endSize) {
                    if (*(uint64_t *) (baseBuf + offset + length + j) == \
                        *(uint64_t *) (newBuf + cursor + j)) {
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

//TODO:!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Cancel fast_set_lengthv3(&record1, cursor - inputPos,0);


                int matchlen = cursor - inputPos;
                handlebytes += cursor - inputPos;
                record1.nOffset = offset;

                /* detect backward */
                int k = 0;
                if (flag == 2) {
                    while (k + 1 <= offset && k + 1 <= Gfast_get_lengthv3(&record2)) {
                        if (baseBuf[offset - (k + 1)] == newBuf[inputPos - (k + 1)])
                            k++;
                        else
                            break;
                    }
                }

                if (k > 0) {

//                    deltaLen -= fast_get_lengthv3(&record2);
                    data_length -= Gfast_get_lengthv3(&record2);

//                    deltaLen -= sizeof(FastDeltaUnit2);

                    if(unmatch64flag)
                    {
                        inst_length -= sizeof(FastGeltaUnit4);
                    }
                    else
                    {
                        inst_length -= sizeof(FastGeltaUnit2);
                    }
                    unmatch64flag = 0;

                    Gfast_set_lengthv3(&record2, Gfast_get_lengthv3(&record2) - k,2);

                    if (Gfast_get_lengthv3(&record2) > 0) {

                        if(Gfast_get_lengthv3(&record2) >= 64)
                        {
                            memcpy(instbuf + inst_length, &record2, sizeof(FastGeltaUnit2));
                            inst_length += sizeof(FastGeltaUnit2);
                        }
                        else
                        {
                            Gfast_set_lengthv3(&record4, Gfast_get_lengthv3(&record2) ,4);
                            memcpy(instbuf + inst_length, &record4, sizeof(FastGeltaUnit4));
                            inst_length += sizeof(FastGeltaUnit4);
                        }

                        data_length += Gfast_get_lengthv3(&record2);
                    }


                    matchlen += k;
                    record1.nOffset -= k;
                }


                if(matchlen < 64)
                {
                    record3.nOffset = record1.nOffset;
                    Gfast_set_lengthv3(&record3,matchlen,1);
                    memcpy(instbuf + inst_length, &record3.flag_length, 1);
                    inst_length += 1;
                    memcpy(instbuf + inst_length, &record3.nOffset, 2);
                    inst_length += 2;
                }
                else if(matchlen < 16384)
                {
                    Gfast_set_lengthv3(&record1, matchlen,0);
                    memcpy(instbuf + inst_length, &record1, sizeof(FastGeltaUnit1));
                    inst_length += sizeof(FastGeltaUnit1);
                }
                else
                {
                    offset = record1.nOffset;
                    while(matchlen > 16383)
                    {
                        record1.nOffset = offset;
                        Gfast_set_lengthv3(&record1, 16383,0);
                        offset   += 16383;
                        matchlen -= 16383;
                        memcpy(instbuf + inst_length, &record1, sizeof(FastGeltaUnit1));
                        inst_length += sizeof(FastGeltaUnit1);
                    }
                    if(matchlen)
                    {
                        record1.nOffset = offset;
                        Gfast_set_lengthv3(&record1, matchlen,0);
                        memcpy(instbuf + inst_length, &record1, sizeof(FastGeltaUnit1));
                        inst_length += sizeof(FastGeltaUnit1);
                    }

                }
                unmatch64flag = 0;
                flag = 1;
            } else {
                //printf("Spooky Hash Error!!!!!!!!!!!!!!!!!!\n");
                goto handle_hash_error;
            }
        } else {
            handle_hash_error:
            if (flag == 2) {

                if(Gfast_get_lengthv3(&record2)<16383)
                {
                    memcpy(databuf + data_length, newBuf + inputPos, 1);
                    data_length += 1;
                    handlebytes += 1;
                    u_int16_t lentmp = Gfast_get_lengthv3(&record2);
                    Gfast_set_lengthv3(&record2, lentmp + 1,2);
                }
                else
                {
                    memcpy(instbuf + inst_length  - sizeof(FastGeltaUnit2),&record2, sizeof(FastGeltaUnit2));
                    handlebytes += 1;
                    Gfast_set_lengthv3(&record2, 1,2);
                    memcpy(instbuf + inst_length, &record2, sizeof(FastGeltaUnit2));
                    inst_length += sizeof(FastGeltaUnit2);
                    memcpy(databuf + data_length, newBuf + inputPos, 1);
                    data_length +=1;

                }

            } else {
                handlebytes += 1;
                Gfast_set_lengthv3(&record2, 1,2);
                memcpy(instbuf + inst_length, &record2, sizeof(FastGeltaUnit2));
                inst_length += sizeof(FastGeltaUnit2);
                memcpy(databuf + data_length, newBuf + inputPos, 1);
                data_length +=1;
                flag = 2;
            }
        }
        if (mathflag) {
//            for (int j = inputPos + STRLOOK; j < cursor + STRLOOK && cursor + STRLOOK < newSize - endSize; j++) {
//                fingerprint = (fingerprint << (movebitlength)) + GEAR[newBuf[j]];
//            }
            for (u_int32_t j = cursor ; j < cursor + STRLOOK && cursor + STRLOOK < newSize - endSize; j++) {
                fingerprint = (fingerprint << (movebitlength)) + GEARmx[newBuf[j]];
            }


            inputPos = cursor;
        } else {

            if (inputPos + STRLOOK < newSize - endSize) fingerprint = (fingerprint << (movebitlength)) +
                                                                      GEARmx[newBuf[inputPos + STRLOOK]];
            inputPos++;
        }
        mathflag = 0;
//        printf("datalen:%d\n",data_length);
    }

    gettimeofday(&t1, NULL);
//    printf("look up :%lu\n", (t1.tv_sec-t0.tv_sec) *1000000 + t1.tv_usec - t0.tv_usec);
//    printf("look up:%.3fMB/s\n", (double)(baseSize - begSize - endSize)/1024/1024/((t1.tv_sec-t0.tv_sec) *1000000 + t1.tv_usec - t0.tv_usec)*1000000);
    if (flag == 2) {

//        memcpy(deltaBuf + deltaLen, newBuf + handlebytes, newSize - endSize - handlebytes);
        memcpy(databuf + data_length, newBuf + handlebytes, newSize - endSize - handlebytes);
//        deltaLen += newSize - endSize - handlebytes;
        data_length += (newSize - endSize - handlebytes);

        int litlen = Gfast_get_lengthv3(&record2) + (newSize - endSize - handlebytes);
        if(litlen <16384)
        {
            Gfast_set_lengthv3(&record2, litlen,2);
            memcpy(instbuf + inst_length - sizeof(FastGeltaUnit2),
                   &record2, sizeof(FastGeltaUnit2));
        }
        else
        {
            Gfast_set_lengthv3(&record2, 16383,2);
            memcpy(instbuf + inst_length - sizeof(FastGeltaUnit2),
                   &record2, sizeof(FastGeltaUnit2));
            Gfast_set_lengthv3(&record2, litlen -16383,2);
            memcpy(instbuf + inst_length, &record2, sizeof(FastGeltaUnit2));
            inst_length += sizeof(FastGeltaUnit2);

        }



    } else {
        if (newSize - endSize - handlebytes) {

            Gfast_set_lengthv3(&record2, newSize - endSize - handlebytes,2);

//            memcpy(deltaBuf + deltaLen, &record2, sizeof(FastDeltaUnit2));
            memcpy(instbuf+ inst_length, &record2, sizeof(FastGeltaUnit2));
//            deltaLen += sizeof(FastDeltaUnit2);
            inst_length += sizeof(FastGeltaUnit2);

//            memcpy(deltaBuf + deltaLen, newBuf + inputPos, newSize - endSize - handlebytes);
            memcpy(databuf + data_length, newBuf + inputPos, newSize - endSize - handlebytes);
//            deltaLen += newSize - endSize - handlebytes;
            data_length += newSize - endSize - handlebytes;
        }
    }

    if (end) {
        int matchlen = endSize;
        int offset = baseSize - endSize;
        while(matchlen > 16383)
        {
            Gfast_set_flagv3(&record1, 0);
            record1.nOffset = offset;
            Gfast_set_lengthv3(&record1, 16383,0);
            offset   += 16383;
            matchlen -= 16383;
            memcpy(instbuf + inst_length, &record1, sizeof(FastGeltaUnit1));
            inst_length += sizeof(FastGeltaUnit1);
        }
        if(matchlen)
        {
            record1.nOffset = offset;
            Gfast_set_lengthv3(&record1, matchlen,0);
            memcpy(instbuf + inst_length, &record1, sizeof(FastGeltaUnit1));
            inst_length +=sizeof(FastGeltaUnit1);
        }
    }
    int inslen = 0;

    if(1)
    {

        deltaLen = 0;
        tmp = inst_length + sizeof(u_int16_t);
        memcpy(deltaBuf+deltaLen,&tmp, sizeof(u_int16_t));
        deltaLen += sizeof(u_int16_t) ;
        inslen  += sizeof(u_int16_t) ;
        memcpy(deltaBuf+deltaLen,instbuf,inst_length);
        deltaLen += inst_length ;
        inslen   += inst_length ;
        memcpy(deltaBuf+deltaLen,databuf,data_length);
        deltaLen += data_length;
    }
    else{
        printf("wrong instruction and data \n");
    }

    *deltaSize = deltaLen;
    return inslen;
}


int gdecode( uint8_t* deltaBuf,u_int32_t deltaSize,
                          uint8_t* baseBuf, u_int32_t baseSize,
                          uint8_t* outBuf,   u_int32_t *outSize)
{

    /* datalength is the cursor of outBuf, and readLength deltaBuf */
    u_int32_t dataLength = 0, readLength = sizeof(u_int16_t);

    u_int32_t addatalenth = 0;
    memcpy(&addatalenth, deltaBuf , sizeof(uint16_t));
    u_int32_t  instructionlenth = addatalenth;


    int matchnum = 0;
    u_int32_t matchlength = 0;
    u_int32_t unmatchlength = 0;
    int unmatchnum = 0;
    while (1) {
        u_int16_t flag = Gfast_get_flagv3(deltaBuf + readLength);

        if (flag == 0) {
            matchnum++;
            FastGeltaUnit1 record;
            memcpy(&record, deltaBuf + readLength, sizeof(FastGeltaUnit1));

            readLength += sizeof(FastGeltaUnit1);

            matchlength += Gfast_get_lengthv3(&record);

            memcpy(outBuf + dataLength, baseBuf + record.nOffset, Gfast_get_lengthv3(&record));

            //printf("match length:%d\n",get_length(&record));
            dataLength += Gfast_get_lengthv3(&record);
        }
        else if(flag==2)
        {
            unmatchnum++;
            FastGeltaUnit2 record;
            memcpy(&record, deltaBuf + readLength, sizeof(FastGeltaUnit2));

            readLength += sizeof(FastGeltaUnit2);

            unmatchlength += Gfast_get_lengthv3(&record);

            memcpy(outBuf + dataLength, deltaBuf + addatalenth, Gfast_get_lengthv3(&record));

            //printf("unmatch length:%d\n",get_length(&record));
            addatalenth += Gfast_get_lengthv3(&record);
            dataLength += Gfast_get_lengthv3(&record);
        }
        else if (flag == 1)
        {

            matchnum++;
            FastGeltaUnit3 record;
            memcpy(&record.flag_length, deltaBuf + readLength,1);
            readLength += 1;
            memcpy(&record.nOffset, deltaBuf + readLength,2);
            readLength += 2;

            //printf("offset: %d\n",record.nOffset);



            matchlength += Gfast_get_lengthv3(&record);

            memcpy(outBuf + dataLength, baseBuf + record.nOffset, Gfast_get_lengthv3(&record));

            //printf("match length:%d\n",get_length(&record));
            dataLength += Gfast_get_lengthv3(&record);
        }
        else if(flag == 3)
        {
            unmatchnum++;
            FastGeltaUnit4 record;
            memcpy(&record, deltaBuf + readLength, sizeof(FastGeltaUnit4));

            readLength += sizeof(FastGeltaUnit4);



            memcpy(outBuf + dataLength, deltaBuf + addatalenth, Gfast_get_lengthv3(&record));

            //printf("unmatch length:%d\n",get_length(&record));
            addatalenth += Gfast_get_lengthv3(&record);
            dataLength += Gfast_get_lengthv3(&record);
            unmatchlength += Gfast_get_lengthv3(&record);
        }


        if (readLength  >= instructionlenth) {
            break;
        }

    }

    //printf("decode data len = %d.\r\n", dataLength);


    *outSize = dataLength;
    return dataLength;
}
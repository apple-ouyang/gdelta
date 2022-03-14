//
// Created by lenovo on 2019/12/10.
//

#include "cstring"
#include "gdelta.h"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#define MBSIZE 1024 * 1024

char *InputFile = (char *)"/home/thl/data/chunk/3177803";
char *BaseFile = (char *)"/home/thl/data/chunk/3180462";

int man() {
  uint8_t *inp = (uint8_t *)malloc(10 * MBSIZE); // input
  uint8_t *bas = (uint8_t *)malloc(10 * MBSIZE);
  uint8_t *del = (uint8_t *)malloc(10 * MBSIZE);
  uint8_t *res = (uint8_t *)malloc(10 * MBSIZE);
  int F1 = open("/home/thl/test/deltabuf", O_RDONLY, 0777);
  if (F1 <= 0) {
    printf("OpenFile Fail:%s\n", "/home/thl/test/deltabuf");
    exit(0);
  }
  int F2 = open("/home/thl/test/basebuf", O_RDONLY, 0777);
  if (F2 <= 0) {
    printf("OpenFile Fail:%s\n", "/home/thl/test/basebuf");
    exit(0);
  }
  int F3 = open("/home/thl/test/truebuf", O_RDONLY, 0777);
  if (F3 <= 0) {
    printf("OpenFile Fail:%s\n", "/home/thl/test/truebuf");
    exit(0);
  }

  uint32_t target_size = 0, origin_size = 0, delta_size = 0, rsize = 0;

  while (int chunkLen = read(F1, (char *)(del + delta_size), MBSIZE)) {
    delta_size += chunkLen;
  }
  while (int chunkLen = read(F2, (char *)(bas + origin_size), MBSIZE)) {
    origin_size += chunkLen;
  }
  while (int chunkLen = read(F3, (char *)(inp + target_size), MBSIZE)) {
    target_size += chunkLen;
  }
  printf("delta:%d\n", delta_size);
  printf("Base:%d\n", origin_size);
  printf("target_size:%d\n", target_size);
  int r2 = gdecode((uint8_t *)del, (uint32_t)delta_size, (uint8_t *)bas,
                   (uint32_t)origin_size, (uint8_t *)res, (uint32_t *)&rsize);

  if (target_size != rsize) {
    printf("restore size (%d) ERROR!\r\n", rsize);
  }

  for (uint32_t i = 0; i < rsize; i++) {
    if (inp[i] != res[i]) {
      printf("xx:%d\n", i);
      exit(0);
    }
  }
  if (memcmp(inp, res, target_size) != 0) {
    printf("delta error!!!\n");
  }

  return 0;
}

int load_file_to_memory(const char *filename, uint8_t **result) {
  FILE *f = fopen(filename, "rb");
  if (f == NULL) {
    *result = NULL;
    return -1; // -1 means file opening fail
  }
  fseek(f, 0, SEEK_END);
  size_t size = ftell(f);
  fseek(f, 0, SEEK_SET);
  *result = (uint8_t *)malloc(size + 1);
  if (size != fread(*result, sizeof(char), size, f)) {
    free(*result);
    return -2; // -2 means file reading fail
  }
  fclose(f);
  (*result)[size] = 0;
  return size;
}

int main(int argc, char *argv[]) {
  uint8_t edflags = 0 while ((c = getopt(argc, argv, "edo:")) != -1) {
    switch (c) {
    case 'd':
      edflags |= 0b01;
      break;
    case 'e':
      edflags |= 0b10;
      break;
    case 'o':
      cvalue = optarg;
      break;
    case '?':
      if (optopt == 'o')
        fprintf(stderr, "Option -%o requires an argument.\n", optopt);
      else if (isprint(optopt))
        fprintf(stderr, "Unknown option `-%o'.\n", optopt);
      else
        fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
      return 1;
    default:
      abort();
    }
  }

  if (edflags > 2 || edflags == 0) {
    fprintf(stderr, "Usage: gdelta [-d|-e] <basefile> <delta|target-file> [-o "
                    "<outputfile> | -c]\n");
    return 1;
  }

  // TODO: detect base/delta/target file specified
  // TODO: load each file, malloc intermediate buffer, write to outputfile/stdout if specified

  uint8_t *target = (uint8_t *)malloc(10 * MBSIZE); // input
  uint8_t *origin = (uint8_t *)malloc(10 * MBSIZE);
  uint8_t *delta = (uint8_t *)malloc(10 * MBSIZE);
  uint8_t *res = (uint8_t *)malloc(10 * MBSIZE);
  int F1 = open(InputFile, O_RDONLY, 0777);
  if (F1 <= 0) {
    printf("OpenFile Fail:%s\n", InputFile);
    exit(0);
  }
  int F2 = open(BaseFile, O_RDONLY, 0777);
  if (F2 <= 0) {
    printf("OpenFile Fail:%s\n", BaseFile);
    exit(0);
  }
  uint32_t target_size = 0, origin_size = 0, delta_size = 0, rsize = 0;

  while (int chunkLen = read(F1, (char *)(target + target_size), MBSIZE)) {
    target_size += chunkLen;
  }

  while (int chunkLen = read(F2, (char *)(origin + origin_size), MBSIZE)) {
    origin_size += chunkLen;
  }

  printf("Input:%d\n", target_size);
  printf("Base:%d\n", origin_size);
  delta_size = 0;

  int b = gencode((uint8_t *)target, target_size, (uint8_t *)origin,
                  origin_size, (uint8_t *)delta, (uint32_t *)&delta_size);

  printf("delta_size:%d\n", delta_size);

  int r2 = gdecode((uint8_t *)delta, (uint32_t)delta_size, (uint8_t *)origin,
                   (uint32_t)origin_size, (uint8_t *)res, (uint32_t *)&rsize);

  if (target_size != rsize) {
    printf("restore size (%d) ERROR!\r\n", rsize);
  }

  if (memcmp(target, res, target_size) != 0) {
    printf("delta error!!!\n");
  }
  FILE *F4 = fopen("/home/thl/test/rebuf", "w");
  fwrite(res, 1, rsize, F4);
  fclose(F4);

  return 0;
}

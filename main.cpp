//
// Created by lenovo on 2019/12/10.
//

#include "cstring"
#include "gdelta.h"
#include <cstdio>
#include <cstdlib>
#include <ctype.h>
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
  uint8_t edflags = 0;
  int c;
  char *cvalue = nullptr;
  char *basefp = nullptr;
  char *targetfp = nullptr;

  while ((c = getopt(argc, argv, "edi::t::o:c")) != -1) {
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
    case 'i':
      basefp = optarg;
      break;
    case 't':
      targetfp = optarg;
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
usage:
    fprintf(stderr, "Usage: gdelta [-d|-e] <basefile> <delta|target-file> [-o "
                    "<outputfile> | -c]\n");
    return 1;
  }

  for (int i = 0; optind < argc; i++, optind++) {
    switch (i) {
	case 0:
		basefp = argv[optind];
		break;
	case 1:
		targetfp = argv[optind];
		break;
	default:
		goto usage;
    }
  }

  printf("Args: input:%s target/delta:%s encode:%d\n", basefp, targetfp,
         edflags & 0b10);

  if (basefp == nullptr || targetfp == nullptr)
	goto usage;


  // Set output filedescriptor (stdout or file)
  int output_fd = fileno(stdout);
  if (cvalue != nullptr) {
    output_fd = open(cvalue, O_RDWR | O_TRUNC | O_CREAT, S_IRGRP | S_IWGRP | S_IWUSR | S_IRUSR);
    if (output_fd < 0)
    {
      printf("Failed to open output file (%d)", output_fd);
      return 1;
    }
  }


  uint8_t *target_delta;
  uint64_t target_delta_size = load_file_to_memory(targetfp, &target_delta);
  uint8_t *origin;
  uint64_t origin_size = load_file_to_memory(basefp, &origin);

  if (edflags & 0b10){
	// Encode target, origin -> delta
	
	// Maximum size for delta is the target state (since it's only useful if it's less than that) 
	uint32_t delta_size = target_delta_size;
	uint8_t *delta = (uint8_t*)malloc(delta_size);
	int status = gencode(target_delta, target_delta_size, origin, origin_size, delta, &delta_size);

	write(output_fd, delta, delta_size);
	free(delta);
	return status;
  }

  if (edflags & 0b01) {
	// Decode origin, delta -> target
	// Allocate slightly more than the origin and delta combined
	uint32_t target_size = target_delta_size + origin_size * 11 / 10;
	uint8_t *target = (uint8_t*)malloc(target_size);
	int status = gdecode(target_delta, target_delta_size, origin, origin_size, target, &target_size);

	write(output_fd, target, target_size);
	free(target);
	return status;
  }

  free(target_delta);
  free(origin);

  if (output_fd != fileno(stdout)) {
    close(output_fd);
  } 

  return 0;
}

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
#ifdef _MSC_VER
#include <compat/msvc.h>
#include <compat/getopt.h>
#else
#include <unistd.h>
#endif

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

  while ((c = getopt(argc, argv, "edo:c")) != -1) {
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
  usage:
    fprintf(stderr, "Usage: gdelta [-d|-e] [-o <outputfile>] <basefile> "
                    "<delta|target-file> \n");
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

//  printf("Args: base:%s target/delta:%s encode:%d\n", basefp, targetfp, edflags & 0b10);

  if (basefp == nullptr || targetfp == nullptr)
    goto usage;

  // Set output filedescriptor (stdout or file)
  int output_fd = fileno(stdout);
  if (cvalue != nullptr) {
#ifdef _WIN32
    output_fd = open(cvalue, O_RDWR | O_TRUNC | O_CREAT);
#else
    output_fd = open(cvalue, O_RDWR | O_TRUNC | O_CREAT,
                     S_IRGRP | S_IWGRP | S_IWUSR | S_IRUSR);
#endif
    if (output_fd < 0) {
      printf("Failed to open output file (%d)\n", output_fd);
      return 1;
    }
  }

  uint8_t *target_delta;
  uint64_t target_delta_size = load_file_to_memory(targetfp, &target_delta);
  uint8_t *origin;
  uint64_t origin_size = load_file_to_memory(basefp, &origin);

  if (edflags & 0b10) {
    // Encode target, origin -> delta

    // Maximum size for delta is the target state (since it's only useful if
    // it's less than that)
    uint32_t delta_size = target_delta_size;
    uint8_t *delta = (uint8_t *)malloc(delta_size);
    int status = gencode(target_delta, target_delta_size, origin, origin_size,
                         delta, &delta_size);

    if (write(output_fd, delta, delta_size) < 0) {
      printf("Failed to write output file (%d)\n", output_fd);
      return 1;
    }

    free(delta);
    return status;
  }

  if (edflags & 0b01) {
    // Decode origin, delta -> target
    // Allocate slightly more than the origin and delta combined
    // TODO: handle status and increase buffer if too small
    uint32_t target_size = target_delta_size + origin_size * 11 / 10;
    uint8_t *target = (uint8_t *)malloc(target_size);
    int status = gdecode(target_delta, target_delta_size, origin, origin_size,
                         target, &target_size);

    if (write(output_fd, target, target_size) < 0) {
      printf("Failed to write output file (%d)\n", output_fd);
      return 1;
    }

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

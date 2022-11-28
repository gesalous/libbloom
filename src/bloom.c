/*
 *  Copyright (c) 2012-2019, Jyri J. Virkki
 *  All rights reserved.
 *
 *  This file is under BSD license. See LICENSE file.
 */

/*
 * Refer to bloom.h for documentation on the public interfaces.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../include/bloom.h"
#include "../include/murmurhash2.h"

#define MAKESTRING(n) STRING(n)
#define STRING(n) #n

#define BLOOM_ALIGNMENT (512)

inline static int test_bit_set_bit(unsigned char *buf, unsigned int x,
                                   int set_bit) {
  unsigned int byte = x >> 3;
  unsigned char c = buf[byte]; // expensive memory access
  unsigned int mask = 1 << (x % 8);

  if (c & mask) {
    return 1;
  } else {
    if (set_bit) {
      buf[byte] = c | mask;
    }
    return 0;
  }
}

static int bloom_check_add(struct bloom *bloom, const void *buffer, int len,
                           int add) {
  if (bloom->ready == 0) {
    printf("bloom at %p not initialized!\n", (void *)bloom);
    return -1;
  }

  int hits = 0;
  register unsigned int a = murmurhash2(buffer, len, 0x9747b28c);
  register unsigned int b = murmurhash2(buffer, len, a);
  register unsigned int x;
  register unsigned int i;

  for (i = 0; i < bloom->hashes; i++) {
    x = (a + i * b) % bloom->bits;
    if (test_bit_set_bit(bloom->bf, x, add)) {
      hits++;
    } else if (!add) {
      // Don't care about the presence of all the bits. Just our own.
      return 0;
    }
  }

  if (hits == bloom->hashes) {
    return 1; // 1 == element already in (or collision)
  }

  return 0;
}

int bloom_init_size(struct bloom *bloom, int entries, double error,
                    unsigned int cache_size) {
  return bloom_init(bloom, entries, error);
}

int bloom_init(struct bloom *bloom, int entries, double error) {
  bloom->ready = 0;

  if (entries < 1000 || error == 0) {
    return 1;
  }

  bloom->entries = entries;
  bloom->error = error;

  double num = log(bloom->error);
  double denom = 0.480453013918201; // ln(2)^2
  bloom->bpe = -(num / denom);

  double dentries = (double)entries;
  bloom->bits = (int)(dentries * bloom->bpe);

  if (bloom->bits % 8) {
    bloom->bytes = (bloom->bits / 8) + 1;
  } else {
    bloom->bytes = bloom->bits / 8;
  }

  bloom->hashes = (int)ceil(0.693147180559945 * bloom->bpe); // ln(2)

  posix_memalign((void **)&bloom->bf, BLOOM_ALIGNMENT, bloom->bytes);

  if (bloom->bf == NULL) { // LCOV_EXCL_START
    printf("Fatal failed to allocate memory for the bloom filter\n");
    return 1;
  } // LCOV_EXCL_STOP
  memset(bloom->bf, 0x00, bloom->bytes);
  bloom->ready = 1;
  return 0;
}

struct bloom *bloom_init2(int entries, double error) {
  struct bloom *bloom = NULL;
  if (posix_memalign((void **)&bloom, BLOOM_ALIGNMENT, sizeof(struct bloom)) !=
      0) {
    printf("FATAL posix_memalign failed\n");
    return NULL;
  }
  memset(bloom, 0x00, sizeof(struct bloom));
  int ret = bloom_init(bloom, entries, error);
  if (ret) {
    free(bloom);
    return NULL;
  }
  return bloom;
}

static int bloom_read_from_file(int file_desc, off_t file_offset, char *buffer,
                                size_t buffer_size) {

  ssize_t bytes_read = 0;

  while (bytes_read < sizeof(struct bloom)) {
    ssize_t num_bytes = pread(file_desc, &buffer[bytes_read],
                              buffer_size - bytes_read, file_offset);
    if (num_bytes < 0)
      return 0;
    bytes_read += num_bytes;
  }
  return 1;
}

static int bloom_append_to_file(int file_desc, char *buffer,
                                size_t buffer_size) {

  ssize_t bytes_written = 0;

  while (bytes_written < sizeof(struct bloom)) {
    ssize_t num_bytes =
        write(file_desc, &buffer[bytes_written], buffer_size - bytes_written);
    if (num_bytes < 0)
      return 0;
    bytes_written += num_bytes;
  }
  return 1;
}
/**
 *@brief Persists the contents of a bloom filter in a file.
 *@param bloom the bloom filter to be persisted.
 *@param filename the path to the file.
 * @return 1 on success 0 on failure.
 */
int bloom_persist(struct bloom *bloom, const char *filename) {
  int file_desc =
      open(filename, O_CREAT | O_APPEND | O_RDWR | O_DIRECT | O_SYNC);
  if (file_desc < 0) {
    printf("Failed to open bloom file %s\n", filename);
    perror("Reason:");
    return 0;
  }
  if (bloom_append_to_file(file_desc, (char *)bloom, sizeof(struct bloom)) <
      0) {
    printf("Failed to write bloom metadata into file %s\n", filename);
    perror("Reason:");
  }

  if (bloom_append_to_file(file_desc, (char *)bloom, bloom->bytes) < 0) {
    printf("Failed to write bloom data into file %s\n", filename);
    perror("Reason:");
  }

  if (close(file_desc) < 0) {
    printf("Failed to close file %s\n", filename);
    return 0;
  }

  return 1;
}

/**
 * @brief Recovers a bloom filter in memory from a file.
 * @param The filename where the contents of the bloom filter are stored
 *@return An in memory representation of the bloom_filter
 */
struct bloom *bloom_recover(char *filename) {
  int file_desc = open(filename, O_RDONLY | O_DIRECT);
  if (file_desc < 0) {
    printf("Failed to open bloom file %s\n", filename);
    perror("Reason:");
    return 0;
  }
  struct bloom *bloom = NULL;
  if (posix_memalign((void **)&bloom, BLOOM_ALIGNMENT, sizeof(struct bloom)) !=
      0) {
    printf("FATAL posix_memalign failed\n");
    return NULL;
  }
  if (0 ==
      bloom_read_from_file(file_desc, 0, (char *)bloom, sizeof(struct bloom))) {
    printf("Failed to read from file %s\n", filename);
    return NULL;
  }

  if (posix_memalign((void **)&bloom->bf, BLOOM_ALIGNMENT, bloom->bytes) < 0) {
    printf("Failed to allocate memory for the bf\n");
    return NULL;
  }
  if (0 == bloom_read_from_file(file_desc, sizeof(struct bloom),
                                (char *)bloom->bf, bloom->bytes)) {
    printf("Failed to read actual bloom filter data\n");
    return NULL;
  }

  return bloom;
}

int bloom_check(struct bloom *bloom, const void *buffer, int len) {
  return bloom_check_add(bloom, buffer, len, 0);
}

int bloom_add(struct bloom *bloom, const void *buffer, int len) {
  return bloom_check_add(bloom, buffer, len, 1);
}

void bloom_print(struct bloom *bloom) {
  printf("bloom at %p\n", (void *)bloom);
  printf(" ->entries = %d\n", bloom->entries);
  printf(" ->error = %f\n", bloom->error);
  printf(" ->bits = %d\n", bloom->bits);
  printf(" ->bits per elem = %f\n", bloom->bpe);
  printf(" ->bytes = %d\n", bloom->bytes);
  printf(" ->hash functions = %d\n", bloom->hashes);
}

void bloom_free(struct bloom *bloom) {
  if (bloom->ready) {
    free(bloom->bf);
  }
  bloom->ready = 0;
}

int bloom_reset(struct bloom *bloom) {
  if (!bloom->ready)
    return 1;
  memset(bloom->bf, 0, bloom->bytes);
  return 0;
}

const char *bloom_version(void) { return MAKESTRING(BLOOM_VERSION); }

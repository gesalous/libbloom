#include "../include/bloom.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define NUM_LEVELS 3
#define L0_SIZE 65536
#define GROWTH_FACTOR 8
#define PBF_11_BITS_PER_ELEMENT 0.0043484747805937
#define BLOOM_BUFFER_SIZE (20*1024*1024UL)

int main(void) {
  char *bloom_buffer[NUM_LEVELS] = {0};
  for (int i = 0; i < NUM_LEVELS; i++) 
    bloom_buffer[i] = calloc(1UL, BLOOM_BUFFER_SIZE);

  struct bloom *bfs[NUM_LEVELS] = {0};

  int64_t level_size = L0_SIZE;
  bfs[0] = bloom_init_with_buffer(bloom_buffer[0], BLOOM_BUFFER_SIZE, level_size, PBF_11_BITS_PER_ELEMENT);
  if (NULL == bfs[0]) {
    fprintf(stderr, "Fatal allocatiom of bloom filter for level 0 failed\n");
    _exit(EXIT_FAILURE);
  }
  for (int i = 1; i < NUM_LEVELS; i++) {
    level_size *= GROWTH_FACTOR;
    bfs[i] = bloom_init_with_buffer(bloom_buffer[i], BLOOM_BUFFER_SIZE, level_size, PBF_11_BITS_PER_ELEMENT);
    if (NULL != bfs[i])
      continue;
    fprintf(stderr, "Fatal allocatiom of bloom filter for level %d failed\n",
            i);
    _exit(EXIT_FAILURE);
  }
  const char *key_s = "giorgis";

  bloom_add(bfs[0], key_s, strlen(key_s) + 1);


  if (0 == bloom_check(bfs[0], key_s, strlen(key_s) + 1)) {
    fprintf(stderr, "FATAL recovered bloom filter is corrupted");
    _exit(EXIT_FAILURE);
  }
  fprintf(stderr, "Success\n");
  return 0;
}

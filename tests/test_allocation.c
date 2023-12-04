#include "../include/bloom.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define NUM_LEVELS 5
#define L0_SIZE 65536
#define GROWTH_FACTOR 8
#define PBF_11_BITS_PER_ELEMENT 0.0043484747805937
int main(void) {

  struct bloom *bfs[NUM_LEVELS] = {0};

  int64_t level_size = L0_SIZE;
  bfs[0] = bloom_init2(level_size, PBF_11_BITS_PER_ELEMENT);
  if (NULL == bfs[0]) {
    fprintf(stderr, "Fatal allocatiom of bloom filter for level 0 failed\n");
    _exit(EXIT_FAILURE);
  }
  for (int i = 1; i < NUM_LEVELS; i++) {
    level_size *= GROWTH_FACTOR;
    bfs[i] = bloom_init2(level_size, PBF_11_BITS_PER_ELEMENT);
    if (NULL != bfs[i])
      continue;
    fprintf(stderr, "Fatal allocatiom of bloom filter for level %d failed\n",
            i);
    _exit(EXIT_FAILURE);
  }
  const char *key_s = "giorgis";

  bloom_add(bfs[0], key_s, strlen(key_s) + 1);


  int file_desc = open("/tmp/test.bloom", O_RDWR | O_CREAT, 0644);

  // Check if the file was opened successfully
  if (file_desc < 0) {
    perror("Failed to open file\n");
    _exit(EXIT_FAILURE);
  }
  bloom_persist(bfs[0], file_desc);
  free(bfs[0]);
  bfs[0] = bloom_recover(file_desc);
  if(bfs[0] == NULL){
    fprintf(stderr,"FATAL did not manage to recover bf\n");
    _exit(EXIT_FAILURE);
  }
  if (0 == bloom_check(bfs[0], key_s, strlen(key_s) + 1)) {
    fprintf(stderr, "FATAL recovered bloom filter is corrupted");
    _exit(EXIT_FAILURE);
  }
  fprintf(stderr, "Success\n");
  return 0;
}

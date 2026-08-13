#ifndef METRIC_BUCKETS_H
#define METRIC_BUCKETS_H

#include <Arduino.h>

// Change this constant easily later if you want 10 seconds instead of 5
#define BUCKET_COUNT 5

struct RamBucket {
  int minFree;
  int maxFree;
  int nowFree;
};

// Declare variables as 'extern' so other files know they exist but don't re-create them
extern RamBucket ramHistory[BUCKET_COUNT];
extern int currentBucketIdx;

// Function declarations
void initRamProfiler();
void profileRam();

#endif

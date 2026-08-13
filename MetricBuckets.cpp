#include "MetricBuckets.h"
#include <limits.h>

extern "C" char *sbrk(int i);

// Allocate the actual memory for the global variables here
RamBucket ramHistory[BUCKET_COUNT];
int currentBucketIdx = 0;
static unsigned long lastBucketSwitchTime = 0; // 'static' keeps it hidden inside this file

void initRamProfiler() {
  char stack_dummy = 0;
  int initialFree = &stack_dummy - sbrk(0);
  
  for (int i = 0; i < BUCKET_COUNT; i++) {
    ramHistory[i].minFree = initialFree;
    ramHistory[i].maxFree = initialFree;
    ramHistory[i].nowFree = initialFree;
  }
  currentBucketIdx = 0;
  lastBucketSwitchTime = millis();
}

void profileRam() {
  char stack_dummy = 0;
  int ramNowFree = &stack_dummy - sbrk(0);
  unsigned long now = millis();

  // Check if 1 second has elapsed
  if (now - lastBucketSwitchTime >= 1000) {
    ramHistory[currentBucketIdx].nowFree = ramNowFree;

    // Advance rolling index
    currentBucketIdx = (currentBucketIdx + 1) % BUCKET_COUNT;
    
    // Reset incoming bucket
    ramHistory[currentBucketIdx].minFree = ramNowFree;
    ramHistory[currentBucketIdx].maxFree = ramNowFree;
    ramHistory[currentBucketIdx].nowFree = ramNowFree;
    
    lastBucketSwitchTime = now;
  }

  // Update live bucket metrics
  if (ramNowFree < ramHistory[currentBucketIdx].minFree) {
    ramHistory[currentBucketIdx].minFree = ramNowFree;
  }
  if (ramNowFree > ramHistory[currentBucketIdx].maxFree) {
    ramHistory[currentBucketIdx].maxFree = ramNowFree;
  }
}

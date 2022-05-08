#include <qthread/qthread.h>

#include <chrono>
#include <iostream>

namespace {

using us = std::chrono::duration<double, std::micro>;
constexpr int kMeasureRounds = 10000000;

static aligned_t qthread_null(void *arg) {
    return 0;
}

static aligned_t qthread_bench_yield(void *arg) {
    for (int i = 0; i < kMeasureRounds / 2; ++i) {
        qthread_yield();
    }
    return 0;
}

void BenchSpawnJoin() {
  for (int i = 0; i < kMeasureRounds; ++i) {
    aligned_t return_value = 0;
    qthread_fork(qthread_null, NULL, &return_value);
    qthread_readFF(NULL, &return_value);
  }
}

void BenchUncontendedMutex() {
  aligned_t lock = 1;
  volatile unsigned long foo = 0;

  for (int i = 0; i < kMeasureRounds; ++i) {
    qthread_lock(&lock);
    foo++;
    qthread_unlock(&lock);
  }
}

void BenchYield() {
  aligned_t return_value = 0;
  qthread_fork(qthread_bench_yield, NULL, &return_value);

  for (int i = 0; i < kMeasureRounds / 2; ++i)
    qthread_yield();

  qthread_readFF(NULL, &return_value);
}

void PrintResult(std::string name, us time) {
  time /= kMeasureRounds;
  std::cout << "test '" << name << "' took "<< time.count() << " us."
            << std::endl;
}

} // anonymous namespace

int main(int argc, char *argv[]) {
  int ret;

  ret = qthread_init(1);
  if (ret) {
    printf("failed to start runtime\n");
    return ret;
  }

  auto start = std::chrono::steady_clock::now();
  BenchSpawnJoin();
  auto finish = std::chrono::steady_clock::now();
  PrintResult("SpawnJoin",
	std::chrono::duration_cast<us>(finish - start));

  start = std::chrono::steady_clock::now();
  BenchUncontendedMutex();
  finish = std::chrono::steady_clock::now();
  PrintResult("UncontendedMutex",
    std::chrono::duration_cast<us>(finish - start));

  start = std::chrono::steady_clock::now();
  BenchYield();
  finish = std::chrono::steady_clock::now();
  PrintResult("Yield",
    std::chrono::duration_cast<us>(finish - start));

  return 0;
}

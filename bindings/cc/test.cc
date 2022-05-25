extern "C" {
#include <base/stddef.h>
#include <base/log.h>
}

#include <string>
#include "sync.h"
#include "thread.h"
#include "timer.h"

namespace {

constexpr int kTestValue = 10;

void foo(int arg) {
  if (arg != kTestValue) BUG();
}

void MainHandler(void *arg) {
  std::string str = "captured!";
  int i = kTestValue;
  int j = kTestValue;
  rt::Mutex m;
  rt::CondVar cv;

  rt::Spawn([=]{
    log_info("hello from ThreadSpawn()! '%s'", str.c_str());
    foo(i);
  });

  rt::Spawn([&]{
    log_info("hello from ThreadSpawn()! '%s'", str.c_str());
    foo(i);
    m.Lock();
    j *= 2;
    m.Unlock();
    cv.Signal();
  });

  rt::Yield();
  m.Lock();
  if (j != kTestValue * 2) cv.Wait(&m);
  if (j != kTestValue * 2) BUG();
  m.Unlock();

  rt::Sleep(1 * rt::kMilliseconds);

  auto th = rt::Thread([&]{
    log_info("hello from rt::Thread! '%s'", str.c_str());
    foo(i);
  });
  th.Join();
}

} // anonymous namespace

int main(int argc, char *argv[]) {
  int ret;

  ret = runtime_init((1 < argc) ? argv[1] : NULL, MainHandler, NULL);
  if (ret) {
    log_err("failed to start runtime");
    return ret;
  }
  return 0;
}

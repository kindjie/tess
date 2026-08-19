#include <tess/ops/async_work.h>

struct BadWork {
  void operator()(tess::AsyncWorkBudget, int&) {}
};

int main() {
  tess::ResumableWorkQueue<int> queue;
  BadWork work;
  static_cast<void>(queue.submit(work));
}

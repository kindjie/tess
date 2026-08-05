#include <tess/core/config.h>

#include <mutex>
#include <thread>
#include <vector>

#ifdef _CPPUNWIND
#error "MSVC portability spike must compile without a /EH option"
#endif

int main() {
  static_assert(!tess::has_exceptions);

  std::mutex mutex;
  std::vector<int> values;
  std::thread worker([&] {
    const std::scoped_lock lock(mutex);
    values.push_back(42);
  });
  worker.join();
  return values.size() == 1 && values.front() == 42 ? 0 : 1;
}

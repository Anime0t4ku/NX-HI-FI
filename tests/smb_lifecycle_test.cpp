#include "smb_lifecycle.hpp"
#include <atomic>
#include <cassert>
#include <thread>
#include <vector>
#include <iostream>
int main() {
    constexpr int threads = 6, iterations = 4000;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    for (int i = 0; i < threads; ++i)
        workers.emplace_back([&] {
            ++ready;
            while (!start)
                std::this_thread::yield();
            for (int j = 0; j < iterations; ++j) {
#ifdef NXH_TEST_UNSAFE_LIFECYCLE
                auto *context = smb2_init_context();
#else
                auto *context = nxh::createSmbContext();
#endif
                assert(context);
                std::this_thread::yield();
#ifdef NXH_TEST_UNSAFE_LIFECYCLE
                smb2_destroy_context(context);
#else
                nxh::destroySmbContext(context);
#endif
            }
        });
    while (ready != threads)
        std::this_thread::yield();
    start = true;
    for (auto &worker : workers)
        worker.join();
    assert(smb2_active_contexts() == nullptr);
    std::cout << "Concurrent SMB context creation/destruction passed (24,000 sessions).\n";
}

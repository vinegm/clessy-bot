#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

/**
 * @brief Pool of persistent worker threads consuming a shared task queue.
 *
 * Threads are spawned once and sleep on a condition variable while idle, so
 * submitting a task costs a queue push + wakeup instead of a thread spawn.
 *
 * Intended for coarse-grained work: whole searches, root-split perft, batch
 * jobs. Do not feed it fine-grained work like single move generations; the
 * synchronization overhead will dominate the work itself.
 */
class ThreadPool {
public:
  explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency()) {
    resize(num_threads);
  }

  ~ThreadPool() { shutdown(); }

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  size_t size() const { return workers.size(); }

  /**
   * @brief Change the number of worker threads (e.g. UCI "Threads" option).
   *
   * Blocks until in-flight tasks finish.
   */
  void resize(size_t num_threads) {
    shutdown();
    if (num_threads == 0) num_threads = 1;

    stopping = false;
    for (size_t i = 0; i < num_threads; i++) {
      workers.emplace_back([this] { worker_loop(); });
    }
  }

  /**
   * @brief Queue a task and get a future for its result.
   */
  template<typename F, typename... Args>
  auto submit(F &&fn, Args &&...args) -> std::future<std::invoke_result_t<F, Args...>> {
    using Result = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<Result()>>(
        std::bind(std::forward<F>(fn), std::forward<Args>(args)...)
    );
    std::future<Result> future = task->get_future();

    {
      std::lock_guard<std::mutex> lock(mutex);
      tasks.push([task] { (*task)(); });
    }
    wake_worker.notify_one();

    return future;
  }

  // Block until the queue is empty and all workers are idle.
  void wait_idle() {
    std::unique_lock<std::mutex> lock(mutex);
    idle.wait(lock, [this] { return tasks.empty() && active_tasks == 0; });
  }

private:
  std::vector<std::thread> workers;
  std::queue<std::function<void()>> tasks;

  std::mutex mutex;
  std::condition_variable wake_worker;
  std::condition_variable idle;

  size_t active_tasks = 0;
  bool stopping = false;

  void worker_loop() {
    while (true) {
      std::function<void()> task;

      {
        std::unique_lock<std::mutex> lock(mutex);
        wake_worker.wait(lock, [this] { return stopping || !tasks.empty(); });

        if (stopping && tasks.empty()) return;

        task = std::move(tasks.front());
        tasks.pop();
        active_tasks++;
      }

      task();

      {
        std::lock_guard<std::mutex> lock(mutex);
        active_tasks--;
        if (tasks.empty() && active_tasks == 0) idle.notify_all();
      }
    }
  }

  void shutdown() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stopping = true;
    }
    wake_worker.notify_all();

    for (std::thread &worker : workers) {
      if (worker.joinable()) worker.join();
    }
    workers.clear();
  }
};

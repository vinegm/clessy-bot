#pragma once

#include <atomic>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

class Logger {
public:
  Logger() = delete;

  enum class Level {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3
  };

  static void set_level(Level level) { min_level.store(level, std::memory_order_relaxed); }
  static Level get_level() { return min_level.load(std::memory_order_relaxed); }
  static bool enabled(Level level) { return level <= get_level(); }

  template<typename... Args>
  static std::string format(Args &&...args) {
    std::ostringstream stream;
    (stream << ... << std::forward<Args>(args));
    return stream.str();
  }

  template<typename... Args>
  static void respond(Args &&...args) {
    raw(format(std::forward<Args>(args)...));
  }

  template<typename... Args>
  static void error(Args &&...args) {
    emit(Level::Error, std::forward<Args>(args)...);
  }

  template<typename... Args>
  static void warn(Args &&...args) {
    emit(Level::Warn, std::forward<Args>(args)...);
  }

  template<typename... Args>
  static void info(Args &&...args) {
    emit(Level::Info, std::forward<Args>(args)...);
  }

  template<typename... Args>
  static void debug(Args &&...args) {
    emit(Level::Debug, std::forward<Args>(args)...);
  }

private:
  static inline std::atomic<Level> min_level{Level::Info};
  static inline std::mutex output_mutex;

  static void raw(const std::string &line);
  static void write(Level level, const std::string &message);
  static std::string tag(Logger::Level level);

  template<typename... Args>
  static void emit(Level level, Args &&...args) {
    if (!enabled(level)) return;

    write(level, format(std::forward<Args>(args)...));
  }
};

#include "logger.hpp"

#include <algorithm>
#include <iostream>
#include <mutex>

std::string Logger::tag(Logger::Level level) {
  switch (level) {
    case Level::Error: return "error: ";
    case Level::Warn: return "warn: ";
    case Level::Debug: return "debug: ";
    case Level::Info: break;
  }

  return "";
}

void Logger::raw(const std::string &line) {
  std::lock_guard<std::mutex> lock(output_mutex);
  std::cout << line << std::endl;
}

void Logger::write(Level level, const std::string &message) {
  std::string flat = message;
  std::replace(flat.begin(), flat.end(), '\n', ' ');
  std::replace(flat.begin(), flat.end(), '\r', ' ');

  std::lock_guard<std::mutex> lock(output_mutex);
  std::cout << "info string " << tag(level) << flat << std::endl;
}

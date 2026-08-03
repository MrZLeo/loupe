#include "loupe/synchronized_output.hpp"

#include <cstddef>
#include <ios>
#include <limits>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>

namespace loupe {
namespace {

constexpr std::string_view kBeginSynchronizedUpdate = "\x1b[?2026h";
constexpr std::string_view kEndSynchronizedUpdate = "\x1b[?2026l";
constexpr char kEndSynchronizedUpdateAndFlush[] = "\x1b[?2026l\0";

} // namespace

class ScopedSynchronizedOutput::Buffer final : public std::streambuf {
public:
  explicit Buffer(std::streambuf *upstream) : upstream_(upstream) {}

  void finish() noexcept {
    try {
      pubsync();
    } catch (...) { // NOLINT(bugprone-empty-catch)
      // Destructors must not allow output failures to terminate the process.
    }
  }

protected:
  int_type overflow(int_type character) override {
    if (traits_type::eq_int_type(character, traits_type::eof())) {
      return traits_type::not_eof(character);
    }
    try {
      pending_.push_back(traits_type::to_char_type(character));
    } catch (...) {
      return traits_type::eof();
    }
    return character;
  }

  std::streamsize xsputn(const char *data, std::streamsize size) override {
    if (size <= 0) {
      return 0;
    }
    try {
      pending_.append(data, static_cast<std::size_t>(size));
    } catch (...) {
      return 0;
    }
    return size;
  }

  int sync() override {
    if (pending_.empty()) {
      try {
        return upstream_->pubsync();
      } catch (...) {
        return -1;
      }
    }

    const bool has_flush_sentinel = pending_.back() == '\0';
    const std::size_t content_size =
        pending_.size() - static_cast<std::size_t>(has_flush_sentinel);
    if (content_size == 0) {
      pending_.clear();
      const char sentinel = '\0';
      try {
        const std::streamsize written = upstream_->sputn(&sentinel, 1);
        return written == 1 ? upstream_->pubsync() : -1;
      } catch (...) {
        return -1;
      }
    }

    std::string frame;
    try {
      frame.reserve(kBeginSynchronizedUpdate.size()
                    + content_size
                    + kEndSynchronizedUpdate.size()
                    + static_cast<std::size_t>(has_flush_sentinel));
      frame.append(kBeginSynchronizedUpdate);
      frame.append(pending_.data(), content_size);
      frame.append(kEndSynchronizedUpdate);
      if (has_flush_sentinel) {
        frame.push_back('\0');
      }
    } catch (...) {
      return -1;
    }
    if (frame.size() > static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max())) {
      pending_.clear();
      return -1;
    }
    pending_.clear();

    std::streamsize written = 0;
    try {
      written = upstream_->sputn(frame.data(),
                                 static_cast<std::streamsize>(frame.size()));
    } catch (...) {
      close_synchronized_update(has_flush_sentinel);
      return -1;
    }
    if (written != static_cast<std::streamsize>(frame.size())) {
      close_synchronized_update(has_flush_sentinel);
      return -1;
    }
    try {
      return upstream_->pubsync();
    } catch (...) {
      close_synchronized_update(has_flush_sentinel);
      return -1;
    }
  }

private:
  void close_synchronized_update(bool with_flush_sentinel) noexcept {
    try {
      if (with_flush_sentinel) {
        upstream_->sputn(kEndSynchronizedUpdateAndFlush,
                         static_cast<std::streamsize>(
                             sizeof(kEndSynchronizedUpdateAndFlush) - 1));
      } else {
        upstream_->sputn(
            kEndSynchronizedUpdate.data(),
            static_cast<std::streamsize>(kEndSynchronizedUpdate.size()));
      }
      upstream_->pubsync();
    } catch (...) { // NOLINT(bugprone-empty-catch)
      // This is a best-effort recovery from a failed frame write.
    }
  }

  std::streambuf *upstream_;
  std::string pending_;
};

ScopedSynchronizedOutput::ScopedSynchronizedOutput(std::ostream &output)
    : output_(output), upstream_(output.rdbuf()) {
  if (upstream_ == nullptr) {
    throw std::invalid_argument("synchronized output requires a stream buffer");
  }
  if (dynamic_cast<Buffer *>(upstream_) != nullptr) {
    return;
  }

  buffer_ = std::make_unique<Buffer>(upstream_);
  output_.rdbuf(buffer_.get());
}

ScopedSynchronizedOutput::~ScopedSynchronizedOutput() noexcept {
  if (buffer_ == nullptr) {
    return;
  }
  buffer_->finish();
  try {
    if (output_.rdbuf() == buffer_.get()) {
      output_.rdbuf(upstream_);
    }
  } catch (...) { // NOLINT(bugprone-empty-catch)
    // Standard streams do not normally throw here; keep destruction noexcept.
  }
}

} // namespace loupe

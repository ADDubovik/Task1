#pragma once

#include <atomic>
#include <vector>
#include <variant>

template<class... Ts>
struct overloads : Ts... { using Ts::operator()...; };

static constexpr auto cache_line = 64;

// Has implicit requirement for T to be default constructible
template<typename T>
class MpscQueue
{
public:
  struct StoppedState
  {
  };

public:
  explicit MpscQueue(const size_t buf_size)
    : _read_sequence(0)
    , _buf_size(buf_size)
    , _buffer(buf_size)
  {
  }

  void EmplaceStoppedState();

  template<typename... Args>
  void Emplace(Args&&... args) noexcept;

  // To be invoked from a single thread
  std::variant<T, StoppedState> Dequeue() noexcept;

private:
  struct alignas(cache_line) Node
  {
    std::atomic_bool is_filled;
    std::variant<T, StoppedState> data;
  };

private:
  template<typename Type, typename... Args>
  void EmplaceImpl(Args&&... args) noexcept
  {
    const auto sequence = _write_sequence.fetch_add(1);
    const auto index = sequence % _buf_size;

    auto& node = _buffer[index];
    if (node.is_filled)
    {
      node.is_filled.wait(true);
    }

    static_assert(requires {
      {
        T(std::forward<Args>(args)...)
      } noexcept;
    });

    node.data.emplace<Type>(std::forward<Args>(args) ...);
    // TODO: memory order?
    node.is_filled.store(true);
    node.is_filled.notify_one();
  }

private:
  alignas(cache_line) std::atomic<uint64_t> _write_sequence;

  alignas(cache_line) uint64_t _read_sequence;

  const size_t _buf_size;
  std::vector<Node> _buffer;
};

///////////////////////////////////////

template<typename T>
template<typename... Args>
void MpscQueue<T>::Emplace(Args&&... args) noexcept
{
  EmplaceImpl<T>(std::forward<Args>(args) ...);
}

template<typename T>
void MpscQueue<T>::EmplaceStoppedState()
{
  EmplaceImpl<StoppedState>();
}

template<typename T>
std::variant<T, typename MpscQueue<T>::StoppedState> MpscQueue<T>::Dequeue() noexcept
{
  const auto index = _read_sequence % _buf_size;
  ++_read_sequence;

  auto& node = _buffer[index];
  if (!node.is_filled)
  {
    node.is_filled.wait(false);
  }

  std::variant<T, StoppedState> result;
  std::visit(
    overloads{
      [&result](T& t) {
        result.emplace<T>(std::move(t));
      },
      [&result](const StoppedState&) {
        result.emplace<StoppedState>();
      },
    },
    node.data
  );

  // TODO: memory order?
  node.is_filled.store(false);
  node.is_filled.notify_all();

  return result;
}

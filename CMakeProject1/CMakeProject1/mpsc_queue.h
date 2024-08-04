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
  struct StateMetaData
  {
    size_t sequence;
    size_t index;
  };

  struct Value
  {
    StateMetaData meta_data;
    T data;
  };

  struct StoppedState
  {
    StateMetaData meta_data;
  };

public:
  explicit MpscQueue(const size_t buf_size)
    : _read_sequence(0)
    , _buf_size(buf_size)
    , _buffer(buf_size)
  {
  }

  MpscQueue(const MpscQueue&) = delete;
  MpscQueue(MpscQueue&&) = delete;

  MpscQueue& operator=(const MpscQueue&) = delete;
  MpscQueue& operator=(MpscQueue&&) = delete;

  ~MpscQueue() = default;

  StateMetaData EmplaceStoppedState();

  template<typename... Args>
  StateMetaData Emplace(Args&&... args) noexcept;

  // To be invoked from a single thread
  [[nodiscard]] std::variant<Value, StoppedState> Dequeue() noexcept;

private:
  struct alignas(cache_line) Node
  {
    std::atomic_bool is_filled;
    std::variant<Value, StoppedState> data;
  };

private:
  template<typename Type, typename... Args>
  StateMetaData EmplaceImpl(Args&&... args) noexcept
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

    StateMetaData meta_data
    {
      .sequence = sequence,
      .index = index,
    };

    node.data.emplace<Type>(meta_data, std::forward<Args>(args) ...);
    // TODO: memory order?
    node.is_filled.store(true);
    node.is_filled.notify_one();

    return meta_data;
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
MpscQueue<T>::StateMetaData MpscQueue<T>::Emplace(Args&&... args) noexcept
{
  return EmplaceImpl<Value>(std::forward<Args>(args) ...);
}

template<typename T>
MpscQueue<T>::StateMetaData MpscQueue<T>::EmplaceStoppedState()
{
  return EmplaceImpl<StoppedState>();
}

template<typename T>
std::variant<typename MpscQueue<T>::Value, typename MpscQueue<T>::StoppedState> MpscQueue<T>::Dequeue() noexcept
{
  const auto index = _read_sequence % _buf_size;
  ++_read_sequence;

  auto& node = _buffer[index];
  if (!node.is_filled)
  {
    node.is_filled.wait(false);
  }

  std::variant<Value, StoppedState> result;
  std::visit(
    overloads{
      [&result](Value& value) {
        result.emplace<Value>(std::move(value));
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

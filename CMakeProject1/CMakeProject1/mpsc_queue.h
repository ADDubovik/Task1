#pragma once

#include <atomic>
#include <vector>
#include <variant>
#include <thread>

template<class... Ts>
struct overloads : Ts... { using Ts::operator()...; };

static constexpr auto cache_line = 64;

// Has implicit requirement for T to be default constructible
template<typename T>
class MpscQueue
{
  enum class NodeStatus
  {
    EMPTY,
    EMPLACE_IN_PROGRESS,
    DEQUEU_IN_PROGRESS,
    FILLED,
  };

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
    std::atomic<NodeStatus> node_status = NodeStatus::EMPTY;
    std::variant<Value, StoppedState> data;
  };

private:
  template<typename Type, typename... Args>
  StateMetaData EmplaceImpl(Args&&... args) noexcept
  {
    const auto sequence = _write_sequence.fetch_add(1);
    const auto index = sequence % _buf_size;

    auto& node = _buffer[index];

    {
      NodeStatus expected = NodeStatus::EMPTY;
      while (!node.node_status.compare_exchange_strong(expected, NodeStatus::EMPLACE_IN_PROGRESS))
      {
        std::this_thread::yield();
      }
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

    {
      NodeStatus expected = NodeStatus::EMPLACE_IN_PROGRESS;
      if (!node.node_status.compare_exchange_strong(expected, NodeStatus::FILLED))
      {
        // Logic error
        std::terminate();
      }
    }

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

  {
    NodeStatus expected = NodeStatus::FILLED;
    while (!node.node_status.compare_exchange_strong(expected, NodeStatus::DEQUEU_IN_PROGRESS))
    {
      std::this_thread::yield();
    }
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

  {
    NodeStatus expected = NodeStatus::DEQUEU_IN_PROGRESS;
    if (!node.node_status.compare_exchange_strong(expected, NodeStatus::EMPTY))
    {
      // Logic error
      std::terminate();
    }
  }

  return result;
}

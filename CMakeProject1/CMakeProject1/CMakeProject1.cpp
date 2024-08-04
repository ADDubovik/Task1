﻿#include "CMakeProject1.h"

#include "IEvent.h"
#include "mpsc_queue.h"

#include <thread>
#include <map>
#include <array>

struct Data
{
	std::thread::id id;
	size_t num = 0;
};

using Queue = MpscQueue<Data>;
using Report = std::map<std::thread::id, size_t>;

void ProduceFn(const size_t count, Queue& queue)
{
	const auto id = std::this_thread::get_id();
	Data data
	{
		.id = id,
		.num = 1,
	};

	for (size_t i = 0; i < count; ++i)
	{
		queue.Emplace(data);
		++data.num;
	}
}

void ConsumeFn(Queue& queue, Report& report)
{
	bool stopped = false;
	while (!stopped)
	{
		const auto data = queue.Dequeue();

		std::visit(
			overloads{
				[&report](const Data& data) {
					auto& num = report[data.id];
					++num;
					if (data.num != num)
					{
						throw std::runtime_error("Sequence failed!");
					}
				},
				[&stopped](const Queue::StoppedState&) {
					stopped = true;
				}
			},
			data
		);
	}
}

int main()
{
	{
		MpscQueue<int> queue(128);
		queue.Emplace(42);
	}

	{
		MpscQueue<std::string> queue(128);
		std::string test1;
		//queue.Emplace(test1);
		queue.Emplace(std::move(test1));
		queue.EmplaceStoppedState();
		const auto data = queue.Dequeue();
	}

	{
		constexpr auto producer_threads_count = 4;
		constexpr size_t messages_count = 1'000;
		constexpr size_t buffer_size = 128;
		Report report;
		Queue queue(buffer_size);
		{
			auto consume_thread = std::jthread{ &ConsumeFn, std::ref(queue), std::ref(report) };
			{
				std::array<std::jthread, producer_threads_count> producer_threads;
				for (auto& thread : producer_threads)
				{
					thread = std::jthread{ &ProduceFn, messages_count, std::ref(queue) };
				}
			}
			queue.EmplaceStoppedState();
		}

		for (const auto& elem : report)
		{
			std::cout << elem.first << ": " << elem.second << std::endl;
		}
	}

	return 0;
}

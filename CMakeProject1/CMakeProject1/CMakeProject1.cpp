#include "CMakeProject1.h"

#include "IEvent.h"
#include "mpsc_queue.h"

#include <thread>
#include <map>
#include <array>
#include <chrono>
#include <format>

struct Data
{
	std::thread::id id;
	size_t num = 0;
};

using Queue = MpscQueue<Data>;
using Report = std::map<std::thread::id, Queue::Value>;

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
		const auto meta_data = queue.Emplace(data);
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
				[&report](const Queue::Value& value) {
					auto& report_value = report[value.data.id];
					if (value.data.num != report_value.data.num + 1)
					{
						throw std::runtime_error("Sequence failed!");
					}
					report_value = value;
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
	//{
	//	MpscQueue<int> queue(128);
	//	queue.Emplace(42);
	//}

	//{
	//	MpscQueue<std::string> queue(128);
	//	std::string test1;
	//	//queue.Emplace(test1);
	//	queue.Emplace(std::move(test1));
	//	queue.EmplaceStoppedState();
	//	const auto data = queue.Dequeue();
	//}

	using namespace std::chrono;

	{
		constexpr auto producer_threads_count = 4;
		constexpr size_t messages_count = 1'000'000;
		constexpr size_t buffer_size = 64 * 1024;

		Report report;

		{
			const auto start_ctor = steady_clock::now();
			Queue queue(buffer_size);
			const auto end_ctor = steady_clock::now();
			std::cout << std::format("Queue construction: {} ms\n", duration_cast<milliseconds>(end_ctor - start_ctor).count());

			const auto start_processing = steady_clock::now();
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
			const auto end_processing = steady_clock::now();

			std::cout << std::format("overall duration is: {} ms\n", duration_cast<milliseconds>(end_processing - start_processing).count());
		}

		for (const auto& elem : report)
		{
			std::cout << elem.first << ": " << elem.second.data.num << std::endl;
		}

		for (const auto& elem : report)
		{
			if (elem.second.data.num != messages_count)
			{
				throw std::runtime_error("Incorrect messages count");
			}
		}
	}

	return 0;
}

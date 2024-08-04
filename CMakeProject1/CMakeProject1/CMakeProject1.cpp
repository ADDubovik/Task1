#include "CMakeProject1.h"

#include "IEvent.h"
#include "mpsc_queue.h"

#include <thread>

struct Data
{
	std::thread::id id;
	size_t num = 0;
};

void WriteFn(const size_t count)
{
	const auto id = std::this_thread::get_id();
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
		queue.Dequeue();
	}


	return 0;
}

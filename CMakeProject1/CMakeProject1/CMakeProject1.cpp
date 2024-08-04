#include "CMakeProject1.h"

#include "IEvent.h"
#include "mcsp_queue.h"

int main()
{
	{
		McspQueue<int> queue(128);
		queue.Emplace(42);
	}

	{
		McspQueue<std::string> queue(128);
		std::string test1;
		//queue.Emplace(test1);
		queue.Emplace(std::move(test1));
		queue.EmplaceStoppedState();
		queue.Dequeue();
	}


	return 0;
}

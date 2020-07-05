#include "stdafx.h"
#include "TaskQueue.h"

struct AsyncTask
{
	AsyncTaskDelegate Action;
	TaskContext* pCounter;
};

static std::deque<AsyncTask> m_Queue;
static std::condition_variable m_WakeUpCondition;
static std::mutex m_QueueMutex;
static std::mutex m_SleepMutex;
static bool m_Shutdown = false;
static bool m_Paused = true;

TaskQueue::~TaskQueue()
{
	m_Shutdown = true;
}

void TaskQueue::Initialize(uint32 threads)
{
	CreateThreads(threads);
}

bool DoWork(uint32 threadIndex)
{
	m_QueueMutex.lock();
	if (!m_Queue.empty())
	{
		AsyncTask task = m_Queue.front();
		m_Queue.pop_front();
		m_QueueMutex.unlock();
		task.Action.Execute(threadIndex);
		task.pCounter->fetch_sub(1);
		return true;
	}
	else
	{
		m_QueueMutex.unlock();
		return false;
	}
}

DWORD WINAPI WorkFunction(LPVOID lpParameter)
{
	for (;;)
	{
		size_t threadIndex = reinterpret_cast<size_t>(lpParameter);
		bool didWork = DoWork((uint32)threadIndex);
		if (!didWork)
		{
			std::unique_lock lock(m_SleepMutex);
			m_WakeUpCondition.wait(lock);
		}
	}
	return 0;
}

void TaskQueue::CreateThreads(size_t count)
{
	for (size_t i = 1; i < count; ++i)
	{
		HANDLE thread = CreateThread(nullptr, 0, WorkFunction, reinterpret_cast<LPVOID>(i), 0, nullptr);
		SetThreadAffinityMask(thread, 1ull << i);
		std::wstringstream name;
		name << "TaskQueue Thread " << i;
		SetThreadDescription(thread, name.str().c_str());
	}
}

void TaskQueue::AddWorkItem(const AsyncTaskDelegate& action, TaskContext& context)
{
	AsyncTask task;
	task.pCounter = &context;
	task.Action = action;

	std::scoped_lock lock(m_QueueMutex);
	m_Queue.push_back(task);
	context.fetch_add(1);

	m_WakeUpCondition.notify_one();
}

void TaskQueue::Join(TaskContext& context)
{
	m_WakeUpCondition.notify_all();
	while (context.load() > 0)
	{
		DoWork(0);
	}
}

void TaskQueue::Distribute(TaskContext& context, const AsyncDistributeDelegate& action, uint32 count, int32 groupSize /*= -1*/)
{
	if (count == 0)
	{
		return;
	}
	if (groupSize == -1)
	{
		groupSize = std::thread::hardware_concurrency();
	}
	uint32 jobs = (uint32)ceil((float)count / groupSize);
	context.fetch_add(jobs);

	{
		std::scoped_lock lock(m_QueueMutex);
		for (uint32 i = 0; i < jobs; ++i)
		{
			AsyncTask task;
			task.pCounter = &context;
			task.Action = AsyncTaskDelegate::CreateLambda([action, i, count, jobs, groupSize](int threadIndex)
				{
					uint32 start = i * groupSize;
					uint32 end = Math::Min(start + groupSize, count);
					for (uint32 j = start; j < end; ++j)
					{
						action.Execute(TaskDistributeArgs{ (int)j, threadIndex });
					}
				});
			m_Queue.push_back(task);
		}
	}
	m_WakeUpCondition.notify_all();
}

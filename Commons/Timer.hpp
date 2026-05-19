#pragma once

#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <memory>

#include "logger.hpp"

namespace timer
{
	inline constexpr int DEFAULT_DELTA = 1000;
	
	class Timeable
	{
	public:
		virtual ~Timeable() {}

		virtual void onTimerUp() = 0;
	};

	class TimerMessagesProcessed
	{
	public:

		TimerMessagesProcessed()
		{
			{ LOG_INFO("Starting timer...") }

			m_shutdown.store(false, std::memory_order_relaxed);

			m_thread_timer = std::jthread(&TimerMessagesProcessed::timerThread, this);
		}

		~TimerMessagesProcessed()
		{
			try
			{
				{ LOG_INFO("Stopping timer...") }
			
				m_shutdown.store(true);
				while (m_thread_stopped == false)
				{
					m_is_empty.store(false);
					m_is_empty.notify_one();
				}

			}
			catch (const std::exception& error)
			{

			}
		}

		void setDelta(int delta) noexcept 
		{ 
			if (delta < 0)
			{
				m_delta.store(DEFAULT_DELTA, std::memory_order_relaxed);
			}
			else
			{
				m_delta.store(delta, std::memory_order_relaxed);
			}
		}

		void addTimeable(std::weak_ptr<Timeable> timeable) 
		{ 
			m_timeables.push_back(timeable); 

			if (m_is_empty == true)
			{
				m_is_empty = false;
				m_is_empty.notify_one();
			}
		}

	private:

		void timerThread()
		{
			{ LOG_INFO("Thread timer started...") }

			if (m_is_empty == true)
			{
				{ LOG_INFO("No timeables at start, so waiting...") }

				sleepUntilTimeablePresent();
			}
			
			while (!m_shutdown)
			{
				const std::chrono::milliseconds timeout(m_delta.load(std::memory_order_relaxed));

				std::this_thread::sleep_for(timeout);
				
				callTimeables();

				if (m_is_empty == true)
				{
					{ LOG_INFO("All timeables expired, so waiting...") }

					sleepUntilTimeablePresent();

					{ LOG_INFO("Timeable detected, starting again...") }
				}
			}

			m_thread_stopped = true;
			{ LOG_INFO("Timer thread exiting...") }
		}

		std::jthread m_thread_timer;
		std::atomic_int m_delta = DEFAULT_DELTA;
		std::atomic_bool m_shutdown = true;
		std::atomic_bool m_is_empty = true;
		std::atomic_bool m_thread_stopped = false;

		std::vector<std::weak_ptr<Timeable>> m_timeables;

	private:

		void callTimeables()
		{
			for (size_t timeable_index = 0; timeable_index < m_timeables.size();)
			{
				std::weak_ptr<Timeable>& timeable = m_timeables[timeable_index];

				if (!timeable.expired())
				{
					std::shared_ptr<Timeable> real_timeable = timeable.lock();
					if (real_timeable != nullptr)
					{
						real_timeable->onTimerUp();
					}

					++timeable_index;
				}
				else
				{
					m_timeables.erase(m_timeables.begin() + timeable_index);
				}
			}

			if (m_timeables.empty())
			{
				m_is_empty.store(true, std::memory_order_relaxed);
			}
		}

		void sleepUntilTimeablePresent()
		{
			if (!m_shutdown)
			{
				m_is_empty.wait(true);
			}
		}
	};
}
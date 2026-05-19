#pragma once

#include "../Commons/MessagingAppComm.hpp"
#include "../Client/MessagingClient.hpp"
#include "../Commons/Timer.hpp"

#include <random>
#include <limits>

namespace apptest
{
	enum class RESULT { SUCCESS, FAILED };

	/*
	template<typename T>
	RESULT testSerializationExpected(T test_nr, std::array<char, sizeof(T)> expected_bytes)
	{
		std::array<char, sizeof(T)> int_bytes = netw::serializeType<T>(test_nr);

		for (size_t i = 0; i < sizeof(T); ++i)
		{
			if (int_bytes[i] != expected_bytes[i])
			{
				return RESULT::FAILED;
			}
		}

		return RESULT::SUCCESS;
	}

	// applies serialize followed by deserialize
	// if everything goes well, after these two
	// we should obtain the same number
	template<typename T>
	class TestSerializeDeserialize
	{
	public:
		TestSerializeDeserialize(int delta) : m_delta(delta)
		{
		
		}

		static RESULT testSerializeDeserialize(T test_nr)
		{	
			std::array<char, sizeof(T)> to_bytes = netw::serializeType<T>(test_nr);
			T to_value = netw::deserializeType<T>(to_bytes);

			if (to_value == test_nr)
			{
				return RESULT::SUCCESS;
			}
			else
			{
				return RESULT::FAILED;
			}
		}

		void timerThread()
		{
			std::chrono::milliseconds timeout(m_delta);

			std::this_thread::sleep_for(timeout);
			m_shutdown = true;
		}

		RESULT runTest()
		{
			std::random_device rd;
			std::mt19937 generator(rd());
			std::uniform_int_distribution<int32_t> distribution((std::numeric_limits<int32_t>::min)(), (std::numeric_limits<int32_t>::max)());

			uint64_t nums_tested = 0;

			// start timer
			std::jthread timer_thread(&TestSerializeDeserialize::timerThread, this);

			while (!m_shutdown)
			{
				++nums_tested;

				const int32_t test_nr = distribution(generator);
				const RESULT result = testSerializeDeserialize(test_nr);

				if (result == RESULT::FAILED)
				{
					std::cerr << "Failed convert: " << test_nr << "\n";
					return result;
				}
			}

			std::cout << "Tested: " << nums_tested << " numbers" << std::endl;

			return RESULT::SUCCESS;
		}

	private:

		int m_delta = 1;
		volatile bool m_shutdown = false;


	};

	RESULT testClientInfoSerDeser()
	{
		msgapp::ClientsInfo test_info;
		test_info.addClientInfo(2, "Test_name_1");
		netw::SBuffer test_buffer = test_info.serialize();

		msgapp::ClientsInfo result_test_info;
		result_test_info.deserialize(test_buffer);

		std::vector<msgapp::ClientsInfo::ClientInfo>& info_result = result_test_info.getClientsList();
		if (info_result[0].id != 2 || info_result[0].name != std::string("Test_name_1"))
		{
			return RESULT::FAILED;
		}
		else
		{
			return RESULT::SUCCESS;
		}

	}
	*/
	
class SpamMessagesSent : public timer::Timeable
{
public:

	virtual ~SpamMessagesSent() {}

	virtual void onTimerUp() override
	{
		uint64_t msg_count = m_messages_count.exchange(0u);
		{ LOG_CRITICAL("% spam messages in % ms", msg_count, m_delta.load(std::memory_order_relaxed)) }
	}

	void setDelta(int delta)
	{
		if (delta < 0)
		{
			m_delta.store(timer::DEFAULT_DELTA, std::memory_order_relaxed);
		}
		else
		{
			m_delta.store(delta, std::memory_order_relaxed);
		}

	}

	void incrementMessageCount() noexcept { ++m_messages_count; }

private:

	std::atomic_int m_delta = timer::DEFAULT_DELTA;
	std::atomic_uint64_t m_messages_count = 0u;
};

	class Bots
	{
	public:
		Bots() {}
		Bots(std::string_view ip, int timeout) : m_ip(ip) 
		{
			if (timeout >= 0)
			{
				m_timeout = timeout;
			}
		}

		void startSpamming()
		{
			using namespace msgapp;

			const msgapp::ServerStatus status = m_client_side.connectMessagingServer("SpamBot", m_ip);
			if (status == msgapp::ServerStatus::NOT_CONNECTED)
			{
				throw std::runtime_error("Failed to connect to server. Test cannot begin!");
			}	

			m_spam_thread = std::jthread(&Bots::spamThread, this);

			showMenu();

			while (!m_shutdown)
			{
				std::string option;

				std::getline(std::cin, option);

				if (option == "help")
				{
					showMenu();
				}
				else if (option == "close")
				{
					m_shutdown = true;
				}
				else if (checkEqCommand(option, "timeout"))
				{
					changeTimeout(option);
				}
				else if (checkEqCommand(option, "log"))
				{
					changeLog(option);
				}
				else if (option == "timer")
				{
					startTimer();
				}
				else if (option == "timer off")
				{
					stopTimer();
				}

			}
		}

	private:

		void spamThread()
		{
			using namespace std::chrono_literals;

			std::random_device r;

			std::default_random_engine e1(r());
			std::uniform_int_distribution<int> uniform_dist(0, 3);

			{ LOG_INFO("Starting thread spamming...") }
			while (!m_shutdown)
			{
				if (m_spam_timer != nullptr)
				{
					m_spam_timer->incrementMessageCount();
				}
				
				const std::chrono::milliseconds timeout_bot(m_timeout);

				//int msg_index = uniform_dist(e1);
				std::string_view msg_spam = m_spam_msgs[3];

				netw::SBuffer buffer(msg_spam.size());
				std::copy(msg_spam.begin(), msg_spam.end(), buffer.data());

				m_client_side.sendChatMessage(buffer);

				{ LOG_DEBUG("Message send.") }

				std::this_thread::sleep_for(timeout_bot);
			}

			{ LOG_INFO("Exiting...") }
		}

		void showMenu()
		{
			std::cout << "---------- BOT SPAM MENU ----------\n\n";

			std::cout <<
				"0. Show menu\nCommand: help\n\n" <<
				"1. Close\nCommand: close\n\n" <<
				"2. Timeout\nCommand: timeout [nr]\n\n" <<
				"3. Log level\nCommand: log [nr] -- DEBUG=0 INFO=1 WARNING=2 ERROR=3 CRITICAL=4\n\n" <<
				"4. Clear console\nCommand: clear, cls\n\n" <<
				"5. Timer\nCommand: timer, timer off\n\n" <<
				std::endl;
		}

		msgapp::MessagingClientSide m_client_side;

		int m_number_bots = 0;
		int m_timeout = DEFAULT_TIMEOUT;
		std::string_view m_ip;
		volatile bool m_shutdown = false;

		std::jthread m_spam_thread;
		std::unique_ptr<timer::TimerMessagesProcessed> m_timer;
		std::shared_ptr<SpamMessagesSent> m_spam_timer;

		static constexpr std::string_view str_48 = { "Lorem ipsum dolor sit amet, consectetur blandit." };
		static constexpr std::string_view str_128 = { "Lorem ipsum dolor sit amet, consectetur adipiscing elit. In non commodo odio, vitae lobortis leo. Proin eget laoreet augue eget." };
		static constexpr std::string_view str_256 = { "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Donec eget urna venenatis augue faucibus varius dapibus et odio. In pellentesque ut lorem at faucibus.Mauris dignissim tempor ligula, ac imperdiet lectus lobortis id.Phasellus at mi justo.Sed leo." };
		static constexpr std::string_view str_512 = { "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Praesent nec eros placerat, feugiat ipsum in, bibendum ligula. Praesent mattis nibh at magna commodo consequat.Pellentesque mollis orci ipsum, in sollicitudin augue congue non. Etiam ac dictum felis, non commodo urna.Pellentesque ornare et nibh consectetur facilisis.Etiam non dapibus sapien. Integer egestas mi sit amet tempor fermentum.Nullam elementum ligula at ex egestas, a maximus risus tempor. Nunc accumsan diam augue, ut bibendum ligula odio." };

		static constexpr std::array<std::string_view, 4> m_spam_msgs{ str_48, str_128, str_256, str_512 };

		static constexpr int DEFAULT_TIMEOUT = 1000;

		bool checkEqCommand(const std::string& option, std::string_view cmd)
		{
			// make sure we don't access out of bounds elements
			if (option.size() < cmd.size()) return false;

			for (size_t i = 0; i < cmd.size(); ++i)
			{
				if (option[i] != cmd[i]) return false;
			}

			return true;
		}

		void changeTimeout(std::string& option)
		{
			try
			{
				char test = option.at(7);
				if (test == ' ')
				{
					std::string str_nr(option.begin() + 7, option.end());

					int new_timeout = std::stoi(str_nr);

					{ LOG_INFO("New timeout: %", new_timeout) }
					if (new_timeout >= 0)
					{
						m_timeout = new_timeout;
					}
					else
					{
						m_timeout = DEFAULT_TIMEOUT;
					}
				}
			}
			catch (const std::exception& error)
			{
				LOG_ERROR(error.what())
			}
		}

		void changeLog(std::string& option)
		{
			static constexpr int log_index_space = 3;
			
			try
			{
				char test = option.at(log_index_space);
				if (test == ' ')
				{
					std::string str_nr(option.begin() + log_index_space, option.end());

					logger::Level new_lvl = (logger::Level)std::stoi(str_nr);

					{ LOG_INFO("New log: %", (int)new_lvl) }

					if (new_lvl >= logger::Level::L_DEBUG && new_lvl <= logger::Level::L_CRITICAL)
					{
						logger::Logger::setLogLevel(new_lvl);
					}
				}
			}
			catch (const std::exception& error)
			{
				LOG_ERROR(error.what())
			}
		}

		void startTimer()
		{
			if (m_timer != nullptr)
			{
				return;
			}

			m_timer = std::make_unique<timer::TimerMessagesProcessed>();
			m_spam_timer = std::make_shared<SpamMessagesSent>();

			m_timer->addTimeable(m_spam_timer);
		}

		void stopTimer()
		{
			if (m_timer == nullptr)
			{
				return;
			}

			m_spam_timer.reset();
			m_timer.reset();
		}
	};
	
}
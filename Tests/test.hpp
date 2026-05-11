#pragma once

#include "../Commons/MessagingAppComm.hpp"

#include <random>
#include <limits>

namespace apptest
{
	enum class RESULT { SUCCESS, FAILED };

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

}
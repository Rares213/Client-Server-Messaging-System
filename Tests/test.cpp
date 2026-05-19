
#include <iostream>

#include "test.hpp"
#include <cassert>

#define TEST_RESULT(result, test_name) if(result == apptest::RESULT::FAILED) {std::cerr << "Failed: " << test_name << std::endl;} else { std::cerr << "Success: " << test_name << std::endl; } 

int main(int argc, char** argv)
{
	if (argc >= 2)
	{
		netw::initNetw();

		logger::Logger::setLogLevel(logger::Level::L_INFO);

		std::string_view ip = argv[1];
		{ LOG_INFO("IP: %", ip) }

		int timeout = -1;
		if (argc == 3)
		{
			timeout = std::stoi(argv[2]);
			{ LOG_INFO("Timeout: %", timeout) }
		}
		else
		{
			{ LOG_INFO("Default timeout will be used.") }
		}

		try
		{
			apptest::Bots bots(ip, timeout);
		
			bots.startSpamming();

			netw::cleanup();
		}
		catch (const std::exception& error)
		{
			LOG_CRITICAL(error.what())
		}
		
	}
	else
	{
		/*
		{
			int32_t test_int = -1;
			std::array<char, sizeof(int32_t)> expected_bytes{ 0xff, 0xff, 0xff, 0xff };
			apptest::RESULT result = apptest::testSerializationExpected(test_int, expected_bytes);
			TEST_RESULT(result, "testSerializationExpected")
		}

		{
			apptest::TestSerializeDeserialize<int32_t> test_class(2000);
			apptest::RESULT result = test_class.runTest();
			TEST_RESULT(result, "TestSerializeDeserialize")
		}

		{
			apptest::RESULT result = apptest::testClientInfoSerDeser();
			TEST_RESULT(result, "testClientInfoSerDeser")
		}
		*/
	}


}
#pragma once

#include <fstream>
#include <iostream>
#include <string>

#define LOG_DEBUG(...)    if(logger::Logger::getLogLevel() <= logger::Level::L_DEBUG)    { logger::Logger::debug(__VA_ARGS__); }
#define LOG_INFO(...)     if(logger::Logger::getLogLevel() <= logger::Level::L_INFO)     { logger::Logger::info(__VA_ARGS__); };
#define LOG_WARNING(...)  if(logger::Logger::getLogLevel() <= logger::Level::L_WARNING)  { logger::Logger::warning(__VA_ARGS__); };
#define LOG_ERROR(...)    if(logger::Logger::getLogLevel() <= logger::Level::L_ERROR)    { logger::Logger::error(__VA_ARGS__); };
#define LOG_CRITICAL(...) if(logger::Logger::getLogLevel() <= logger::Level::L_CRITICAL) { logger::Logger::critical(__VA_ARGS__); };


namespace logger
{
	enum class Level { L_DEBUG, L_INFO, L_WARNING, L_ERROR, L_CRITICAL };

	class Logger
	{
	public:

		static void set_output(std::ostream* out) { log_output = out; }

		static void setLogLevel(Level level) { log_level = level; }
		static Level getLogLevel() { return Logger::log_level; }

		static void debug(const std::string& message)
		{
			std::cout << "DEBUG: " << message << std::endl;
		}

		static void info(const std::string& message)
		{
			std::cout << "\nINFO: " << message << "\n";
		}

		static void warning(const std::string& message)
		{
			std::cout << "\nWARNING: " << message << "\n";
		}

		static void error(const std::string& message)
		{
			std::cout << "\nERROR: " << message << "\n";
		}

		static void critical(const std::string& message)
		{
			std::cout << "\nCRITICAL: " << message << "\n";
		}

		template<typename T, typename... Targs>
		static void debug(const char* format, T value, Targs... fargs)
		{
			std::cout << "\nDEBUG: ";
			tprint(format, value, fargs...);
			std::cout << "\n";
		}

		template<typename T, typename... Targs>
		static void info(const char* format, T value, Targs... fargs)
		{
			std::cout << "\nINFO: ";
			tprint(format, value, fargs...);
			std::cout << "\n";
		}

		template<typename T, typename... Targs>
		static void warning(const char* format, T value, Targs... fargs)
		{
			std::cout << "\nWARNING: ";
			tprint(format, value, fargs...);
			std::cout << "\n";
		}

		template<typename T, typename... Targs>
		static void error(const char* format, T value, Targs... fargs)
		{
			std::cout << "\nERROR: ";
			tprint(format, value, fargs...);
			std::cout << "\n";	
		}

		template<typename T, typename... Targs>
		static void critical(const char* format, T value, Targs... fargs)
		{
			std::cout << "\nCRITICAL: ";
			tprint(format, value, fargs...);
			std::cout << "\n";
		}

	private:
		Logger() {}

		static void tprint(const char* format)
		{
			std::cout << format;
		}

		template<typename T, typename... Targs>
		static void tprint(const char* format, T value, Targs... fargs)
		{
			for (; *format != '\0'; format++)
			{
				if (*format == '%')
				{
					std::cout << value;
					tprint(format + 1, fargs...);
					return;
				}
				std::cout << *format;
			}
		}

		inline static Level log_level = Level::L_DEBUG;
		inline static std::ostream* log_output = &std::cout;
	};

}

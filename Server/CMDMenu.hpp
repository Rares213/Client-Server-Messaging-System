#pragma once

#include "MessagingServer.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <cstdlib>

namespace cmdm
{
	class CMDMenu
	{
	public:

		CMDMenu(msgapp::MessagingServer& server) : m_server(server) {}

		void runMenu()
		{
			std::string option;

			showMenu();

			do
			{
				std::getline(std::cin, option);

				if (option == "help")
				{
					showMenu();
				}
				else if (option == "close")
				{
					m_server.shutdown();
					m_shutdown = true;
				}
				else if (option == "show")
				{
					showClients();
				}
				else if (option == "clear" || option == "cls")
				{
					std::system(option.c_str());
				}
				else if (checkEqCommand(option, KICK_CMD))
				{
					// kick = 4 chars + " " = 5
					if (option.size() >= 5u)
					{
						std::string str_id(option.data() + KICK_CMD.size(), option.size());
						try
						{
							int id = std::stoi(str_id);
							m_server.kickClient(id);
						}
						catch (const std::invalid_argument& error)
						{
							LOG_ERROR(error.what())
						}
					}
				}
				else if (checkEqCommand(option, LOG_CMD))
				{
					// log = 3 chars + " " = 4
					if (option.size() >= 4u)
					{
						std::string str_log_lvl(option.data() + LOG_CMD.size(), option.size());
						try
						{
							int id = std::stoi(str_log_lvl);
							if (id >= 0 && id <= 4)
							{
								logger::Logger::setLogLevel(static_cast<logger::Level>(id));
							}
						}
						catch (const std::invalid_argument& error)
						{
							LOG_ERROR(error.what())
						}
						catch (const std::exception& error)
						{
							LOG_ERROR(error.what())
						}
					}
				}
				else if (option == "timer")
				{
					m_server.setDeltaTimer();
				}
				else if (option == "timer off")
				{
					m_server.stopTimer();
				}

			} while (!m_shutdown);
		}

	private:

		msgapp::MessagingServer& m_server;
		bool m_shutdown = false;

	private:

		static constexpr std::string_view CLOSE_CMD = "close";
		static constexpr std::string_view SHOW_CMD = "show";
		static constexpr std::string_view KICK_CMD = "kick";
		static constexpr std::string_view LOG_CMD = "log";

		void showMenu()
		{
			std::cout << "---------- Server Menu ----------" << std::endl;

			std::cout <<
				"0. Show menu\nCommand: help\n\n" <<
				"1. Close server\nCommand: close\n\n" <<
				"2. Show users:\nCommand: show\n\n" <<
				"3. Kick user\nCommand: kick [id]\n\n" <<
				"4. Log level\nCommand: log [nr] -- DEBUG=0 INFO=1 WARNING=2 ERROR=3 CRITICAL=4\n\n" <<
				"5. Clear console\nCommand: clear, cls\n\n" <<
				"6. Timer\nCommand: timer, timer off (stops)\n\n" <<
				std::endl;
		}

		size_t showClients()
		{
			msgapp::ClientsInfo clients_data = m_server.getClientsInfo();
			std::vector<msgapp::ClientInfo>& clients_list = clients_data.getClientsList();

			if (clients_list.size() == 0u)
			{
				std::cout << "No clients yet." << std::endl;
			}
			else
			{
				for (size_t i = 0; i < clients_list.size(); ++i)
				{
					std::cout << i + 1 << ". ID = " << clients_list[i].id << " Name " << clients_list[i].name << "\n";
				}
				std::cout << std::endl;
			}

			return clients_list.size();
		}

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


	};
}
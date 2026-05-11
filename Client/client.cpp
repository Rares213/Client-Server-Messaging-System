
#include <iostream>

#include "../Commons/netw.hpp"
#include "MessagingClient.hpp"
#include "../Commons/GUI.hpp"

int main(int argc, char** argv)
{	
	netw::initNetw();

	wnd::WindowSpec win_spec;
	win_spec.window_name = "Client";

	gui::GUI::initGUI(win_spec);

	msgapp::MessagingClientSide messaging_client;
	messaging_client.run();

	return 0;
}
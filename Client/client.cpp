
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

	msgapp::MessagingClientSide msg_client;
	msgapp::MessagingClientGUI client_gui;

	client_gui.setMessagingClient(msg_client);

	client_gui.runUI();

	return 0;
}
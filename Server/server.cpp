
#include "MessagingServer.hpp"
#include "CMDMenu.hpp"


int main()
{
    logger::Logger::setLogLevel(logger::Level::L_INFO);

    try
    {   
        msgapp::MessagingServer::init();

        msgapp::MessagingServer messaging_server(msgapp::PORT.data());

        msgapp::MessageProcessor msg_processor(messaging_server);
        msgapp::Broadcaster broadcaster(messaging_server);

        messaging_server.setMessagerProcessor(&msg_processor);
        messaging_server.setBroadcaster(&broadcaster);

        cmdm::CMDMenu cmd_menu(messaging_server);

        messaging_server.run();
        cmd_menu.runMenu();

        messaging_server.stopListening();
    }
    catch(const std::system_error& err)
    {
        LOG_CRITICAL(err.what())
    }
}


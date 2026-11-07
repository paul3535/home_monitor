#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <ctime>
#include <thread>
#include <time.h>

//#include <Python.h>
#include "Logger.h"
#include "PiUtils.h"
#include "HomeConfig.h"
#include "HomeSystem.h"

HomeConfig* _config;
HomeSystem* _home_system;

#if 0
void listenToSocket( )
{
    int sockfd, newsockfd, portno, clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int cmd;
    std::string cmd_str;

    portno = _config->getSocketPort();

    printf( "using port #%d\n", portno );
   
   #if 0
    while( true )
    {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0)
        {
            PiUtils::MySocketUtils::error( const_cast<char *>("ERROR opening socket") );
            std::cerr << "Sleeping for 30 seconds then retrying.\n";
            sleep( 30 );
            continue;
        }
        bzero((char *) &serv_addr, sizeof(serv_addr));

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        serv_addr.sin_port = htons( portno );
        if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        {
            PiUtils::MySocketUtils::error( const_cast<char *>( "Pi ERROR on binding" ) );
            std::cerr << "Sleeping for 30 seconds then retrying.\n";
            sleep( 30 );
            continue;
        }
        listen(sockfd,5);
        clilen = sizeof(cli_addr);
        break;
    }
    #endif
 
    //--- infinite wait on a connection ---
    while ( 1 ) {
        printf( "waiting for new client...\n" );
        #if 1
        if ( ( newsockfd = accept( sockfd, (struct sockaddr *) &cli_addr, (socklen_t*) &clilen) ) < 0 )
            PiUtils::MySocketUtils::error( const_cast<char *>("ERROR on accept") );
        //printf( "opened new communication with client\n" );
        while ( 1 ) // Use the while loop to enable to break out
        {
            //---- wait for a number from client ---
            cmd_str = PiUtils::MySocketUtils::getStringData( newsockfd );
            //cmd = PiUtils::MySocketUtils::getData( newsockfd );
            DeviceUtils::command_options opt = DeviceUtils::commandOptionsFromString( cmd_str );
            opt._text_format = PiUtils::HTML;
            if ( !_home_system->runCommand( opt ) )
            {
                _logger->logMessage( "Command failed.\n", true, true );
            }
            std::cerr << "Result: \n" << opt._result_str << "\n";

            //std::string system_state = _pool_system->getSystemStatusAsString( false );
            PiUtils::MySocketUtils::sendData( newsockfd, opt._result_str );

            //_logger->logState( _pool_system );
            break;
        }
        close( newsockfd );
        //if ( _home_system->shutdownRequested() )
        {
            //break;
        }
        #endif
        //sleep( 20 );

        //--- if -2 sent by client, we can quit ---
//        if ( data == -2 )
//          break;
     }

}
#endif

void monitorHouse()
{
    std::cout << "Starting to monitor the home system.\n";
    _home_system->runSystem();
}

#if 0
void syncLogs( )
{
    std::cout << "Enabling log syncing.\n";
    _home_system->syncLogs();
}
#endif

void setupEnv()
{
    // Make a file with our date string
    std::string date_msk = "%A\n %F\n%A %H:%M:%S";
    std::string cmd_string = "echo \"";
    cmd_string += date_msk;
    cmd_string += "\" > /tmp/house_manager_date";
    system( cmd_string.c_str() );
    setenv("DATEMSK","/tmp/house_manager_date",1);
}

// Directory structure
// .../house
// .../house/*sensor_group*_log.php
// .../house/*sensor_group*/summary.php
// .../house/*sensor_group*/*sensor*_log.php
// .../house/*sensor_group*/*sensor*/*logs*.csv

int main(int argc, char *argv[]) {
    setupEnv();
    // Get the location for the file.
    Logger::setLevel(Logger::TRACE);
    std::string file = "";
    if ( argc > 1 )
    {
        for( int i = 1; i < argc; ++i )
        {
            if ( strncmp( argv[i], "-l", 2 ) == 0 )
            {
                Logger::setLevel( argv[ ++i ] );
            }
            else
                file = argv[i];
        }
    }
    std::cerr << "Going to read from: " << file.c_str() << "\n";
    _config = HomeConfig::loadConfig( file );
    if ( !_config )
    {
        std::cerr << "Cannot parse file.  Exiting\n";
        return -1;
    }
    std::cerr << "Ready to start\n";
    _home_system = HomeSystem::getHomeSystem( _config );
    if ( !_home_system )
    {
        std::cerr << "Failed to get HomeSystem\n";
        return -1;
    }
    monitorHouse();
#if 0

    std::string log_file = _config->getLoggingHome();
    log_file += _config->getSystemName();
    log_file += "_";
    std::string start_time_str = "";
    PiUtils::getYYMMDD_HHMMSSDisplayTime( PiUtils::getCurrentTime(), start_time_str );
    log_file += start_time_str;
    log_file += ".log";
    std::string debug_log = "";
    if ( _config->enableDebug() )
    {
        debug_log = _config->getLoggingHome();
        debug_log.append( _config->getDebugFile() );
    }
    _logger = new DeviceLogger( log_file, _config->getInstallDir(), debug_log );
    _home_system = HomeSystem::getHomeSystem( _config, _logger );
    if ( !_home_system )
    {
        // Bail
        std::cerr << "Unable to generate the HomeSystem.\n";
        return -1 -1;
    }

    // Read from file
    bool from_file = true;
    try
    {
        //from_file = _sensor_monitor->populateLoggingGroups( file );
        //from_file = _sensor_monitor->populateLoggingGroups( _config );
    }
    catch( ... )
    {
        from_file = false;
    }
    if ( !from_file )
    {
        std::cerr << "Unable to read from file.\n";
    }
 
    bool single_process = _config->singleThread();
    if ( single_process )
    {
        //listenToSocket();
        monitorHouse();
    }
    else
    {
        printf( "Starting monitor thread.\n" );
        std::thread monitor( monitorHouse );
        printf( "Starting sync logs.\n" );
        std::thread sync_logs( syncLogs );
        if ( _config->enableSocket() )
        {
            printf( "Starting listenToSocket thread.\n" );
            std::thread listener( listenToSocket );
            listener.join();
        }
        sync_logs.join();
        monitor.join();
    }
    delete _logger;
    delete _home_system;
    delete _config;
    return 0;
    #endif
}

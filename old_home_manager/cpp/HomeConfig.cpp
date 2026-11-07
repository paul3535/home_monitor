#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string>
#include <string.h>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <fstream>

#include "HomeConfig.h"
#include "Logger.h"

using json = nlohmann::json;
using namespace JSONUtils;

HomeConfig::HomeConfig( )
{
}

HomeConfig::~HomeConfig( )
{
}

json HomeConfig::toJSON() const
{
    json out;
    out[ J_SYSTEM_NAME ] = getSystemName();
    out[ J_SYSTEM_PRETTY_NAME ] = getSystemPrettyName();
    out[ J_BASE_INSTALL_DIR ] = getBaseInstallDir();
    out[ J_DEBUG_OBJECT ][ J_DEBUG_OBJECT_ENABLED ] = getDebug();
    out[ J_EVAL_FREQUENCY_SECONDS ] = getEvaluationFrequencySeconds();
    out[ J_SEND_EMAIL_PYTHON_FILE ] = getSendEmailPythonFile();
    out[ J_GET_EMAIL_PYTHON_FILE ] = getGetEmailPythonFile();
    out[ J_PRIVATE_FILE ] = getPrivateFile();
    out[ J_EMAIL_BASE_DIR ] = getEmailBaseDir();
    std::vector< std::string > config_files;
    getConfigFiles( config_files );
    out[ J_CONFIG_FILES ] = config_files;
    std::vector< std::string > event_files;
    getEventFiles( event_files );
    out[ J_EVENT_FILES ] = event_files;
    return out;
}


std::string appendDirIfFound( const std::string& dir, const json& jf, std::string json_field )
{
    std::string val = JSONUtils::getJSONStringValue( jf, json_field, "" );
    if ( !val.empty() )
    {
        val = PiUtils::addDirectoryToFile( dir, val );
    }
    return val;
}

HomeConfig* HomeConfig::loadConfig( const std::string& fn )
{
    Logger::log( Logger::INFO, "HomeConfig::loadConfig");
    // Make sure fn is a fully qualified path.
    std::string config_fn = fn;
    if ( fn.find_first_of( "/" ) != 0 )
    {
        config_fn = PiUtils::addFullDirectoryToFile( fn );
        Logger::log( Logger::TRACE, "Directory " + fn + " not full. Changing to " + config_fn);
    }
    // Need to make this way more resilient to missing json fields
    HomeConfig* config = new HomeConfig( );
    config->setConfigDir( PiUtils::getDirectoryFromFilename( config_fn ) );
    Logger::log( Logger::INFO, "HomeConfig::loadConfig config_fn" + config_fn);
    std::ifstream ifs(config_fn);
    json jf = json::parse(ifs);

    json logging_json = jf.at( J_LOGGING_OBJECT );
    config->setLoggingEnabled( logging_json.at( J_LOGGING_ENABLED ) );
    std::string logging_base_dir = logging_json.at( J_LOGGING_BASE_DIR );
    logging_base_dir.append("/");
    logging_base_dir.append(jf.at( J_SYSTEM_NAME ) );
    config->setLoggingBaseDir( logging_base_dir );
    config->setLogToFile( JSONUtils::getJSONBoolValue( logging_json, J_LOG_TO_FILE, false ) ); 
    std::string log_level_separate_file_str = JSONUtils::getJSONStringValue( logging_json, J_LOG_LEVEL_TO_SEPARATE_FILES, Logger::getLevelAsString(Logger::INFO) );
    config->setLogLevelToSeparateFile( log_level_separate_file_str );
    Logger::setLogLocation( config->getLoggingBaseDir(), config->getLogToFile(), Logger::getLevelFromString(log_level_separate_file_str) );
    std::string log_level_str = JSONUtils::getJSONStringValue( logging_json, J_LOG_LEVEL, Logger::getLevelAsString(Logger::INFO) );
    Logger::log( Logger::INFO, "HomeConfig::loadConfig setting the log level to:" + log_level_str);
    config->setLogLevel( log_level_str ); 
    Logger::setLevel( log_level_str ); 

    config->setSendStartup( JSONUtils::getJSONBoolValue( jf, J_SEND_STARTUP, true ) );

    config->setSystemName( jf.at( J_SYSTEM_NAME ) );
    config->setSystemPrettyName( jf.at( J_SYSTEM_PRETTY_NAME ) );
    config->setBaseInstallDir( jf.at( J_BASE_INSTALL_DIR ) );
    config->setSendEmailPythonFile( appendDirIfFound( config->getConfigDir(), jf, J_SEND_EMAIL_PYTHON_FILE ) );
    config->setGetEmailPythonFile( appendDirIfFound( config->getConfigDir(), jf, J_GET_EMAIL_PYTHON_FILE ) );
    config->setPrivateFile( appendDirIfFound( config->getConfigDir(), jf, J_PRIVATE_FILE ) );
    config->setEmailBaseDir( appendDirIfFound( config->getConfigDir(), jf, J_EMAIL_BASE_DIR ) );

    json debug_json = jf.at( J_DEBUG_OBJECT );
    config->setDebug( debug_json.at( J_DEBUG_OBJECT_ENABLED ) );

    config->setEvaluationFrequencySeconds( jf.at( J_EVAL_FREQUENCY_SECONDS ) );
    config->setEvaluationFrequencySecondsMin( JSONUtils::getJSONIntValue( jf, J_EVAL_FREQUENCY_SECONDS_MIN, 5 ) );

    std::vector< std::string > config_files;
    config_files = jf[ J_CONFIG_FILES ].get<std::vector<std::string> >( );
    // These files are missing the directoy. Go ahead and add it
    PiUtils::addDirectoryToFiles( config->getConfigDir(), config_files );
    config->setConfigFiles( config_files );
    std::vector< std::string > event_files;
    event_files = jf[ J_EVENT_FILES ].get<std::vector<std::string> >( );
    PiUtils::addDirectoryToFiles( config->getConfigDir(), event_files );
    config->setEventFiles( event_files );


    config->setGPIOEnabled( jf.at( J_GPIO_ENABLED ) );
    config->setGPIOManagerType( JSONUtils::getJSONStringValue( jf, J_GPIO_MANAGER_TYPE, "gpio_disabled" ));
    Logger::log( Logger::TRACE, config->toJSON().dump(2) );

    config->setEmailRetryLimit( JSONUtils::getJSONIntValue( jf, J_EMAIL_RETRY_LIMIT, 10 ) );
    config->setEmailRetryFrequencySeconds( JSONUtils::getJSONIntValue( jf, J_EMAIL_RETRY_FREQ_SECONDS, 300 ) );
    if ( jf.contains( J_PRIVATE_FILE ) )
    {
        std::ifstream private_file( PiUtils::addDirectoryToFile( config->getConfigDir(), jf.at( J_PRIVATE_FILE ) ));
        json pf = json::parse( private_file );
        if ( pf.contains( J_EMAIL_INFO ) )
        {
            json email_info = pf.at( J_EMAIL_INFO );
            config->setEmailInfo( email_info );
        }
    }
    if ( config->getEmailInfo().is_null() )
    {
        json email_info;
        email_info[ J_EMAIL_TO ] = "example@example.com";
        config->setEmailInfo( email_info );
    }
    Logger::log( Logger::TRACE, "HomeConfig::loadConfig done");
    return config;
}


bool HomeConfig::getDeviceStrings( std::vector< std::string >& devices ) const
{
    Logger::log( Logger::TRACE, "HomeConfig::getDeviceStrings");
    // Go through all config files, load them up and add the json strings for the devices into the vectore.
    std::vector< std::string > config_files;
    getConfigFiles( config_files );
    for( std::vector< std::string >::const_iterator itr = config_files.begin(); itr != config_files.end(); ++itr )
    {
        // Load up the file and convert it to json. Each device will be in it's own section.
        // NOTE: There can/will be multiple devices in each file.
        try
        {
            std::ifstream ifs(*itr);
            json jf = json::parse(ifs);
            for( json::iterator j_itr = jf.begin(); j_itr != jf.end(); ++j_itr )
            {
                // Check for J_DEVICES. Add those ones in
                if ( j_itr.key() == J_DEVICES )
                {
                    for( json::iterator j_itr2 = (*j_itr).begin(); j_itr2 != (*j_itr).end(); ++j_itr2 )
                    {
                        devices.push_back( (*j_itr2).dump() );
                        Logger::log( Logger::TRACE, "HomeConfig::getDeviceStrings added: " + (*j_itr2).dump());
                    }
                }
                else if ( j_itr.key() == J_EMAIL_CHECK )
                {
                    devices.push_back( (*j_itr).dump() );
                    Logger::log( Logger::TRACE, "HomeConfig::getDeviceStrings added: " + (*j_itr).dump());
                }
                else
                    Logger::log( Logger::TRACE, "HomeConfig::getDeviceStrings skipping key: " + j_itr.key() + " : " + (*j_itr).dump());
            }
        }
        catch (...)
        {
            devices.clear();
            Logger::log( Logger::WARNING, "Exception caught while parsing " + *itr );
            return false;
        }
    }
    return true;
}

void HomeConfig::setEmailQueries( json eq )
{
    // Populate std::set< std::string > _email_queries; from json
    _email_queries.clear();
    for (const auto& email : eq["email_queries"])
    {
        _email_queries.insert(email.get<std::string>());
        Logger::log( Logger::TRACE, "HomeConfig::setEmailQueries adding email: " + email.get<std::string>());
    }

}

json HomeConfig::getEmailQuesties() const
{
    json val;
    return val;
}

bool HomeConfig::checkValidEmailQuery( const std::string& str ) const
{
    return false;
}

void HomeConfig::setGPIOManagerType( const std::string& str )
{
    Logger::log( Logger::TRACE, "HomeConfig::setGPIOManagerType wth str: " + str);
    _gpio_manager_type = GPIOManager::getGPIOTypeFromString(str);
}
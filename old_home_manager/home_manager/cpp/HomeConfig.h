#ifndef HOME_CONFIG_H
#define HOME_CONFIG_H
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string>
#include <string.h>
//#include <stringstream.h>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <vector>
#include <set>
#include <time.h>
#include <map>
#include <mutex>

#include <algorithm> 
#include <functional> 
#include <cctype>
#include <locale>

#include "JSONUtils.h"
#include "GPIOManager.h"
// This class holds everyting that we need to configure the sensor and operations.
// All user configurable text, etc is in here
class HomeConfig
{
    public:
        HomeConfig( );
        virtual ~HomeConfig();

        static HomeConfig* loadConfig( const std::string& fn );

        std::string getConfigDir() const { return _config_dir; }
        void setConfigDir( const std::string& dir ) { _config_dir = dir; }
        std::string getSystemName() const { return _system_name; }
        void setSystemName( const std::string& name ) { _system_name = name; }
        std::string getSystemPrettyName() const { return _system_pretty_name; }
        void setSystemPrettyName( const std::string& name ) { _system_pretty_name = name; }
        std::string getBaseInstallDir() const { return _base_install_dir; }
        void setBaseInstallDir( const std::string& dir ) { _base_install_dir = dir; }
        bool getDebug() const { return _debug_enabled; }
        void setDebug( bool debug ) { _debug_enabled = debug; }

        unsigned int getEvaluationFrequencySeconds() const { return _eval_frequency_seconds; }
        void setEvaluationFrequencySeconds( unsigned int sec ) { _eval_frequency_seconds = sec; }

        unsigned int getEvaluationFrequencySecondsMin() const { return _eval_frequency_seconds_min; }
        void setEvaluationFrequencySecondsMin( unsigned int sec ) { _eval_frequency_seconds_min = sec; }

        std::string getSendEmailPythonFile() const { return _send_email_python_file; }
        void setSendEmailPythonFile( const std::string& file ) { _send_email_python_file = file; }
        std::string getGetEmailPythonFile() const { return _get_email_python_file; }
        void setGetEmailPythonFile( const std::string& file ) { _get_email_python_file = file; }
        std::string getPrivateFile() const { return _private_file; }
        void setPrivateFile( const std::string& file ) { _private_file = file; }
        std::string getEmailBaseDir() const { return _email_base_dir; }
        void setEmailBaseDir( const std::string& dir ) { _email_base_dir = dir; }

        void getConfigFiles( std::vector< std::string >& config_files ) const { config_files = _config_files; }
        void setConfigFiles( std::vector< std::string > files ) { _config_files = files; }
        void getEventFiles( std::vector< std::string >& event_files ) const { event_files = _event_files; }
        void setEventFiles( std::vector< std::string > files ) { _event_files = files; }
        bool getDeviceStrings( std::vector< std::string >& devices ) const;
        nlohmann::json toJSON() const;

        void setLoggingEnabled( bool enabled ) { _logging_enabled = enabled; }
        bool getLoggingEnabled() const { return _logging_enabled; }

        void setLoggingBaseDir( const std::string& dir ) { _logging_base_dir = dir; }
        std::string getLoggingBaseDir( ) const { return _logging_base_dir; }
        void setLogToFile( bool enable ) { _log_to_file = enable; }
        bool getLogToFile() const { return _log_to_file; }
        void setLogLevelToSeparateFile( const std::string& level_str ) { _log_level_to_separate_file_str = level_str; }
        std::string getLogLevelToSeparateFile() const { return _log_level_to_separate_file_str; }
        void setLogLevel( const std::string& str ) { _log_level_str = str; }
        std::string getLogLevel( ) const { return _log_level_str; }

        void setGPIOEnabled( bool enabled ) { _gpio_enabled = enabled; }
        bool getGPIOEnabled() const { return _gpio_enabled; }

        void setGPIOManagerType( const std::string& str );
        GPIOManager::GPIO_MANAGER_TYPE getGPIOManagerType() const { return _gpio_manager_type; }

        void setEmailInfo( json info ) { _email_info = info; }
        json getEmailInfo() const { return _email_info; }

        void setEmailQueries( json eq );
        json getEmailQuesties() const;
        bool checkValidEmailQuery( const std::string& str ) const;

        void setEmailRetryLimit( unsigned int limit ) { _email_retry_limit = limit; }
        unsigned int getEmailRetryLimit( ) const { return _email_retry_limit; }
        void setEmailRetryFrequencySeconds( unsigned int freq ) { _email_retry_frequency_seconds = freq; }
        unsigned int getEmailRetryFrequencySeconds( ) const { return _email_retry_frequency_seconds; }

        void setSendStartup( bool send ) { _send_startup = send; }
        bool getSendStartup() const { return _send_startup; }
    private:
        std::string _system_name;
        std::string _system_pretty_name;
        std::string _base_install_dir;
        std::vector< std::string > _config_files;
        std::vector< std::string > _event_files;
        std::string _config_dir;
        bool _debug_enabled;
        unsigned int _eval_frequency_seconds;
        unsigned int _eval_frequency_seconds_min;
        std::string _send_email_python_file;
        std::string _get_email_python_file;
        std::string _private_file;
        std::string _email_base_dir;
        bool _gpio_enabled;

        // Logging
        bool _logging_enabled;
        std::string _logging_base_dir;
        bool _log_to_file;
        std::string _log_level_str;
        std::string _log_level_to_separate_file_str;
        // Private
        std::string _email_username;
        std::string _email_password;
        json _email_info;
        // These are who is allowed to ask for status
        std::set< std::string > _email_queries;

        unsigned int _email_retry_limit;
        unsigned int _email_retry_frequency_seconds;

        bool _send_startup;

        GPIOManager::GPIO_MANAGER_TYPE _gpio_manager_type;
};

#endif

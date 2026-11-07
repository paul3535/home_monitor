#ifndef PI_UTILS
#define PI_UTILS
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
#include <map>
#include <time.h>
#include <mutex>

#include <algorithm> 
#include <functional> 
#include <cctype>
#include <locale>
#include "nlohmann/json.hpp"
using json = nlohmann::json;

#define	WPI_GARAGE_DOWN	0 // 18 // BCM_GPIO 29
#define	WPI_GARAGE_UP	3 // 20 // BCM_GPIO 31

namespace PiUtils
{
    struct TriState {
        enum Value { Yes, No, Optional } value;

        TriState(Value v) : value(v) {}
        operator Value() const { return value; }

        // Convert a string to TriState (case-insensitive)
        static TriState fromString(const std::string& str) {
            std::string lowerStr = str;
            std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), [](unsigned char c) { return std::tolower(c); });

            if (lowerStr == "yes") return TriState(Yes);
            if (lowerStr == "no") return TriState(No);
            if (lowerStr == "optional") return TriState(Optional);

            throw std::invalid_argument("Invalid TriState string: " + str);
        }

        // Convert a TriState to string
        std::string toString() const {
            switch (value) {
                case Yes: return "Yes";
                case No: return "No";
                case Optional: return "Optional";
            }
            throw std::logic_error("Invalid TriState value");
        }
    };

    struct PiPicture
    {
        PiPicture() :
            _keep_full( true ),
            _full_file_name( "" ),
            _full_link_name( "" ),
            _resize( false ),
            _resize_factor( 0.5 ),
            _resize_file_name( "" ),
            _resize_link_name( "" ),
            _perform_crop( false ),
            _w_offset( -1 ),
            _h_offset( -1 ),
            _w_size( -1 ),
            _h_size( -1 )
        {
        }

        bool _keep_full;
        std::string _full_file_name;
        std::string _full_link_name;
        bool _resize;
        float _resize_factor;
        std::string _resize_file_name;
        std::string _resize_link_name;
        bool _perform_crop;
        int _w_offset;
        int _h_offset;
        int _w_size;
        int _h_size;

    };

    static bool timeCompareLessThan( struct tm* t1, struct tm* t2 )
    {
        if ( !t1 )
        {
            return true;
        }
        else if ( !t2 )
        {
            return false;
        }
        if ( t1->tm_year < t2->tm_year )
        {
            return true;
        }
        else if ( t1->tm_year > t2->tm_year )
        {
            return false;
        }
        if ( t1->tm_yday < t2->tm_yday )
        {
            return true;
        }
        else if ( t1->tm_yday > t2->tm_yday )
        {
            return false;
        }
        if ( t1->tm_hour < t2->tm_hour )
        {
            return true;
        }
        else if ( t1->tm_hour > t2->tm_hour )
        {
            return false;
        }
        if ( t1->tm_min < t2->tm_min )
        {
            return true;
        }
        else if ( t1->tm_min > t2->tm_min )
        {
            return false;
        }
        if ( t1->tm_sec < t2->tm_sec )
        {
            return true;
        }
        else if ( t1->tm_sec > t2->tm_sec )
        {
            return false;
        }
        return false;
    }
        enum TEXT_FORMAT
        {
            TXT = 0,
            CSV = 1,
            HTML = 2
        };

        TEXT_FORMAT getTextFormatFromString( std::string str, TEXT_FORMAT def = TXT );
	std::string getTextFormatAsString( TEXT_FORMAT text_format );

        std::string getTableStartDelimiter( TEXT_FORMAT format );
        std::string getTableEndDelimiter( TEXT_FORMAT format );
        std::string getTableCellStartDelimiter( TEXT_FORMAT format );
        std::string getTableCellEndDelimiter( TEXT_FORMAT format );
        std::string getTableLineStartDelimiter( TEXT_FORMAT format );
        std::string getTableLineEndDelimiter( TEXT_FORMAT format );
        std::string getStartDelimiter( TEXT_FORMAT format );
        std::string getEndDelimiter( TEXT_FORMAT format );
        std::string getLineEndDelimiter( TEXT_FORMAT format );


    template <typename T> std::string tostr(const T& t)
    { 
       std::ostringstream os; 
       os<<t; 
       return os.str(); 
    } 

    // This should be called in a thread
    // This will create a backdoor to a host on a specific port
    // Note that login via keys is mandatory (no password)

    namespace MySocketUtils
    {
        void error( char *msg );
        void sendData( int sockfd, int x );
        bool sendData( int sockfd, std::string text );
        int getData( int sockfd );
        std::string getStringData( int sockfd );
    }


    // trim from start
    static inline std::string &ltrim(std::string &s) {
	    s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not_fn([](int ch) { return std::isspace(ch); })));
            return s;
    }

    // trim from end
    static inline std::string &rtrim(std::string &s) {
	    s.erase(std::find_if(s.rbegin(), s.rend(), std::not_fn([](int ch) { return std::isspace(ch); })).base(), s.end());
            return s;
    }

    // trim from both ends
    static inline std::string &trim(std::string &s) {
            return ltrim(rtrim(s));
    }

    // NOTE!!!! DO NOT DELETE THIS STRUCT!!!!!!!
    static struct tm* getCurrentTime( )
    {
        time_t rawtime;
        time (&rawtime);
        struct tm * timeinfo= localtime(&rawtime);
        return timeinfo;
    }

    static time_t getCurrentTime_t( )
    {
        time_t rawtime;
        time (&rawtime);
        return rawtime;
    }

    static bool getYYMMDDDisplayTime( struct tm* my_time, std::string& str )
    {

        if ( my_time )
        {
            char buffer[80];
            strftime(buffer,80,"%Y%m%d",my_time);
            str = buffer;
        }
        else
        {
            str = "Now";
        }
        return true;
    }

    static bool getYYMMDDDisplayTime( time_t my_time, std::string& str )
    {
        return( getYYMMDDDisplayTime( localtime(&my_time), str ) );
    }

    static bool getYYMMDisplayTime( struct tm* my_time, std::string& str )
    {

        if ( my_time )
        {
            char buffer[80];
            strftime(buffer,80,"%Y%m",my_time);
            str = buffer;
        }
        else
        {
            str = "Now";
        }
        return true;
    }

    static bool getYYMMDisplayTime( time_t my_time, std::string& str )
    {
        return getYYMMDisplayTime( localtime(&my_time), str );
    }

    static bool getYYMMDD_HHMMSSDisplayTime( struct tm* my_time, std::string& str )
    {
        if ( my_time )
        {
            char buffer[80];
            strftime(buffer,80,"%Y%m%d_%H%M%S",my_time);
            str = buffer;
        }
        else
        {
            str = "Now";
        }
        return true;
    }

    static bool getYYMMDD_HHMMSSDisplayTime( time_t my_time, std::string& str )
    {
        return getYYMMDD_HHMMSSDisplayTime( localtime(&my_time), str );
    }

    static bool getCurrentYYMMDD_HHMMSSDisplayTime( std::string& str )
    {
        time_t my_time = getCurrentTime_t( );
        return getYYMMDD_HHMMSSDisplayTime( localtime(&my_time), str );
    }

    static bool getPrettyDisplayTime( struct tm* my_time, std::string& str )
    {
        if ( my_time )
        {
            char buffer[80];
            strftime(buffer,80,"%Y-%m-%d %H:%M:%S",my_time);
            str = buffer;
        }
        else
        {
            str = "Now";
        }
        return true;
    }

    static bool getCurrentPrettyDisplayTime( std::string& str )
    {
        struct tm* my_time = getCurrentTime( );
        return getPrettyDisplayTime( my_time, str );
    }

    static bool getPrettyDisplayTime( time_t my_time, std::string& str )
    {
        return getPrettyDisplayTime( localtime(&my_time), str );
    }

    static std::string getStdoutFromCommand(std::string cmd)
    {
        std::string data;
        FILE * stream;
        const int max_buffer = 4096;
        char buffer[max_buffer];
        cmd.append(" 2>&1"); // Do we want STDERR?

        stream = popen(cmd.c_str(), "r");
        if (stream) {
            while (!feof(stream))
                if (fgets(buffer, max_buffer, stream) != NULL) data.append(buffer);
            pclose(stream);
        }
        return data;
    }

    static float convertCelciusToFahrenheit( float cel )
    {
        return ( 32.0 + cel * 9.0 / 5.0 );
    }

    static float convertFahrenheitToCelcius( float f )
    {
        return ( (f - 32.0) * 5.0 / 9.0 );
    }

    inline bool fileExists (const std::string& name)
    {
        struct stat buffer;   
        return (stat (name.c_str(), &buffer) == 0); 
    }

    inline bool dirExists (const std::string& name)
    {
        struct stat buffer;
        return ((stat (name.c_str(), &buffer) == 0) && S_ISDIR(buffer.st_mode) );
    }

    void sampleLinesFromFile( std::string& data, unsigned int num_lines_requested, unsigned int every_nth, std::string file_name, std::string alt_file_name = "", std::string string_to_remove = "" );
    void getLinesFromEndOfFile( std::string& data, unsigned int num_lines_requested, std::string file_name );

    bool takePicture( PiPicture& pic );
    bool takePicture( std::string full_file_name, std::string link_name );

    // Image utilities
    bool resizeImage( PiPicture& pipic );

    struct email_response
    {
        std::string _sender;
        std::string _to;
        std::string _subject;
        std::string _body;
    };
    
    void connectBackdoorToHost( std::string hostname_ipaddress, std::string username, int port, std::string remote_command_to_run, bool keepalive );
    bool syncDir( std::string hostname_ipaddress, std::string username, std::string local_dir, std::string remote_dir );
    bool checkMessage( std::string install_dir, std::string message_label, std::string& result_str );
    bool checkMessageJSON( std::string install_dir, std::string message_label, std::vector< email_response >& messages, std::vector<std::string>& raw_json );
    //int checkMessage( std::string install_dir, std::string message_label );
    bool parseConfigFile( std::string fn, std::map< std::string, std::string >& key_value );

    int runSystemCommand( std::string command, int timeout = -1, bool echo_command = false, std::string dir = "" );

    // time in seconds
    unsigned int timeLeftToSleep( time_t start_time, unsigned int requested_duration, unsigned int min_value = 0 );
    unsigned int getDayOfYear( time_t datum_time, int offset );
    unsigned int getHour( time_t datum_time, int offset );
    unsigned int getMinute( time_t datum_time, int offset );
    time_t getNextTimeFromHourMinute( int hour, int minute, int additional_days = 0, int repeat_seconds = 0);
    time_t getTimeInAdditionHourMinuteSeconds( int days, int hours, int minutes, int seconds, time_t start_time = 0 );
    std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems);
    std::vector<std::string> split(const std::string &s, char delim);

    std::string getDirectoryFromFilename( const std::string& str );
    std::string addDirectoryToFile( std::string dir, std::string file );
    std::string addFullDirectoryToFile( std::string file );
    void addDirectoryToFiles( std::string dir, std::vector< std::string >& files );

    void roundFloat( const int digits, const float val, std::string& res );

    inline std::string toLowerCase(const std::string& str) {
        std::string lowerStr = str;
        std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(),
                    [](unsigned char c) { return std::tolower(c); });
        return lowerStr;
    }
    std::string getInternalIPAddress();

};
#endif

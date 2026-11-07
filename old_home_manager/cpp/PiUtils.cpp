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
#include <map>
#include <limits.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

#include "PiUtils.h"
#include "JSONUtils.h"
#include "Logger.h"

using namespace JSONUtils;

using namespace PiUtils;

void MySocketUtils::error( char *msg ) {
  perror(  msg );
  exit(1);
}

void MySocketUtils::sendData( int sockfd, int x ) {
  int n;

  char buffer[32];
  sprintf( buffer, "%d\n", x );
  if ( (n = write( sockfd, buffer, strlen(buffer) ) ) < 0 )
    MySocketUtils::error( const_cast<char *>( "ERROR writing to socket") );
  buffer[n] = '\0';
}

bool MySocketUtils::sendData( int sockfd, std::string text ) {
  int n;

  if ( (n = write( sockfd, text.c_str(), strlen(text.c_str()) ) ) < 0 )
    MySocketUtils::error( const_cast<char *>( "ERROR writing to socket") );
  return (n > 0);
}

int MySocketUtils::getData( int sockfd ) {
  char buffer[2048];
  int n;

  if ( (n = read(sockfd,buffer,2048) ) < 0 )
    MySocketUtils::error( const_cast<char *>( "ERROR reading from socket") );
  buffer[n] = '\0';
  return atoi( buffer );
}

std::string MySocketUtils::getStringData( int sockfd ) {
  char buffer[2048];
  int n;

  if ( (n = read(sockfd,buffer,2048) ) < 0 )
    MySocketUtils::error( const_cast<char *>( "ERROR reading from socket") );
  buffer[n] = '\0';
  std::string str = buffer;
  return str;
}

void PiUtils::sampleLinesFromFile( std::string& data, unsigned int num_lines_requested, unsigned int every_nth, std::string file_name, std::string alt_file_name, std::string string_to_remove )
{
    if ( num_lines_requested == 0 )
    {
        data = "";
        return;
    }
    std::string cmd = "cat ";
    if ( !alt_file_name.empty() )
    {
        cmd.append( " " );
        cmd.append( alt_file_name );
    }
    cmd.append( " " );
    cmd.append( file_name );
    if ( !string_to_remove.empty() )
    {
        cmd.append( " | grep -v " );
        cmd.append( string_to_remove );
    }
    if ( every_nth > 1 )
    {
        cmd.append( " | awk '{if (count++%" );
        cmd.append( tostr( every_nth ) );
        cmd.append( "==0) print $0;}' -" );
    }
    if ( num_lines_requested > 0 )
    {
        cmd.append( "| tail -n " );
        cmd.append( tostr( num_lines_requested ) );
    }
    std::cerr << "Going to run: " << cmd.c_str();
    data = getStdoutFromCommand( cmd );
    //tail -n 100 freezer.log | awk '{if (count++%10==0) print $0;}' -
}

void PiUtils::getLinesFromEndOfFile( std::string& data, unsigned int num_lines_requested, std::string file_name )
{
    if ( ( num_lines_requested == 0 )|| ( !fileExists( file_name ) ) )
    {
        data = "";
        return;
    }
    std::string cmd = "tail -n ";
    cmd.append( tostr( num_lines_requested ) );
    cmd.append( " " );
    cmd.append( file_name );
    data = getStdoutFromCommand( cmd );
    //tail -n 100 freezer.log | awk '{if (count++%10==0) print $0;}' -
#if 0
    string filename = "test.txt";
    ifstream fin;
    fin.open(filename);
    if(fin.is_open()) {
        fin.seekg(-1,ios_base::end);                // go to one spot before the EOF

        bool keepLooping = true;
        while(keepLooping) {
            char ch;
            fin.get(ch);                            // Get current byte's data

            if((int)fin.tellg() <= 1) {             // If the data was at or before the 0th byte
                fin.seekg(0);                       // The first line is the last line
                keepLooping = false;                // So stop there
            }
            else if(ch == '\n') {                   // If the data was a newline
                keepLooping = false;                // Stop at the current position.
            }
            else {                                  // If the data was neither a newline nor at the 0 byte
                fin.seekg(-2,ios_base::cur);        // Move to the front of that data, then to the front of the data before it
            }
        }

        string lastLine;            
        getline(fin,lastLine);                      // Read the current line
        cout << "Result: " << lastLine << '\n';     // Display it
        fin.close();
    }
#endif
}

bool PiUtils::takePicture( std::string full_file_name, std::string link_name )
{
    PiPicture pic;
    pic._full_file_name = full_file_name;
    pic._full_link_name = link_name;
    bool success = takePicture( pic );
    return success;
}

bool PiUtils::takePicture( PiPicture& pipic )
{
//https://www.raspberrypi.org/forums/viewtopic.php?t=45235
// Initial crop of washer power:
// convert -crop 25x25+292+1592 lights_off_washer_off.jpg loff_washer_off.jpg
// Beside washer power:
// convert -crop 5x5+327+1602 lights_on_washer_on.jpg jpg:- | convert - -resize 1x1 txt:-
//compare -verbose -metric mae g20150812_134238_resize.jpg g20150812_070553_resize.jpg out.jpg
//convert test_10z.jpg -resize 1x1 txt:-
//# ImageMagick pixel enumeration: 1,1,255,srgb
//0,0: ( 90, 85, 91)  #5A555B  srgb(90,85,91)
//raspistill -vf -hf -n -v -o g150712_0812.jpg -l garage.jpg
//https://github.com/sdenton4/pipic
//http://inventingsituations.net/2014/01/01/pilapse3/
//http://raspberrypi.stackexchange.com/questions/12321/getting-correctly-exposed-photos-and-the-right-shutter-speed-for-the-rpi-camera

    //std::string cmd = "raspistill -vf -hf -n -o ";
    std::string cmd = "raspistill -n -o ";
    cmd.append( pipic._full_file_name );
std::cerr << cmd << "\n";
    PiUtils::runSystemCommand( cmd.c_str() );
    if ( !pipic._full_link_name.empty() )
    {
        cmd = "rm ";
        cmd.append( pipic._full_link_name );
        PiUtils::runSystemCommand( cmd.c_str(), 10 );
        cmd = "ln -s ";
        cmd.append( pipic._full_file_name );
        cmd.append( " " );
        cmd.append( pipic._full_link_name );
        PiUtils::runSystemCommand( cmd.c_str(), 10 );
    }
    // At this point, we should have the picture.
    // Convert to reduced
    bool resize = pipic._resize && ( pipic._resize_factor > 0 );
    if ( resize )
    {
        if ( !PiUtils::resizeImage( pipic ) )
        {
            std::cerr << "Failed to resize.\n";
        }
        else if ( !pipic._keep_full )
        {
            // Remove the full
            cmd = "rm ";
            cmd.append( pipic._full_file_name );
            PiUtils::runSystemCommand( cmd.c_str(), 10 );
        }
    }
    else
    {
        // TODO Do something with the link
    }
    return fileExists( pipic._full_file_name );
}

std::string PiUtils::getTextFormatAsString( TEXT_FORMAT text_format )
{
    switch( text_format )
    {
        case PiUtils::HTML:
            return "HTML";
        case PiUtils::CSV:
            return "CSV";
        case PiUtils::TXT:
        default:
            return "TXT";
    }
    return "TXT";
}

PiUtils::TEXT_FORMAT PiUtils::getTextFormatFromString( std::string str, TEXT_FORMAT def )
{
    if ( strcmp( str.c_str(), "TXT" ) == 0 )
    {
        return PiUtils::TXT;
    }
    if ( strcmp( str.c_str(), "HTML" ) == 0 )
    {
        return PiUtils::HTML;
    }
    if ( strcmp( str.c_str(), "CSV" ) == 0 )
    {
        return PiUtils::CSV;
    }
    return def;
}

std::string PiUtils::getTableCellStartDelimiter( TEXT_FORMAT format )
{
    switch( format )
    {
        case TXT:
        case CSV:
            return "";
        case HTML:
            return "<td>";
    }
    return "";
}

std::string PiUtils::getTableCellEndDelimiter( TEXT_FORMAT format )
{
    switch( format )
    {
        case TXT:
            return "\t";
        case CSV:
            return ",";
        case HTML:
            return "</td>";
    }
    return "";
}

std::string PiUtils::getTableStartDelimiter( TEXT_FORMAT format )
{
    switch( format )
    {
        case TXT:
        case CSV:
            return "";
        case HTML:
            return "<table border = 1>";
    }
    return "";
}

std::string PiUtils::getTableEndDelimiter( TEXT_FORMAT format )
{
    switch( format )
    {
        case TXT:
        case CSV:
            return "\n";
        case HTML:
            return "</table>\n";
    }
    return "";
}

std::string PiUtils::getTableLineStartDelimiter( TEXT_FORMAT format )
{
    switch( format )
    {
        case TXT:
        case CSV:
            return "";
        case HTML:
            return "<tr>";
    }
    return "";
}

std::string PiUtils::getTableLineEndDelimiter( TEXT_FORMAT format )
{
    switch( format )
    {
        case TXT:
        case CSV:
            return "\n";
        case HTML:
            return "</tr>\n";
    }
    return "";
}

std::string PiUtils::getStartDelimiter( TEXT_FORMAT format )
{
    switch( format )
    {
        case TXT:
            return "";
        case CSV:
            return "";
        case HTML:
            return "<html>";
    }
    return "";
}

std::string PiUtils::getEndDelimiter( TEXT_FORMAT format )
{
    switch( format )
    {
        case TXT:
            return "\t";
        case CSV:
            return ",";
        case HTML:
            return "</html>";
    }
    return "";
}

std::string PiUtils::getLineEndDelimiter( TEXT_FORMAT format )
{
    switch( format )
    {
        case TXT:
        case CSV:
            return "\n";
        case HTML:
            return "<br>\n";
    }
    return "";
}
#if 0
bool PiUtils::analyzePicture( PiPicture& pipic, float min_brightness, int max_exposure )
{
    return false;
    // Analyze the picture.  If it is too dark, brighten it up for the next shot
    //float _brightness_threshold;
    //float _max_exposure_time;
    //convert test_10z.jpg -resize 1x1 txt:-
    std::string cmd = "convert ";
    cmd.append( pipic._full_file_name );
    cmd.append( " -resize 1x1 txt:-" );
    int buf_size = 2048;
    char buf[buf_size];
    FILE *fp;

    if ((fp = popen(cmd.c_str(), "r")) == NULL) {
        printf("Error opening pipe!\n");
	return false;
    }

    // Output:
    //# ImageMagick pixel enumeration: 1,1,255,srgb
    //0,0: ( 90, 85, 91)  #5A555B  srgb(90,85,91)
    while (fgets(buf, buf_size, fp) != NULL) {
        // Do whatever you want here...
        if ( buf[0] == '#' )
        {
            // Ignore
            printf("IGNORED OUTPUT: %s", buf);
            continue;
        }
        printf("OUTPUT: %s", buf);
        // Parse the buffer
        // It is of the format email@address,subject
        std::string output = buf;
        std::string::size_type pos = output.find( ',' );
        if ( pos == std::string::npos )
        {
            printf( "Invalid result string: " );
            printf( output.c_str() );
        }
        else
        {
            // Send a status e-mail
            GarageMonitorUtils::CommandOptions command;
            command._command = GarageMonitorUtils::SEND_STATUS_EMAIL;
            command._email_address = output.substr( 0, pos );
            _garage->sendCommand( command );
        }
    }
    int ret = pclose(fp);
}
#endif



bool PiUtils::resizeImage( PiPicture& pipic )
{
    if ( !pipic._resize )
    {
        return false;
    }
    if ( pipic._resize_factor <= 0.0 )
    {
        return false;
    }
    if ( pipic._full_file_name.empty() )
    {
        return false;
    }
    if ( pipic._resize_file_name.empty() )
    {
        return false;
    }

    std::string cmd = "convert ";
    cmd.append( pipic._full_file_name );
    cmd.append( " -resize " );
    cmd.append( tostr( pipic._resize_factor * 100.0 ) );
    cmd.append( "% " );
    cmd.append( pipic._resize_file_name );
std::cerr << cmd << "\n";
    PiUtils::runSystemCommand( cmd.c_str(), 60 );

    if ( !fileExists( pipic._resize_file_name ) )
    {
        return false;
    }
    if ( !pipic._resize_link_name.empty() )
    {
        cmd = "rm ";
        cmd.append( pipic._resize_link_name );
        PiUtils::runSystemCommand( cmd.c_str(), 10 );
        cmd = "ln -s ";
        cmd.append( pipic._resize_file_name );
        cmd.append( " " );
        cmd.append( pipic._resize_link_name );
        PiUtils::runSystemCommand( cmd.c_str(), 10 );
    }
    return true;
}

void PiUtils::connectBackdoorToHost( std::string hostname_ipaddress, std::string username, int port, std::string remote_command_to_run, bool keepalive )
{
    // ssh 
    std::string command = "ssh ";
    command += username;
    command += "@";
    command += hostname_ipaddress;
    command += " -R";
    command += tostr( port );
    command += ":127.0.0.1:22 ";
    command += remote_command_to_run;
    do
    {
        //std::cerr << "Would have run: \n";
        std::cerr << "Running: \n";
        std::cerr << command.c_str();
        //std::cerr << "\nBut didn't\n";
        PiUtils::runSystemCommand( command.c_str() );
        if ( keepalive )
        {
            sleep( 60 );
        }
    } while( keepalive );
}

bool PiUtils::syncDir( std::string hostname_ipaddress, std::string username, std::string local_dir, std::string remote_dir )
{
    std::string command = "rsync -avP ";
    command += local_dir;
    command += " ";
    command += username;
    command += "@";
    command += hostname_ipaddress;
    command += ":";
    command += remote_dir;
    std::cerr << "rsync command: ";
    std::cerr << command;
    std::cerr << "\n";
    int ret = PiUtils::runSystemCommand( command.c_str(), 60 );
    if ( WEXITSTATUS(ret) == 0 )
    {
        return true;
    }
    return false;
}

bool PiUtils::checkMessage( std::string install_dir, std::string message_label, std::string& result_str )
{
    result_str = "";
    std::string cmd = "sudo /usr/bin/timeout -k 60 30 sudo python ";
    cmd.append( install_dir );
    cmd.append( "/home/utils/python/get_email.py" );
    cmd.append( " " );
    cmd.append( message_label.c_str() );

    bool success = false;
    while( true )
    {
        int buf_size = 2048;
        char buf[buf_size];
        FILE *fp;

        if ((fp = popen(cmd.c_str(), "r")) == NULL) {
            printf("Error opening pipe!\n");
            success = false;
            break;
        }

        while (fgets(buf, buf_size, fp) != NULL) {
            // Do whatever you want here...
            //printf("OUTPUT: %s", buf);
            //printf( "Send mail." );
            // Parse the buffer
            // It is of the format email@address,subject
            result_str.append( buf );
        }
        int ret = pclose(fp);
        success = ( WEXITSTATUS(ret) == 0 );
        success = true;
        break;
    }
    fflush( stdout );
    return success;
}

bool PiUtils::checkMessageJSON( std::string install_dir, std::string message_label, std::vector< email_response >& messages, std::vector<std::string>& raw_json )
{
    raw_json.clear();
    messages.clear();
    /*
    std::string cmd = "sudo /usr/bin/timeout -k 60 30 sudo python ";
    cmd.append( install_dir );
    cmd.append( "/home/utils/python/get_email_json.py" );
    cmd.append( " " );
    cmd.append( message_label.c_str() );
    */
    std::string cmd = "cat ";
    cmd.append( install_dir );
    cmd.append( "/home/utils/python/text.txt" );

    bool success = false;
    while( true )
    {
        int buf_size = 204800; // Could have a big body
        char buf[buf_size];
        FILE *fp;

        if ((fp = popen(cmd.c_str(), "r")) == NULL) {
            printf("Error opening pipe!\n");
            success = false;
            break;
        }

        while (fgets(buf, buf_size, fp) != NULL) {
            // Each line should be a json line. Note it could be long if there is a big body
            // Parse out the sender, subject and body.
            email_response resp;
            std::string str = buf;
            json j = json::parse( str );
            resp._sender = j.at( "sender" );
            resp._subject = j.at( "subject" );
            resp._body = j.at( "body" );
            // Return the raw_json as well if the user wants it
            messages.push_back( resp );
            raw_json.push_back( buf );
        }
        int ret = pclose(fp);
        success = ( WEXITSTATUS(ret) == 0 );
        success = true;
        break;
    }
    fflush( stdout );
    return success;
}
#if 0
int PiUtils::checkMessage( std::string install_dir, std::string message_label )
{
    while( true )
    {
        std::string cmd = "sudo python ";
        cmd.append( install_dir );
        cmd.append( "/home/utils/python/get_email.py" );
#if 0

        while( true )
        {
            int buf_size = 2048;
            char buf[buf_size];
            FILE *fp;

            if ((fp = popen(cmd.c_str(), "r")) == NULL) {
                printf("Error opening pipe!\n");
                break;
            }

            while (fgets(buf, buf_size, fp) != NULL) {
                // Do whatever you want here...
                //printf("OUTPUT: %s", buf);
                printf( "Send mail." );
                // Parse the buffer
                // It is of the format email@address,subject
                std::string output = buf;
                std::string::size_type pos = output.find( ',' );
                if ( pos == std::string::npos )
                {
                    printf( "Invalid result string: " );
                    printf( output.c_str() );
                }
                else
                {
                    // Send a status e-mail
                    GarageMonitorUtils::CommandOptions command;
                    command._command = GarageMonitorUtils::SEND_STATUS_EMAIL;
                    command._email_address = output.substr( 0, pos );
                    _garage->sendCommand( command );
                }
            }
            int ret = pclose(fp);
#if 0
            if ( WEXITSTATUS(ret) == 0 )
            {
                // Do nothing
                printf( "Do nothing." );
            }
            else if ( WEXITSTATUS(ret) == 1 )
            {
                printf( "Send emial." );
            }
#endif
            break;
        }
#endif
        fflush( stdout );
        sleep( 60 );
    }
}
#endif

bool PiUtils::parseConfigFile( std::string fn, std::map< std::string, std::string >& key_value )
{
    std::cerr << "Going to parseConfigFile with file: " << fn << "\n";
    // Read in the config file, then load up any sensors we need to load up.
    std::ifstream infile( fn.c_str() );
    std::string line = "";
    bool success = true;
    while (getline(infile, line)){
        std::cerr << "line: " << line << "\n";
        std::vector<std::string> all_words;
        if ( line[0] == '#' )
        {
            //std::cout << "Comment line: " << line << "\n";
            continue;
        }

        std::stringstream strstr(line);
        std::string word = "";
        while (getline(strstr,word, ',')) all_words.push_back(word);
        bool valid = all_words.size() == 2;
        if ( !valid )
        {
            std::cout << "PiUtils::parseConfigFile Invalid line: " << line << "\n";
            continue;
        }
        std::string key = all_words[0].c_str();
        std::string value = all_words[1].c_str();
        key_value[ key ] = value;
    }
    return success;
}

bool getCurrentPath( std::string &path)
{
    char cwd[PATH_MAX];
    if ( getcwd(cwd, sizeof(cwd)) != NULL )
    {
        path = cwd;
        return true;
    }
    path = "";
    return false;
}

#include <iostream>
#include <memory>
#include <stdexcept>
#include <array>
#include <cstdlib>  // for WEXITSTATUS, WIFEXITED

struct ScriptResult {
    std::string output;
    int exit_code;
};

ScriptResult runPythonScript(const std::string& command) {
    std::array<char, 128> buffer;
    std::string result;
    int exit_code = -1;

    //std::string new_command = "source /home/pi/github/home_manager/.venv/bin/activate && ";
    //new_command.append( command );
    // Open a pipe to the Python script
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }

    // Read the output of the script
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

    // Get the exit status of the script
    int status = pclose(pipe);
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    }

    return {result, exit_code};
}

void appendToFile(const std::string& filename, const std::string& text) {
    // Open the file in append mode
    std::ofstream file(filename, std::ios_base::app);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to open file for appending");
    }
    
    // Write the text to the file
    file << text;
}
/*
int main() {
    std::string command = "python3 your_script.py";
    ScriptResult result = runPythonScript(command);
    
    std::cout << "Python Script Output: " << result.output << std::endl;
    std::cout << "Exit Code: " << result.exit_code << std::endl;

    return 0;
}
*/

int PiUtils::runSystemCommand( std::string command, int timeout, bool echo_command, std::string dir )
{
    //std::string cmd = "source /home/pi/github/home_manager/.venv/bin/activate && ";
    //cmd.append( command );
    //command = cmd;
    echo_command = true;
    std::string curr_dir = "";
    if ( !dir.empty() )
    {
        getCurrentPath( curr_dir );
        std::cerr << "Curr dir: " << curr_dir << "\n";
        chdir( dir.c_str() );
        std::string tmp_dir = "";
        getCurrentPath( tmp_dir );
        std::cerr << "New dir: " << tmp_dir << "\n";
    }
    else
    {
        std::string tmp_dir = "";
        getCurrentPath( tmp_dir );
        std::cerr << "Dir is empty: " << tmp_dir << "\n";
    }
    if ( ( timeout > 0 ) && false )
    {
        std::string new_command = "";
        std::string first_word = command.substr( 0, 4 );
        if ( strcmp( first_word.c_str(), "sudo" ) == 0 )
        {
            new_command.append( "sudo " );
        }
        new_command.append( "/usr/bin/timeout " );
        new_command.append( " -k" );
        new_command.append( PiUtils::tostr( timeout*2 ) );
        new_command.append( " " );
        new_command.append( PiUtils::tostr( timeout ) );
        new_command.append( " " );
        new_command.append( command );
        command = new_command;
    }
    if ( echo_command )
    {
        std::cerr << "Going to run command: " << command << "\n";
    }
    int ret = 0;
    if ( false )
    {
        ret = WEXITSTATUS(system( command.c_str() ));  
    }
    else
    {
        std::cerr << "Going to run the script: " << command.c_str() << "\n" << std::endl;
        ScriptResult result = runPythonScript(command.c_str());
    
        std::cerr << "Python Script Output: " << result.output << std::endl;
        std::cerr << "Exit Code: " << result.exit_code << std::endl;
        ret = result.exit_code;
    }
    if ( echo_command )
    {
        std::cerr << "The result is: " << ret << ".\n";
    }
    if ( !curr_dir.empty() )
    {
        chdir( curr_dir.c_str() );
    }
    return ret;
}

unsigned int PiUtils::timeLeftToSleep( time_t start_time, unsigned int requested_duration, unsigned int min_value )
{
    time_t cur_time;
    time (&cur_time);
    unsigned int elapsed = (unsigned int) cur_time - (unsigned int) start_time;
    if ( ( elapsed > requested_duration ) || ( ( requested_duration - elapsed ) < min_value ) )
    {
        return min_value;
    }
    return requested_duration - elapsed;
}

unsigned int PiUtils::getDayOfYear( time_t datum_time, int offset )
{
    struct tm* _time = localtime(&datum_time);
    int day = _time->tm_yday + offset;
    int num_days_in_year = 365;
    if ( day >= num_days_in_year ) { day -= num_days_in_year; }
    if ( day < 0 ) { day += num_days_in_year; }
    return ( (unsigned int) day );
}

unsigned int PiUtils::getHour( time_t datum_time, int offset )
{
    struct tm* _time = localtime(&datum_time);
    int hour = _time->tm_hour + offset;
    if ( hour >= 24 ) { hour -= 24; }
    if ( hour < 0 ) { hour += 24; }
    return ( (unsigned int) hour );
}

unsigned int PiUtils::getMinute( time_t datum_time, int offset )
{
    struct tm* _time = localtime(&datum_time);
    int hour = _time->tm_min + offset;
    if ( hour >= 60 ) { hour -= 60; }
    if ( hour < 0 ) { hour += 60; }
    return ( (unsigned int) hour );
}

std::vector<std::string> &PiUtils::split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}


std::vector<std::string> PiUtils::split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, elems);
    return elems;
}

time_t PiUtils::getTimeInAdditionHourMinuteSeconds( int days, int hours, int minutes, int seconds, time_t start_time )
{
    std::chrono::system_clock::time_point time_now = std::chrono::system_clock::now();
    if ( start_time > 0 )
        time_now = std::chrono::system_clock::from_time_t( start_time );
    time_now += std::chrono::hours(days * 24 + hours);
    time_now += std::chrono::minutes(minutes);
    time_now += std::chrono::seconds(seconds);
    return std::chrono::system_clock::to_time_t( time_now );
}

time_t PiUtils::getNextTimeFromHourMinute( int hour, int minute, int additional_days, int repeat_seconds )
{
    std::string date_str = "";
    //struct tm* cur_time = PiUtils::getCurrentTime( );
    int wday = PiUtils::getCurrentTime()->tm_wday;
    wday += additional_days;
    while ( wday > 7 )
    {
        wday -= 7;
    }
    while ( wday < 0 )
    {
        wday += 7;
    }
    switch( wday )
    {
        case 0: 
            date_str = "Sunday";
            break;
        case 1: 
            date_str = "Monday";
            break;
        case 2: 
            date_str = "Tuesday";
            break;
        case 3: 
            date_str = "Wednesday";
            break;
        case 4: 
            date_str = "Thursday";
            break;
        case 5: 
            date_str = "Friday";
            break;
        case 6: 
            date_str = "Saturday";
            break;
    }
    date_str += " ";
    if ( hour < 10 )
    {
        date_str += "0";
    }
    date_str += PiUtils::tostr( hour );
    date_str += ":";
    if ( minute < 10 )
    {
        date_str += "0";
    }
    date_str += PiUtils::tostr( minute );
    date_str += ":00";
    struct tm* cmd_time = new tm;
    getdate_r( date_str.c_str(), cmd_time );
    if ( NULL != cmd_time )
    {
        //error?
    }
    time_t ret = mktime( cmd_time );
    std::cerr << "Going to get the time for: " << date_str << ": ret " << ret << "\n";
    delete cmd_time;
    if ( repeat_seconds > 0 )
    {
        Logger::log( Logger::TRACE, "PiUtils checking for earlier time");
        std::string curr_time_str;
        PiUtils::getYYMMDD_HHMMSSDisplayTime( PiUtils::getCurrentTime_t(), curr_time_str );
        while ( ret > PiUtils::getCurrentTime_t() )
        {
            time_t new_time = PiUtils::getTimeInAdditionHourMinuteSeconds( 0, 0, 0, (-1) * repeat_seconds, ret );
            std::string new_time_str;
            PiUtils::getYYMMDD_HHMMSSDisplayTime( new_time, new_time_str );
            std::string planned_time_str;
            PiUtils::getYYMMDD_HHMMSSDisplayTime( ret, planned_time_str );
            Logger::log( Logger::TRACE, "JSONUtils::getTimeFromJSON currently planned time " + planned_time_str + " checking if time " + new_time_str + " is less than current time " + curr_time_str );
            if ( new_time < PiUtils::getCurrentTime_t() )
            {
                break;
            }
            ret = new_time;
        }
    }
    return ret;
}

std::string PiUtils::getDirectoryFromFilename( const std::string& str )
{
    size_t loc = str.find_last_of("/");
    std::string directory = str.substr(0, loc );
    Logger::log( Logger::TRACE, "Directory is: " + directory );
    return directory;
}

std::string PiUtils::addDirectoryToFile( std::string dir, std::string file )
{
    Logger::log( Logger::TRACE, "PiUtils::addDirectoryToFile: dir " + dir );
    if ( dir.empty() )
        return file;
    return ( dir + "/" + file );
}

std::string PiUtils::addFullDirectoryToFile( std::string file )
{
    Logger::log( Logger::TRACE, "PiUtils::addFullDirectoryToFile: " + file );
    std::string str = "";
    getCurrentPath( str );
    str += "/";
    str += file;
    return str;
}

void PiUtils::addDirectoryToFiles( std::string dir, std::vector< std::string >& files )
{
    Logger::log( Logger::TRACE, "PiUtils::addDirectoryToFiles: dir " + dir );
    if ( dir.empty() )
        return;
    std::vector< std::string > new_files;
    for( std::vector< std::string >::iterator itr = files.begin(); itr != files.end(); ++itr )
    {
        new_files.push_back( addDirectoryToFile( dir, *itr ) );
    }
    files.swap( new_files );
}

void PiUtils::roundFloat( const int digits, const float val, std::string& res )
{
    if ( digits != 2 )
        return;
    // we use array of chars to store number
    // as a string.
    char str[40]; // TODO thread safe?

    // Print in string the value of var
    // with two decimal point
    sprintf(str, "%.2f", val);
    res += str;
}

std::string PiUtils::getInternalIPAddress()
{
    struct ifaddrs *ifaddr, *ifa;
    char addr[INET_ADDRSTRLEN] = {0};
    std::string ip = "";

    if (getifaddrs(&ifaddr) == -1)
        return "";

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr)
            continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            void* in_addr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, in_addr, addr, INET_ADDRSTRLEN);
            std::string candidate(addr);
            if (candidate != "127.0.0.1") {
                ip = candidate;
                break;
            }
        }
    }
    freeifaddrs(ifaddr);
    return ip;
}
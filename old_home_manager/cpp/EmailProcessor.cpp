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

#include "EmailProcessor.h"
#include "EmailSender.h"
#include "HomeEvent.h"
#include "HomeConfig.h"
#include "HomeSystem.h"
#include "Device.h"
#include "HomeEventProcessors.h"
#include "JSONUtils.h"
#include "Logger.h"

using namespace JSONUtils;


/*
    Commands:
        - The commands can either be in the subject or the first line of the body.
    Status
        - Gives a description of all devices and alert conditions
        - Gives current evaluations
    mute
        - This will be proceeded by a time. By default, it will be minutes (and 60 minutes if no number). Can be appended with s/seconds, m/minutes, h/hours, d/days, p/permanent
        - Note that permanent is just until the program restarts.
        - When something is muted, it will still log that is has triggered, but will be noted as muted.
        - An e-mail will be sent when the muting is accepted as well as when it is finished.
    - In the future, it will also allow running other commands
*/

// This will check for emails. It will get back a json with 0, 1 or more messages.
bool readEmail( HomeSystem* system )
{
    Logger::log( Logger::WARNING, "HomeEventProcessor::readEmail: UNIMPLEMENTED ");
    return false;
}

bool sendEmail( HomeSystem* system, json email )
{
    Logger::log( Logger::TRACE, "HomeEventProcessor::sendEmail");
    // write json file to disk
    // call script and pass in the file.
    // if the script fails, add in an email retry
    time_t time = PiUtils::getCurrentTime_t();
    std::string time_str;
    PiUtils::getYYMMDD_HHMMSSDisplayTime( time, time_str );
    if ( email.contains( J_ADD_STATUS_HTML ) && email[ J_ADD_STATUS_HTML ] )
    {
        json options;
        options[ J_S_TIME_EVALUATIONS_S ] = 60*60; // 1 hour
        options[ J_S_TIME_EVALUATION_STATS_S ] = 24*60*60; // 1 day
        options[ J_S_GENERATE_SUBJECT ] = false;
        options[ J_S_CONSTRUCT_BODY ] = true;
        options[ J_ADD_SYSTEM_INFO ] = JSONUtils::getJSONBoolValue( email, J_ADD_SYSTEM_INFO, true );
        system->getEvaluationStore()->generateStatus( time, email, options, PiUtils::HTML );
    }
    std::string email_filename;
    std::string sent_email_filename;
    unsigned int counter = 0;
    do
    {
        email_filename = "email_" + time_str;
        if ( counter > 0 )
        {
            email_filename.append( "_" );
            email_filename.append( std::to_string( counter ) );
        }
        ++counter;
        email_filename.append( ".json" );
        sent_email_filename = system->getHomeConfig()->getEmailBaseDir() + "/sent" + "/" + email_filename;
        email_filename = system->getHomeConfig()->getEmailBaseDir() + "/" + email_filename;
    } while (PiUtils::fileExists( email_filename ) || PiUtils::fileExists( sent_email_filename ) );
    
	std::fstream email_file;
    email_file.open(email_filename, std::ios_base::app | std::ios_base::in );
    if ( email_file.is_open() )
    {
        email_file << email.dump( 2 );
        email_file.close();
    }
    else
    {
        Logger::log( Logger::ERROR, "HomeEventProcessor::sendEmail: can't open email file: " + email_filename);
        return false;
    }
    #if 0
    // Try and send through EmailSender.cpp
    bool success = EmailSender::sendFromJson(email);
    if ( success )
    {
        Logger::log( Logger::TRACE, "HomeEventProcessor::EmailSender::sendFromJson: sent email : " + email.dump(2) + " with subject: " + JSONUtils::getJSONStringValue( email, J_EMAIL_SUBJECT, "MISSING SUBJECT" ) );
    }
    else
    {
        Logger::log( Logger::WARNING, "HomeEventProcessor::EmailSender::sendFromJson: failed to send e-mail");
    }
    #else
    std::string dir = "";
    std::string cmd = "";
    {
        std::string send_email_script = system->getHomeConfig()->getSendEmailPythonFile();
        if( send_email_script.empty() )
        {
            Logger::log( Logger::ERROR, "HomeEventProcessor::sendEmail Missing send email script" );
            return false;
        }
        cmd = "python3 ";
        cmd.append( send_email_script );
        cmd.append( " --json " );
        cmd.append( email_filename );
    }

    Logger::log( Logger::TRACE, "HomeEventProcessor::sendEmail: " + cmd );
    int ret = PiUtils::runSystemCommand( cmd.c_str(), 120, false );
    bool success  = ( WEXITSTATUS(ret) == 0 );
    #endif
    if ( success )
    {
        // Move the email file to "sent"
        std::string mv = "mv ";
        mv += email_filename;
        mv += " ";
        mv += sent_email_filename;
        PiUtils::runSystemCommand( mv.c_str(), 120 );
        Logger::log( Logger::TRACE, "sendEmail: sent email : " + email_filename + " with subject: " + JSONUtils::getJSONStringValue( email, J_EMAIL_SUBJECT, "MISSING SUBJECT" ) );
    }
    else
    {
        Logger::log( Logger::WARNING, "HomeEventProcessor::sendEmail: failed to send e-mail"  + email_filename);
    }

    return success;
}

bool EmailProcessor::checkEmail( HomeSystem* system, json email_options, json& received_emails )
{
    Logger::log( Logger::TRACE, "HomeEventProcessor::checkEmail" );
    std::string dir = "";
    std::string cmd = "";
    if (email_options.contains( J_EMAIL_CHECK_LABEL ))
    {
        std::string get_email_script = system->getHomeConfig()->getGetEmailPythonFile();
        if( get_email_script.empty() )
        {
            Logger::log( Logger::ERROR, "HomeEventProcessor::checkEmail Missing get email script" );
            return false;
        }
        cmd = "python3 ";
        cmd.append( get_email_script );
        cmd.append( " --tag " );
        cmd.append( email_options[J_EMAIL_CHECK_LABEL] );
    }
    else
    {
        Logger::log( Logger::ERROR, "HomeEventProcessor::checkEmail Missing tag" );
        return false;
    }

    Logger::log( Logger::TRACE, "HomeEventProcessor::checkEmail: " + cmd );

    bool success = false;
    while( true ) // Use a while loop for easy breaking
    {
        int buf_size = 204800; // Could have a big body
        char buf[buf_size];
        FILE *fp;

        if ((fp = popen(cmd.c_str(), "r")) == NULL) {
            Logger::log( Logger::TRACE, "HomeEventProcessor::checkEmail error opening pipe: " + cmd );
            success = false;
            break;
        }

        if (fgets(buf, buf_size, fp) != NULL) {
            // Each line should be a json line. Note it could be long if there is a big body
            // Parse out the sender, subject and body.
            json resp;
            std::string str = buf;
            Logger::log( Logger::TRACE, "HomeEventProcessor::checkEmail read: " + str );
            received_emails = json::parse( str );
        }
        int ret = pclose(fp);
        success = ( WEXITSTATUS(ret) == 0 );
        success = true;
        break;
    }
    fflush( stdout );
    return success;
}

bool EmailProcessor::processSendEmail( HomeEvent* event, HomeSystem* system )
{
    json event_info = event->getEventInfo();
    Logger::log( Logger::TRACE, "HomeEventProcessor::processSendEmail start with event_info: " + event_info.dump() );

    if ( !event_info.contains( J_EMAIL_INFO ) )
    {
        Logger::log( Logger::ERROR, "HomeEventProcessor::processSendEmail Missing TO or SUBJECT" );
        return false;
    }
    bool success = sendEmail( system, event_info.at( J_EMAIL_INFO ) );
    if ( !success )
    {
        // Do we retry?
        if ( JSONUtils::getJSONIntValue( event_info, J_EMAIL_RETRY_COUNT, 0 ) <= system->getHomeConfig()->getEmailRetryLimit() )
        {
            Logger::log( Logger::WARNING, "HomeEventProcessor::processSendEmail: Send a retry.");
            json event_json = event->getEventInfo();
            // Go ahead and retry
            event_info[ J_EMAIL_RETRY_COUNT ] = JSONUtils::getJSONIntValue( event_info, J_EMAIL_RETRY_COUNT, 0 ) + 1;
            event_json[ J_TIME ] = PiUtils::getTimeInAdditionHourMinuteSeconds( 0, 0, 0, system->getHomeConfig()->getEmailRetryFrequencySeconds() );
            system->addHomeEvent( event_json, HomeEvent::SYSTEM );
        }
        else
            Logger::log( Logger::ERROR, "HomeEventProcessor::processSendEmail: failed to send e-mail. No retries left.");
    }
    return success;
}

bool EmailProcessor::processGetEmail( HomeSystem* system )
{
    Logger::log( Logger::INFO, "HomeEventProcessor::processGetEmail: UNIMPLEMENTED.");
    return false;
}

bool EmailProcessor::sendStartupStatusEmail( bool is_startup, json event_info, json status_info, HomeSystem* system, HomeEvent::EVENT_ORIGINATOR_TYPE event_originator_type )
{
    // Format it properly
    // See if event_info has e-mail info. If so, use that.
    // If not, get the default e-mail info from the system.
    json email;
    if ( event_info.contains( J_EMAIL_INFO ) )
        email = event_info.at( J_EMAIL_INFO );
    else
        email = system->getHomeConfig()->getEmailInfo();

    Logger::log( Logger::TRACE, "EmailProcessor::sendStartupStatusEmail email:" + email.dump( 2 ) );
    json status_email_event;
    status_email_event[ J_VERSION ] = 2;
    status_email_event[ J_TIME_IMMEDIATE ] = true; // Make sure it happens this cycle
    status_email_event[ J_EVENT_PROCESSOR_TYPE ] = event_originator_type;

    json status_email;
    status_email[ J_EMAIL_TO ] = JSONUtils::getJSONArrayValue( email, J_STATUS_ADDRESSES );
    status_email[ J_EMAIL_SUBJECT ] = JSONUtils::getJSONStringValue( status_info, J_EMAIL_SUBJECT, "" );
    if (is_startup) {
        status_email[ J_EMAIL_BODY ] = JSONUtils::getJSONStringValue( status_info, J_EMAIL_HEADER, "" ) +
                                   JSONUtils::getJSONStringValue( status_info, J_EMAIL_CURR_TABLE, "" ) +
                                   JSONUtils::getJSONStringValue( status_info, J_EMAIL_STATS_TABLE, "" ) +
                                   JSONUtils::getJSONStringValue( status_info, J_EMAIL_FOOTER, "" );

    } else {
        status_email[ J_EMAIL_BODY ] = JSONUtils::getJSONStringValue( status_info, J_EMAIL_BODY, "Missing body" );
    }
    status_email[ J_EMAIL_FROM ] = JSONUtils::getJSONStringValue( email,  J_EMAIL_FROM, "" );

    status_email_event[ J_EMAIL_INFO ] = status_email;
    status_email_event[ J_EVENT_PROCESSOR_TYPE ] = HomeEventProcessor::getEventProcessorTypeAsString( HomeEventProcessor::EMAIL );
    
    system->addHomeEvent( status_email_event, event_originator_type );


    return true;
}

bool EmailProcessor::processCommandEmailEvent( HomeEvent* event, HomeSystem* system )
{
    Logger::log( Logger::TRACE, "EmailProcessor::processCommandEmailEvent" );

    json results;
    time_t time = PiUtils::getCurrentTime_t();
    json event_info = event->getEventInfo();
    DeviceUtils::DEVICE_COMMAND cmd = DeviceUtils::getCommandFromString( JSONUtils::getJSONStringValue( event_info, J_COMMAND, DeviceUtils::getCommandAsString( DeviceUtils::INVALID_DEVICE_COMMAND ) ) );
    json options;
    switch( cmd )
    {
        case DeviceUtils::GET_STATUS:
            options[ J_S_TIME_EVALUATIONS_S ] = 60*60;
            options[ J_S_GENERATE_SUBJECT ] = true;
            options[ J_S_CONSTRUCT_BODY ] = true;
            options[ J_ADD_SYSTEM_INFO ] = true;
            if ( system->getEvaluationStore()->generateStatus( time, results, options, PiUtils::HTML ) )
            {
                sendStartupStatusEmail( false, event_info, results, system, event->getOriginatorType() );
            }
            else
            {
                Logger::log( Logger::ERROR, "FAILED EmailProcessor::processCommandEmailEvent: GET_STATUS");
            }
            break;
        case DeviceUtils::SEND_STARTUP:
            options[ J_S_TIME_EVALUATIONS_S ] = 60;
            options[ J_S_GENERATE_SUBJECT ] = true;
            options[ J_S_CONSTRUCT_BODY ] = true;
            options[ J_ADD_SYSTEM_INFO ] = true;
            if ( system->getEvaluationStore()->generateStatus( time, results, options, PiUtils::HTML, true ) )
            {
                sendStartupStatusEmail( true, event_info, results, system, event->getOriginatorType() );
            }
            else
            {
                Logger::log( Logger::ERROR, "FAILED EmailProcessor::processCommandEmailEvent: SEND_STARTUP");
            }
            break;
       default:
            Logger::log( Logger::WARNING, "EmailProcessor::processEmailEvent: Invalid command: " + event_info.dump() );
            return false;
            break;
    }
    return true;
}
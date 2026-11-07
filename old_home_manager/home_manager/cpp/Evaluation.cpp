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

#include "Device.h"
#include "HomeConfig.h"
#include "HomeEventProcessors.h"
#include "HomeSystem.h"
#include "JSONUtils.h"
#include "Logger.h"
#include "PiUtils.h"

using namespace DeviceUtils;
using namespace JSONUtils;

namespace Evaluation
{

json evaluation_stats::asJson() const
{
    json all;
    all[ J_DEVICE ] = device->getName();
    all[ J_STATS ] = evaluation_stat_json;
    all[ J_METADATA ] = evaluation_metadata;
    all[ J_VALID ] = is_valid;
    std::string str;
    PiUtils::getYYMMDD_HHMMSSDisplayTime( start_time, str );
    all[ J_START_TIME ] = str;
    PiUtils::getYYMMDD_HHMMSSDisplayTime( end_time, str );
    all[ J_END_TIME ] = str;
    return all;
}

EvaluationStore::EvaluationStore( HomeSystem* system )
    : _system( system )
    , _logging_enabled( false )
    , _logging_location( "" )
    , _last_stats_time( 0 )
    , _cur_stats_cycle_end( 0 )
{
    _evaluations.clear();
    _evaluation_stats.clear();
}

EvaluationStore::~EvaluationStore()
{
}

std::string getEvaluationsHeader( std::vector< device_evaluation > evaluations, PiUtils::TEXT_FORMAT format, bool include_date_time )
{
    Logger::log( Logger::TRACE, "EvaluationStore::getEvaluationsHeader" );
    std::string result = "";
    result += getTableLineStartDelimiter( format );
    for( std::vector< device_evaluation >::const_iterator itr = evaluations.begin(); itr != evaluations.end(); ++itr )
    {
        if ( include_date_time && ( itr == evaluations.begin() ) )
        {
            result += getTableCellStartDelimiter( format );
            result += "Date/Time";
            result += getTableCellEndDelimiter( format );
        }
        for( json::const_iterator j_itr = (*itr).evaluation_results.begin(); j_itr != (*itr).evaluation_results.end(); ++j_itr )
        {
            result += getTableCellStartDelimiter( format );
            result += j_itr.key();
            result += getTableCellEndDelimiter( format );
        }
    }
    result += getTableLineEndDelimiter( format );
    return result;
}

std::string getEvaluationsLine( std::vector< device_evaluation > evaluations, PiUtils::TEXT_FORMAT format, bool include_date_time, time_t eval_time )
{
    Logger::log( Logger::TRACE, "EvaluationStore::getEvaluationsLine" );
    std::string result = "";
    result += getTableLineStartDelimiter( format );
    for( std::vector< device_evaluation >::const_iterator itr = evaluations.begin(); itr != evaluations.end(); ++itr )
    {
        if ( include_date_time && ( itr == evaluations.begin()) )
        {
            std::string time_str;
            PiUtils::getYYMMDD_HHMMSSDisplayTime( eval_time, time_str );
            result += getTableCellStartDelimiter( format );
            result += time_str;
            result += getTableCellEndDelimiter( format );
        }
        for( json::const_iterator j_itr = (*itr).evaluation_results.begin(); j_itr != (*itr).evaluation_results.end(); ++j_itr )
        {
            Logger::log( Logger::TRACE, j_itr.value().dump() );
            result += getTableCellStartDelimiter( format );
            if ( j_itr.value().type() == json::value_t::number_float )
                PiUtils::roundFloat( 2, j_itr.value(), result );
            else
                result += j_itr.value().dump();
            result += getTableCellEndDelimiter( format );
        }
    }
    result += getTableLineEndDelimiter( format );
    Logger::log( Logger::TRACE, "EvaluationStore::getEvaluationsLine done line" );
    return result;
}

std::string getEvaluationStatsHeader( std::vector< evaluation_stats > stats, PiUtils::TEXT_FORMAT format, bool include_date_time )
{
    Logger::log( Logger::TRACE, "EvaluationStore::getEvaluationStatsHeader" );
    std::string result = "";
    result += getTableLineStartDelimiter( format );
    Logger::log( Logger::TRACE, "EvaluationStore::getEvaluationStatsHeader dump all" );
    for( std::vector< evaluation_stats >::const_iterator itr = stats.begin(); itr != stats.end(); ++itr )
    {
        if ( include_date_time && ( itr == stats.begin() ) )
        {
            result += getTableCellStartDelimiter( format );
            result += "Date/Time";
            result += getTableCellEndDelimiter( format );
        }
        json json_result = (*itr).evaluation_stat_json;
        Logger::log( Logger::TRACE, "EvaluationStore::getEvaluationStatsHeader result: " + json_result.dump() );
        Logger::log( Logger::TRACE, "EvaluationStore::getEvaluationStatsHeader Going to get " + J_PROPERTY_LONG_NAME + " and " + J_PROPERTY_TYPE );
        std::string name = json_result[ J_PROPERTY_LONG_NAME ];
        Logger::log( Logger::TRACE, "EvaluationStore::getEvaluationStatsHeader name " + name );
        switch( DeviceUtils::getPropertyTypeFromString( json_result[J_PROPERTY_TYPE] ) )
        {
            case TYPE_FLOAT:
            {
                Logger::log( Logger::TRACE, "EvaluationStore::getEvaluationStatsHeader type is float");
                result += getTableCellStartDelimiter( format );
                result += name;
                result += " min";
                result += getTableCellEndDelimiter( format );
                result += getTableCellStartDelimiter( format );
                result += name;
                result += " max";
                result += getTableCellEndDelimiter( format );
                break;
            }
            case TYPE_INTEGER:
            case TYPE_BOOL:
            case TYPE_STATE:
            default:
                Logger::log( Logger::TRACE, "EvaluationStore::getEvaluationStatsHeader type is not float");
                result += getTableCellStartDelimiter( format );
                result += name;
                result += " TODO";
                result += getTableCellEndDelimiter( format );
                break;
        }
    }
    result += getTableLineEndDelimiter( format );
    return result;
}

std::string getEvaluationStatsLine( std::vector< evaluation_stats > stats, PiUtils::TEXT_FORMAT format, bool include_date_time, time_t eval_time )
{
    Logger::log( Logger::TRACE, "EvaluationStore::getEvaluationStatsLine" );
    std::string result = "";
    result += getTableLineStartDelimiter( format );
    for( std::vector< evaluation_stats >::const_iterator itr = stats.begin(); itr != stats.end(); ++itr )
    {
        if ( include_date_time && ( itr == stats.begin()) )
        {
            std::string time_str;
            PiUtils::getYYMMDD_HHMMSSDisplayTime( eval_time, time_str );
            result += getTableCellStartDelimiter( format );
            result += time_str;
            result += getTableCellEndDelimiter( format );
        }
        json json_result = (*itr).evaluation_stat_json;
        switch( DeviceUtils::getPropertyTypeFromString( json_result[J_PROPERTY_TYPE] ) )
        {
            case TYPE_FLOAT:
            {
                result += getTableCellStartDelimiter( format );
                PiUtils::roundFloat( 2, json_result[J_EVAL_MIN], result );
                result += getTableCellEndDelimiter( format );
                result += getTableCellStartDelimiter( format );
                PiUtils::roundFloat( 2, json_result[J_EVAL_MAX], result );
                result += getTableCellEndDelimiter( format );
                break;
            }
            case TYPE_INTEGER:
            case TYPE_BOOL:
            case TYPE_STATE:
            default:
                result += getTableCellStartDelimiter( format );
                result += " TODO";
                result += getTableCellEndDelimiter( format );
                break;
        }
    }
    result += getTableLineEndDelimiter( format );
    Logger::log( Logger::TRACE, "EvaluationStore::getEvaluationStatsLine adding in:" + result );
    Logger::log( Logger::TRACE, "EvaluationStore::getEvaluationStatsLine done line" );
    return result;
}

bool EvaluationStore::generateHistoryTable( time_t evaluation_time, json& results, const json& options, PiUtils::TEXT_FORMAT text_format )
{
    Logger::log( Logger::TRACE, "EvaluationStore::generateHistoryTable start ");
    std::string curr_table = PiUtils::getTableStartDelimiter( text_format );
    // Walk backwards
    time_t finish_time = PiUtils::getTimeInAdditionHourMinuteSeconds( 0, 0, 0, -1 *
                         JSONUtils::getJSONIntValue( options, J_S_TIME_EVALUATIONS_S, 5*60*60 )  );
    int num_evaluations = JSONUtils::getJSONIntValue( options, J_S_NUM_EVALUATIONS, 100 );
    std::map< time_t, std::vector< device_evaluation > >::reverse_iterator itr = _evaluations.rbegin();
    int i = 0;
    while ( itr != _evaluations.rend() )
    {
        if ( itr == _evaluations.rbegin() )
        {
            curr_table += getEvaluationsHeader( (*itr).second, text_format, true );
        }
        curr_table += getEvaluationsLine( (*itr).second, text_format, true, (*itr).first );
        itr = ++itr;
        ++i;
        if ( itr == _evaluations.rend() || ( (*itr).first < finish_time ) || ( i > num_evaluations ) )
            break;
    }
    curr_table += PiUtils::getTableEndDelimiter( text_format );
    results[J_EMAIL_CURR_TABLE] = curr_table;
    Logger::log( Logger::TRACE, "EvaluationStore::generateHistoryTable end");
    return true;
}

bool EvaluationStore::generateStatsTable( time_t evaluation_time, json& results, const json& options, PiUtils::TEXT_FORMAT text_format )
{
    Logger::log( Logger::TRACE, "EvaluationStore::generateStatsTable start ");
    std::string curr_table = PiUtils::getTableStartDelimiter( text_format );
    // Walk backwards
    time_t finish_time = PiUtils::getTimeInAdditionHourMinuteSeconds( 0, 0, 0, -1 *
                         JSONUtils::getJSONIntValue( options, J_S_TIME_EVALUATION_STATS_S, 24*60*60 )  );
    int num_evaluations = JSONUtils::getJSONIntValue( options, J_S_NUM_EVALUATIONS, 100 );
    std::string finish_time_str;
    PiUtils::getYYMMDD_HHMMSSDisplayTime( finish_time, finish_time_str );
    std::string evaluation_time_str;
    PiUtils::getYYMMDD_HHMMSSDisplayTime( evaluation_time, evaluation_time_str );

    Logger::log( Logger::TRACE, "EvaluationStore::generateStatsTable going to log for num: " + PiUtils::tostr( num_evaluations ) + " or back to " + finish_time_str + " from eval time: " + evaluation_time_str + " with _evaluation_stats.size() being " + PiUtils::tostr( _evaluation_stats.size() ) );
    std::map< time_t, std::vector< evaluation_stats > >::reverse_iterator itr = _evaluation_stats.rbegin();
    int i = 0;
    while ( itr != _evaluation_stats.rend() )
    {
        if ( itr == _evaluation_stats.rbegin() )
        {
            curr_table += getEvaluationStatsHeader( (*itr).second, text_format, true );
        }
        curr_table += getEvaluationStatsLine( (*itr).second, text_format, true, (*itr).first );
        ++itr;
        ++i;
        Logger::log( Logger::TRACE, "EvaluationStore::generateStatsTable looking like: " + curr_table );
        if ( itr == _evaluation_stats.rend() || ( (*itr).first < finish_time ) || ( i > num_evaluations ) )
        {
            std::string break_str;
            PiUtils::getYYMMDD_HHMMSSDisplayTime( (*itr).first, break_str );
            Logger::log( Logger::TRACE, "EvaluationStore::generateStatsTable breaking at num: " + PiUtils::tostr( i ) + " or at braek time " + break_str );
            if ( itr == _evaluation_stats.rend() )
                Logger::log( Logger::TRACE, "EvaluationStore::generateStatsTable breaking due to rend()" );
            else if ( (*itr).first < finish_time ) 
                Logger::log( Logger::TRACE, "EvaluationStore::generateStatsTable breaking due to (*itr).first < finish_time" );
            else if ( i > num_evaluations )
                Logger::log( Logger::TRACE, "EvaluationStore::generateStatsTable breaking due to i > num_evaluations" );
            break;
        }
    }
    curr_table += PiUtils::getTableEndDelimiter( text_format );

    results[J_EMAIL_STATS_TABLE] = curr_table;
    Logger::log( Logger::TRACE, "EvaluationStore::generateStatsTable end");
    return true;
}

bool EvaluationStore::generateStatus( time_t evaluation_time, json& results, const json& options, PiUtils::TEXT_FORMAT text_format, bool is_startup )
{
    Logger::log( Logger::TRACE, "EvaluationStore::generateStatus start");
    std::string header = getStartDelimiter( text_format );
    header += "Status at time ";
    std::string time_str;
    PiUtils::getYYMMDD_HHMMSSDisplayTime( evaluation_time, time_str );
    header += time_str;
    header += PiUtils::getLineEndDelimiter( text_format );

    // Get the history
    // Populates results[J_EMAIL_CURR_TABLE]
    generateHistoryTable( evaluation_time, results, options, text_format );

    // Get the history
    // Populates results[J_EMAIL_STATS_TABLE]
    generateStatsTable( evaluation_time, results, options, text_format );

    std::string footer = PiUtils::getEndDelimiter( text_format );
    results[J_EMAIL_HEADER] = header;
    results[J_EMAIL_FOOTER] = footer;

    if ( JSONUtils::getJSONBoolValue( options, J_S_GENERATE_SUBJECT, true ) )
    {
        if ( is_startup )
            results[J_EMAIL_SUBJECT] = "Startup email for " + time_str;
        else
            results[J_EMAIL_SUBJECT] = "Status email for " + time_str;
    }

    if ( JSONUtils::getJSONBoolValue( options, J_ADD_SYSTEM_INFO, true ) )
    {
        std::string sysinfo = _system->getDeviceStore()->getDevicesAsString( PiUtils::HTML );
        std::string internal_ip = PiUtils::getInternalIPAddress();
        if (!internal_ip.empty()) {
            sysinfo += "<br>Internal IP: " + internal_ip;
        }
        results[ J_SYSTEM_INFO ] = sysinfo;
    }
//    if ( JSONUtils::getJSONBoolValue( options, J_INCLUDE_EVENTS, true ) )
    {
        std::string events = "<br>Upcoming events:<br>";
        std::string events_table = getTableStartDelimiter( text_format );
        _system->getEventManager()->generateEventsTable( results, options, text_format );
    }
    if ( JSONUtils::getJSONBoolValue( options, J_S_CONSTRUCT_BODY, true ) )
    {
        std::string old_body = JSONUtils::getJSONStringValue( results, J_EMAIL_BODY, "" );
        results[ J_EMAIL_BODY ] = old_body +
                                  JSONUtils::getJSONStringValue( results, J_EMAIL_HEADER, "" ) +
                                  PiUtils::getLineEndDelimiter( text_format ) +
                                  JSONUtils::getJSONStringValue( results, J_SYSTEM_INFO, "" ) +
                                  PiUtils::getLineEndDelimiter( text_format ) +
                                  JSONUtils::getJSONStringValue( results, J_EMAIL_CURR_TABLE, "" ) +
                                  PiUtils::getLineEndDelimiter( text_format ) +
                                  JSONUtils::getJSONStringValue( results, J_EMAIL_STATS_TABLE, "" ) +
                                  PiUtils::getLineEndDelimiter( text_format ) +
                                  JSONUtils::getJSONStringValue( results, J_EVENTS_TABLE, "" ) +
                                  PiUtils::getLineEndDelimiter( text_format ) +
                                  JSONUtils::getJSONStringValue( results, J_EVENTS_ACK_TABLE, "" ) +
                                  JSONUtils::getJSONStringValue( results, J_EMAIL_FOOTER, "" );
    }
    Logger::log( Logger::TRACE, "EvaluationStore::generateStatus end");
    return true;
}

bool EvaluationStore::expireEvaluations( )
{
    // Any evaluations more than 3 days old can be removed (probably 2, but just go with 3).
    // Since _evaluations are in order, just start walking the map until we get to the right date.
    time_t date_to_remove_before = PiUtils::getTimeInAdditionHourMinuteSeconds( -3, 0, 0, 0 );
    std::string time_str;
    PiUtils::getYYMMDD_HHMMSSDisplayTime( date_to_remove_before, time_str );
    Logger::log( Logger::TRACE, "EvaluationStore::expireEvaluations going to remove evaluations before time: " + time_str );
    std::map< time_t, std::vector< device_evaluation > >::iterator itr = _evaluations.begin();
    if ( itr != _evaluations.end() )
    {
        while( (*itr).first < date_to_remove_before )
        {
            PiUtils::getYYMMDD_HHMMSSDisplayTime( (*itr).first, time_str );
            Logger::log( Logger::TRACE, "EvaluationStore::expireEvaluations going to remove evaluations at time: " + time_str );
            _evaluations.erase( itr );
            itr = _evaluations.begin();
            if ( itr == _evaluations.end() )
                break;
        }
    }
    std::map< time_t, std::vector< evaluation_stats > >::iterator stats_itr = _evaluation_stats.begin();
    if ( stats_itr != _evaluation_stats.end() )
    {
        while( (*stats_itr).first < date_to_remove_before )
        {
            PiUtils::getYYMMDD_HHMMSSDisplayTime( (*stats_itr).first, time_str );
            Logger::log( Logger::TRACE, "EvaluationStore::expireEvaluations going to remove evaluation_stats at time: " + time_str );
            _evaluation_stats.erase( stats_itr );
            stats_itr = _evaluation_stats.begin();
            if ( stats_itr == _evaluation_stats.end() )
                break;
        }
    }
    return true;
}

bool EvaluationStore::addEvaluations( time_t evaluation_time, std::vector< device_evaluation >& evaluations )
{
    Logger::log( Logger::TRACE, "EvaluationStore::addEvaluations." );
    // Expire evaluations
    expireEvaluations();
    _evaluations[ evaluation_time ] = evaluations;
    // Add do stats
    if ( _logging_enabled )
        logEvaluations( evaluation_time, evaluations );
    calculateStats();
    return true;
}

bool EvaluationStore::logEvaluations( time_t evaluation_time, std::vector< device_evaluation >& evaluations )
{
    Logger::log( Logger::TRACE, "EvaluationStore::logEvaluations." );
    if ( !_logging_enabled ) return false;
    if ( _logging_location.empty() ) return false;
    std::string time_str;
    PiUtils::getYYMMDDDisplayTime( evaluation_time, time_str );
    std::string all_filename = _logging_location + time_str + "_all.json";
    bool new_file = !PiUtils::fileExists( all_filename );
    // Append the evaluations to the file. It will be a file with json lines, but not a json file.

	std::fstream all_file;
    all_file.open(all_filename, std::ios_base::app | std::ios_base::in );
    if ( all_file.is_open() )
    {
        Logger::log( Logger::TRACE, "EvaluationStore::logEvaluations going to append to file: " + all_filename );
        for( std::vector< device_evaluation >::const_iterator itr = evaluations.begin(); itr != evaluations.end(); ++itr )
        {
            if ( itr != evaluations.begin() ) all_file << ",";
            json all = (*itr).evaluation_metadata;
            all[ J_RESULTS ] = (*itr).evaluation_results;
            all[ J_VALIDATION_RESULTS ] = (*itr).evaluation_validation;
            all_file << all.dump();
        }
        all_file << "\n";
        all_file.close();
    }
    else
    {
        Logger::log( Logger::ERROR, "EvaluationStore::logEvaluations failed to open file: " + all_filename );
        return false;
    }

    // Results. This is a CSV file.
    std::string results_filename = _logging_location + time_str + ".log";
	std::fstream results_file;
    results_file.open(results_filename, std::ios_base::app | std::ios_base::in );
    if ( results_file.is_open() )
    {
        if ( new_file )
        {
            Logger::log( Logger::TRACE, "EvaluationStore::logEvaluations going to create file: " + results_filename );
            // Update the symlink
            // Build the header
            results_file << "Date/Time";
            for( std::vector< device_evaluation >::const_iterator itr = evaluations.begin(); itr != evaluations.end(); ++itr )
            {
                for( json::const_iterator j_itr = (*itr).evaluation_results.begin(); j_itr != (*itr).evaluation_results.end(); ++j_itr )
                {
                    results_file << "," << j_itr.key();
                }
                // Iterate though and just get the keys
            }
            results_file << "\n";
        }
        Logger::log( Logger::TRACE, "EvaluationStore::logEvaluations going to append to file: " + results_filename );
        std::string time_str2;
        PiUtils::getYYMMDD_HHMMSSDisplayTime( evaluation_time, time_str2 );
        results_file << time_str2;
        for( std::vector< device_evaluation >::const_iterator itr = evaluations.begin(); itr != evaluations.end(); ++itr )
        {
            for( json::const_iterator j_itr = (*itr).evaluation_results.begin(); j_itr != (*itr).evaluation_results.end(); ++j_itr )
            {
                results_file << "," << j_itr.value();
            }
        }
        results_file << "\n";
        results_file.close();
    }
    else
    {
        Logger::log( Logger::ERROR, "EvaluationStore::logEvaluations failed to open file: " + results_filename );
        return false;
    }
    Logger::log( Logger::TRACE, "EvaluationStore::logEvaluations done." );
    return true;
}

bool EvaluationStore::logEvaluationStats( time_t evaluation_time, std::vector< evaluation_stats >& stats )
{
    Logger::log( Logger::TRACE, "EvaluationStore::logEvaluationStats." );
    if ( !_logging_enabled ) return false;
    if ( _logging_location.empty() ) return false;
    std::string time_str;
    PiUtils::getYYMMDDDisplayTime( evaluation_time, time_str );
    std::string all_filename = _logging_location + time_str + "_stats_all.json";
    bool new_file = !PiUtils::fileExists( all_filename );
    // Append the evaluations to the file. It will be a file with json lines, but not a json file.

	std::fstream all_file;
    all_file.open(all_filename, std::ios_base::app | std::ios_base::in );

    if ( all_file.is_open() )
    {
        Logger::log( Logger::TRACE, "EvaluationStore::logEvaluationStats going to append to file: " + all_filename );
        for( std::vector< evaluation_stats >::const_iterator itr = stats.begin(); itr != stats.end(); ++itr )
        {
            if ( itr != stats.begin() ) all_file << ",";
            all_file << (*itr).asJson().dump();
        }
        all_file << "\n";
        all_file.close();
    }
    else
    {
        Logger::log( Logger::ERROR, "EvaluationStore::logEvaluationStats failed to open file: " + all_filename );
        return false;
    }

    // Results. This is a CSV file.
    std::string results_filename = _logging_location + time_str + "_stats.log";
	std::fstream results_file;
    results_file.open(results_filename, std::ios_base::app | std::ios_base::in );
    if ( results_file.is_open() )
    {
        if ( new_file )
        {
            Logger::log( Logger::TRACE, "EvaluationStore::logEvaluationStats going to create file: " + results_filename );
            // Update the symlink
            // Build the header
            results_file << "Date/Time";
            for( std::vector< evaluation_stats >::const_iterator itr = stats.begin(); itr != stats.end(); ++itr )
            {
                for( json::const_iterator j_itr = (*itr).evaluation_stat_json.begin(); j_itr != (*itr).evaluation_stat_json.end(); ++j_itr )
                {
                    if ( j_itr.key() == J_PROPERTY_TYPE )
                        continue;
                    results_file << "," << j_itr.key();
                }
                // Iterate though and just get the keys
            }
            results_file << "\n";
            
        }
        Logger::log( Logger::TRACE, "EvaluationStore::logEvaluationStats going to append to file: " + results_filename );
        std::string time_str2;
        PiUtils::getYYMMDD_HHMMSSDisplayTime( evaluation_time, time_str2 );
        results_file << time_str2;
        for( std::vector< evaluation_stats >::const_iterator itr = stats.begin(); itr != stats.end(); ++itr )
        {
            for( json::const_iterator j_itr = (*itr).evaluation_stat_json.begin(); j_itr != (*itr).evaluation_stat_json.end(); ++j_itr )
            {
                if ( j_itr.key() == J_PROPERTY_TYPE )
                    continue;
                results_file << "," << j_itr.value();
            }
        }
        results_file << "\n";
        results_file.close();
    }
    else
    {
        Logger::log( Logger::ERROR, "EvaluationStore::logEvaluationStats failed to open file: " + results_filename );
        return false;
    }
    Logger::log( Logger::TRACE, "EvaluationStore::logEvaluationStats done." );
    return true;
}
void EvaluationStore::determineNextStatsCycleEnd( bool reset )
{
    Logger::log( Logger::TRACE, "EvaluationStore::determineNextStatsCycleEnd" );
    if ( ( _cur_stats_cycle_end == 0 ) || reset )
    {
        // For now, we want to calculate stats on the hour for the previous hour.
        struct tm* cmd_time = PiUtils::getCurrentTime( );
        if ( NULL == cmd_time )
        {
            Logger::log( Logger::ERROR, "EvaluationStore::determineNextStatsCycleEnd could not get current time." );
            return;
        }

        cmd_time->tm_min = 59;
        cmd_time->tm_sec = 59;
        _cur_stats_cycle_end = mktime( cmd_time );
        std::string str = "Setting _cur_stats_cycle_end for current time: ";
        std::string tmp = "";
        PiUtils::getCurrentPrettyDisplayTime( tmp );
        str.append( tmp );
        str.append( " to: " );
        PiUtils::getPrettyDisplayTime( _cur_stats_cycle_end, tmp );
        str.append( tmp );
        Logger::log( Logger::TRACE, "EvaluationStore::determineNextStatsCycleEnd " + str );
    }
}

bool EvaluationStore::calculateStats()
{
    Logger::log( Logger::TRACE, "EvaluationStore::calculateStats." );
    determineNextStatsCycleEnd( false );
    // Get the last time for an evaluation
    time_t last_evaluation_time = 0;
    if ( !_evaluations.empty() ) // This should never happen because we are only called after evaluations have been
                                 // added, but check just in case.
    {
        std::map< time_t, std::vector< device_evaluation > >::const_reverse_iterator c_itr = _evaluations.rbegin();
        last_evaluation_time = (*c_itr).first;
    }
    if ( _cur_stats_cycle_end > last_evaluation_time )
    {
        Logger::log( Logger::TRACE, "EvaluationStore::calculateStats Skipping because _cur_stats_cycle_end > last_evaluation_time" );
        return true;
    }
    // At this point, we have passed our _cur_stats_cycle_end, so we need to add an entry
    {
        std::string str = "Updating the stats for time: ";
        std::string tmp;
        PiUtils::getPrettyDisplayTime( last_evaluation_time, tmp );
        str.append( tmp );
        str.append( " with " );
        str.append( PiUtils::tostr( _evaluations.size() ) );
        str.append( " evaluations" );
        Logger::log( Logger::TRACE, "EvaluationStore::calculateStats " + str );
    }
    if ( _evaluations.size() == 0 )
    {
        Logger::log( Logger::TRACE, "EvaluationStore::calculateStats returning because we have no evaluations" );
        determineNextStatsCycleEnd( true );
        return false;
    }

    bool success = true;
    // There is 1 entry for each of the different device_evaluations.
    // It is assumed there are the same number for each timestep
    std::vector< evaluation_stats > all_stats;
    time_t new_last_stats_time = 0;
    for( std::map< time_t, std::vector< device_evaluation > >::const_reverse_iterator cr_itr = _evaluations.rbegin(); cr_itr != _evaluations.rend(); ++cr_itr )
    {
        // Check and see if this is after the _cur_stats_cycle_end.
        std::string stats_time;
        PiUtils::getPrettyDisplayTime( (*cr_itr).first, stats_time );
        if ( (*cr_itr).first > _cur_stats_cycle_end )
        { 
            std::string str = "Eval for time: ";
            str.append( stats_time );
            str.append( "occurs after _cur_stats_cycle_end time of: " );
            std::string tmp;
            PiUtils::getPrettyDisplayTime( _cur_stats_cycle_end, tmp );
            str.append( tmp );
            str.append( ". Skipping." );
            Logger::log( Logger::TRACE, "EvaluationStore::calculateStats " + str );
            continue;
        }
        std::pair< time_t, std::vector< device_evaluation > > evals = *cr_itr;
        if ( all_stats.empty() )
        {
            if ( evals.second.size() == 0 )
            {
                Logger::log( Logger::TRACE, "EvaluationStore::calculateStats stats at time " + stats_time + " are empty" );
                return false;
            }
            Logger::log( Logger::TRACE, "EvaluationStore::calculateStats " + PiUtils::tostr( evals.second.size() ) + " all_stats" );
            for( std::vector< device_evaluation >::const_iterator itr = evals.second.begin(); itr != evals.second.end(); ++itr )
            {
                for( json::const_iterator j_itr = (*itr).results.begin(); j_itr != (*itr).results.end(); ++j_itr )
                {
                    evaluation_stats stats;
                    stats.device = (*itr).device;
                    stats.evaluation_stat_json[ J_PROPERTY_TYPE ] = (*j_itr)[J_PROPERTY_TYPE];
                    stats.evaluation_stat_json[ J_PROPERTY_LONG_NAME ] = (*j_itr)[J_PROPERTY_LONG_NAME];
                    all_stats.push_back( stats );
                    Logger::log( Logger::TRACE, "EvaluationStore::calculateStats going to add " + stats.device->getName() + " all_stats" );
                }
            }
        }
        time_t eval_time = evals.first;
        // Check and see if we are at the end.
        if ( eval_time <= _last_stats_time )
        {
            std::string str = "Eval for time: ";
            str.append( stats_time );
            str.append( "occurs before or equal to _last_stats_time time of: " );
            std::string tmp;
            PiUtils::getPrettyDisplayTime( _last_stats_time, tmp );
            str.append( tmp );
            str.append( ". Done with the stats." );
            Logger::log( Logger::TRACE, "EvaluationStore::calculateStats " + str );
            break;
        }
        // Since we are going in reverse order, the "first" one that gets here is the latest one
        if ( new_last_stats_time == 0 )
        {
            new_last_stats_time = eval_time;
        }

        // At this point, we want to add it to our stats.
        unsigned int i = 0;
        for( std::vector< device_evaluation >::const_iterator itr = evals.second.begin(); itr != evals.second.end(); ++itr, ++i )
        {
            // TODO Need to iterate through eval.results
            for( json::const_iterator j_itr = (*itr).results.begin(); j_itr != (*itr).results.end(); ++j_itr )
            {
                json result = (*j_itr);
                all_stats[i].start_time = eval_time; // This is updated down to the earliest one.
                if ( all_stats[i].end_time == 0 )
                {
                    all_stats[i].end_time = eval_time;
                }
                all_stats[i].evaluation_metadata[ J_NUM_SAMPLES ] = 1 + JSONUtils::getJSONIntValue( all_stats[i].evaluation_metadata, J_NUM_SAMPLES, 0 );
                // Do something different based on type.
                switch( DeviceUtils::getPropertyTypeFromString( result[J_PROPERTY_TYPE] ) )
                {
                    case TYPE_FLOAT:
                    {
                        float val = result[ J_VAL ];
                        // Check and see if the json entries exist.
                        if ( !all_stats[i].evaluation_stat_json.contains( J_EVAL_MIN ) )
                        {
                            all_stats[i].evaluation_stat_json[ J_EVAL_MIN ] = val;
                        }
                        if ( !all_stats[i].evaluation_stat_json.contains( J_EVAL_MAX ) )
                        {
                            all_stats[i].evaluation_stat_json[ J_EVAL_MAX ] = val;
                        }
                        if ( !all_stats[i].evaluation_stat_json.contains( J_EVAL_TOTAL ) )
                        {
                            all_stats[i].evaluation_stat_json[ J_EVAL_TOTAL ] = 0;
                        }
                        if ( val < all_stats[i].evaluation_stat_json[ J_EVAL_MIN ] )
                        {
                            all_stats[i].evaluation_stat_json[ J_EVAL_MIN ] = val;
                        }
                        if ( val > all_stats[i].evaluation_stat_json[ J_EVAL_MAX ] )
                        {
                            all_stats[i].evaluation_stat_json[ J_EVAL_MAX ] = val;
                        }
                        float curr_total = all_stats[i].evaluation_stat_json[ J_EVAL_TOTAL ];
                        all_stats[i].evaluation_stat_json[ J_EVAL_TOTAL ] = val + curr_total;
                    }
                    case TYPE_INTEGER:
                    case TYPE_BOOL:
                    case TYPE_STATE:
                    default:
                        break;
                }
            }
        }
    }
    // TODO Iterate through again to finalize (for example, make an average)
    _evaluation_stats[ last_evaluation_time ] = all_stats;
    Logger::log( Logger::TRACE, "calculateStats added in " + PiUtils::tostr( all_stats.size() ) + " stats" );
    // TODO save them out
    logEvaluationStats( new_last_stats_time, all_stats );
    _last_stats_time = new_last_stats_time;
    determineNextStatsCycleEnd( true );
    return success;
}

EvaluationAction::EvaluationAction( json email_info, const std::string& device_name )
    : _device( 0 )
    , _evaluation_state( UNKNOWN_EVALUATION_STATE )
    , _email_info( email_info )
    , _device_name( device_name )
    , _watch_time_s( 0 )
    , _warn_time_s( 300 )
    , _error_time_s( 600 )
    , _error_retry_time_s( 300 )
    , _curr_time_triggered( 0 )
    , _error_counter( 0 )
    , _email_watch( false )
    , _email_warn( true )
    , _email_error( true )
    , _mute_until_time( 0 )
{
}

EvaluationAction* EvaluationAction::getCustomAction( HomeSystem* system, json ea )
{
    Logger::log( Logger::TRACE, "EvaluationAction::getCustomAction" );
    if ( ea.contains( J_EA_TYPE ) )
    {
        Logger::log( Logger::TRACE, "EvaluationActionManager::addEvaluationAction has " + J_EA_TYPE + ":" + ea.dump() + " with J_EA_TYPE: " + ea.at(J_EA_TYPE ).dump() );
        if( CUSTOM_EVAL == getEvalActionTypeFromString( ea.at( J_EA_TYPE ) ) )
        {
            std::string custom_type = JSONUtils::getJSONStringValue( ea, J_CUSTOM_TYPE, "" );
            if ( custom_type == "DOOR_FAILED_TO_CLOSE")
            {
                return EA_DoorClosingFailure::getActionFromJSON( system, ea );
            }
            if ( custom_type == "CLOSED_TOO_LONG")
            {
                return EA_DoorClosingFailure::getActionFromJSON( system, ea );
            }
        }
    }
    return 0;
}

bool EvaluationAction::setNotificationConfig( json config )
{
    _email_watch = JSONUtils::getJSONBoolValue( config, J_NOTIFICATION_WATCH, _email_watch ); // TODO Set to false
    _email_warn = JSONUtils::getJSONBoolValue( config, J_NOTIFICATION_WARN, _email_warn );
    _email_error = JSONUtils::getJSONBoolValue( config, J_NOTIFICATION_ERROR, _email_error );
    setWatchWarnErrorTimes(
        JSONUtils::getJSONIntValue( config, J_EA_WATCH_TIME, _watch_time_s ),
        JSONUtils::getJSONIntValue( config, J_EA_WARN_TIME, _warn_time_s ),
        JSONUtils::getJSONIntValue( config, J_EA_ERROR_TIME, _error_time_s ),
        JSONUtils::getJSONIntValue( config, J_EA_ERROR_RETRY_TIME, _error_retry_time_s ) );
    return true;
}

json EvaluationAction::evaluate( const std::vector< device_evaluation >& evaluations )
{
    Logger::log( Logger::TRACE, getEvaluationActionAsString() + "::evaluate" );
    // Go through and find out device.
    json results;
    results[ J_EVALUATION_SUCCESS ] = false;
    results[ J_EVALUATION_STRING ] = std::string( "Device not found");
    time_t eval_time = 0;
    bool triggered = evaluateInternal(evaluations, results, eval_time);

    Logger::log( Logger::TRACE, getEvaluationActionAsString() + "::evaluate check triggered:" + results.dump() );
    if ( triggered )
    {
        if ( _curr_time_triggered == 0 )
            _curr_time_triggered = eval_time;
        results[ J_ADD_STATUS_HTML ] = true;
        std::string eval_time_str;
        PiUtils::getYYMMDD_HHMMSSDisplayTime( eval_time, eval_time_str );
        std::string curr_eval_time_str;
        PiUtils::getYYMMDD_HHMMSSDisplayTime( _curr_time_triggered, curr_eval_time_str );
        time_t time_since_triggered_s = eval_time - _curr_time_triggered;
        Logger::log( Logger::TRACE, getEvaluationActionAsString() + "::evaluate triggered:" + results.dump() + " at eval time: " + eval_time_str + " with curr_time: " + curr_eval_time_str + " and state: " + getEvaluationStateAsString( _evaluation_state ) + " and time_since_triggered:" + std::to_string( time_since_triggered_s ) );

        // _watch_time_s - Amount of time we stay in the watch state
        // _warn_time_s - Amount of time we stay in the warn state
        // _error_time_s - Amount of time we stay in the error state
        // _error_retry_time_s - Amount of time we stay in the error retry state
        if ( time_since_triggered_s >= _error_time_s )
        {
            // We are in the error state. Could be retrying...
            if ( time_since_triggered_s >= ( _error_time_s + ( _error_counter * _error_retry_time_s ) ) )
            {
                setNewEvaluationState( ERROR, results );
            }
        }
        else if ( time_since_triggered_s >= _warn_time_s )
        {
            if ( _evaluation_state != WARN )
                setNewEvaluationState( WARN, results );
        }
        else if ( time_since_triggered_s >= _watch_time_s )
        {
            if ( _evaluation_state != WATCH )
                setNewEvaluationState( WATCH, results );
        }
        else
        {
            // We stay in WAIT
            if ( _evaluation_state != WAIT )
                setNewEvaluationState( WAIT, results );
        }
    }
    else if ( _curr_time_triggered > 0 )
    {
        Logger::log( Logger::TRACE, getEvaluationActionAsString() + "::evaluate not triggered:" + results.dump() );
        setNewEvaluationState( WAIT, results );
        _error_counter = 0;
        _evaluation_state = WAIT;
        _curr_time_triggered = 0;
    }
    return results;
}

bool EvaluationAction::defaultSetNewEvaluationState( EVALUATION_STATE new_state, json& results )
{
    Logger::log( Logger::TRACE, "EvaluationAction::defaultSetNewEvaluationState" );

    // Do we need to do anything
    bool send_email = false;
    bool returning_to_wait = false;
    json email_addresses;
    switch( new_state )
    {
        case WAIT:
            if ( _evaluation_state == WAIT )
                return true; // Nothing to do
            // We only need to send an e-mail if we are coming from a state that sent an e-mail.
            if ( _evaluation_state == WATCH )
            {
                email_addresses = JSONUtils::getJSONArrayValue( _email_info, J_WATCH_ADDRESSES );
                send_email = _email_watch;
            }
            else if ( _evaluation_state == WARN )
            {
                email_addresses = JSONUtils::getJSONArrayValue( _email_info, J_WARN_ADDRESSES );
                send_email = _email_warn;
            }
            else
            {
                email_addresses = JSONUtils::getJSONArrayValue( _email_info, J_ERROR_ADDRESSES );
                send_email = _email_error;
            }
            _error_counter = 0; // Make sure to reset it
            returning_to_wait = true;
            break;
        case WATCH:
            if ( _evaluation_state == WATCH )
                return true; // Nothing to do
            email_addresses = JSONUtils::getJSONArrayValue( _email_info, J_WATCH_ADDRESSES );
            send_email = _email_watch;
            break;
        case WARN:
            if ( _evaluation_state == WARN )
                return true; // Nothing to do
            email_addresses = JSONUtils::getJSONArrayValue( _email_info, J_WARN_ADDRESSES );
            send_email = _email_warn;
            break;
        case ERROR: // This needs to be handled no matter what
        case ERROR_RETRY:
            ++_error_counter;
            email_addresses = JSONUtils::getJSONArrayValue( _email_info, J_ERROR_ADDRESSES );
            send_email = _email_error;
            break;
        case TEMPORARILY_DISABLED:
        case UNKNOWN_EVALUATION_STATE:
        default:
            email_addresses = _email_info.at( J_ERROR_ADDRESSES );
            send_email = _email_error;
            break;
    }
    
    _evaluation_state = new_state;
    // If we don't have to send an email, stop here
    if ( !send_email )
    {
        return true;
    }
    if ( ( _mute_until_time != 0 ) && !returning_to_wait )
    {
        // We might be muted. If so, check and don't email watch/warnings/errors until we are past _mute_until_time.
        time_t current_time = PiUtils::getCurrentTime_t();
        if ( _mute_until_time > current_time )
        {
            Logger::log( Logger::INFO, "EvaluationAction::defaultSetNewEvaluationState expected to send email, but we are muted." );
            return true;
        }
        Logger::log( Logger::INFO, "EvaluationAction::defaultSetNewEvaluationState mute expired." );
        _mute_until_time = 0;
        // TODO Send an e-mail to let them know that muting is done?
    }
    // Build the subject line. NOTE: If this changes, make sure to change getDeviceFromEASubject
    // subject: SYSTEM_NAME DEVICE_NAME: other text.
    std::string email_subject = _device->getSystem()->getHomeConfig()->getSystemName() + " " + _device->getName() + ":";
    if ( _evaluation_state == new_state )
    {
        email_subject += std::string( " repeating state " ) +
                         getEvaluationStateAsString( _evaluation_state );
        if (_error_counter > 0)
            email_subject += " for the " + std::to_string( _error_counter ) + " time";
    }
    else
    {
        email_subject += std::string( " changing from state " ) +
                         getEvaluationStateAsString( _evaluation_state ) +
                         " to state " +
                         getEvaluationStateAsString( new_state );
    }
    

    if ( email_addresses.empty() )
    {
        Logger::log( Logger::ERROR, "EvaluationAction::defaultSetNewEvaluationState expected to send email, but addresses is empty." );
        return false;
    }
    // At this point, we need to send an email. Determine to who and what to say.

    json email_event;
    email_event[ J_VERSION ] = 2;
    email_event[ J_TIME_IMMEDIATE ] = true; // Make sure it happens this cycle
    email_event[ J_EVENT_ORIGINATOR_TYPE ] = HomeEvent::getEventOriginatorTypeAsString( HomeEvent::ANALYZED );
    email_event[ J_EVENT_PROCESSOR_TYPE ] = HomeEventProcessor::getEventProcessorTypeAsString( HomeEventProcessor::EMAIL );

    json email_info;
    email_info[ J_EMAIL_TO ] = email_addresses;
    email_info[ J_EMAIL_FROM ] = JSONUtils::getJSONStringValue( _email_info,  J_EMAIL_FROM, "" );
    email_info[ J_EMAIL_SUBJECT ] = email_subject;
    email_info[ J_EMAIL_BODY ] = results[ J_EVALUATION_STRING ];
    email_info[ J_ADD_STATUS_HTML ] = true;

    email_event[ J_EMAIL_INFO ] = email_info;

    // Add out event to the results
    results[ J_NEW_EVENT ] = email_event;
    Logger::log( Logger::TRACE, "EvaluationAction::defaultSetNewEvaluationState added an email event." );
    return true;
}

#if 0
// The subject line is in the following format:
// subject: SYSTEM_NAME DEVICE_NAME: other text.
// Take the string, strip off the system name and get the device name up until the colon.
bool EvaluationAction::getDeviceFromEASubject( const std::string& subject, const std::string& curr_system_name, const std::string& device_name )
{
    // First, make sure we start with the system name.
    // Next, strip it off.
    // Next, strip off everything after the :
    // Find the device from that
    //size_t loc = str.find_last_of("/");
    //std::string directory = str.substr(0, loc );
    std::string system_name;
    std::string system_device;

    // Create a string stream for parsing
    std::istringstream iss(subject);

    // Parse system_name
    iss >> system_name;

    // Parse system_device (until the colon)
    std::getline(iss, system_device, ':');
    device_name = system_device.substr(1); // Remove leading space
    return system_name == curr_system_name;
}
#endif

EvaluationActionManager::EvaluationActionManager(HomeSystem* system)
    : _system( system )
{
}

EvaluationActionManager::~EvaluationActionManager()
{
}

bool EvaluationActionManager::initializeActions( HomeSystem* system )
{
    Logger::log( Logger::TRACE, "EvaluationActionManager::initializeActions" );
    bool success = true;
    for ( std::vector< EvaluationAction* >::iterator itr =  _actions.begin(); itr != _actions.end(); ++itr )
    {
        success = (*itr)->initialize( system ) && success;
    }
    return success;
}

json EvaluationActionManager::runEvaluationActions( const std::vector< device_evaluation >& evaluations )
{
    Logger::log( Logger::TRACE, "EvaluationActionManager::runEvaluationActions" );
    json results;
    for ( std::vector< EvaluationAction* >::iterator itr =  _actions.begin(); itr != _actions.end(); ++itr )
    {
        json result = (*itr)->evaluate( evaluations );
        if (!result.is_null() )
        {
            results.push_back( result );
        }
    }
    return results;
}

bool EvaluationActionManager::addEvaluationAction( json ea )
{
    Logger::log( Logger::TRACE, "EvaluationActionManager::addEvaluationAction:" + ea.dump() );

    EvaluationAction* action = 0;
    if ( ea.contains( J_EA_TYPE ) )
    {
        Logger::log( Logger::TRACE, "EvaluationActionManager::addEvaluationAction has " + J_EA_TYPE + ":" + ea.dump() + " with J_EA_TYPE: " + ea.at(J_EA_TYPE ).dump() );
        switch( getEvalActionTypeFromString( ea.at( J_EA_TYPE ) ) )
        {
            case BOUNDS_THRESHOLD:
                action = EA_BoundsThreshold::getActionFromJSON( _system, ea );
                break;
            case PROCESS_CHECK_EMAIL:
                action = EA_CheckEmail::getActionFromJSON( _system, ea );
                break;
            case STATE_THRESHOLD:
                action = EA_StateThreshold::getActionFromJSON( _system, ea );
                break;
            case CUSTOM_EVAL:
                action = EvaluationAction::getCustomAction( _system, ea );
                break;
            default:
                Logger::log( Logger::TRACE, "EvaluationActionManager::addEvaluationAction not supported." );
        }
    }
    if ( action )
    {
        _actions.push_back( action );
        return true;
    }
    Logger::log( Logger::TRACE, "EvaluationActionManager::addEvaluationAction no action created." );

    return true;
}

void EvaluationActionManager::addEvaluationActionsForDevice( const Device*& device, json& info ) const
{
    std::vector< std::string > actions;
    // Find the actions for the device
    for( std::vector< EvaluationAction* >::const_iterator itr = _actions.begin(); itr != _actions.end(); ++itr )
    {
        if ( (*itr)->getDeviceName() == device->getName() )
        {
            actions.push_back( (*itr)->getEvaluationActionAsString() );
        }
    }
    info[ J_EVALUATION_ACTIONS ] = actions;
}

const float BOUNDS_EPSILON = 0.0001;
EA_BoundsThreshold::EA_BoundsThreshold(
    json email_info,
    const std::string& device_name,
    const std::string& property,
    BOUNDS_OPERATION op,
    float val,
    float secondary_val)
    : EvaluationAction( email_info, device_name )
    , _property( property )
    , _val( val )
    , _secondary_val( secondary_val )
    , _op( op )
{
}

EA_BoundsThreshold* EA_BoundsThreshold::getActionFromJSON( HomeSystem* system, json ea )
{
    Logger::log( Logger::TRACE, "EA_BoundsThreshold::getActionFromJSON from: " + ea.dump() );
    if ( getEvalActionTypeFromString( JSONUtils::getJSONStringValue( ea, J_EA_TYPE, "" ) ) != BOUNDS_THRESHOLD )
        return 0;

    json email;
    if ( ea.contains( J_EMAIL_INFO ) )
        email = ea.at( J_EMAIL_INFO );
    else
        email = system->getHomeConfig()->getEmailInfo();

    EA_BoundsThreshold* bounds = new EA_BoundsThreshold(
        email,
        JSONUtils::getJSONStringValue( ea, J_DEVICE_NAME, "" ),
        JSONUtils::getJSONStringValue( ea, J_EA_PROPERTY_NAME, "" ),
        getBoundsOperationFromString( JSONUtils::getJSONStringValue( ea, J_EA_OPERATION, "" ) ),
        JSONUtils::getJSONFloatValue( ea, J_EA_DATUM, INVALID_FLOAT_VAL ),
        JSONUtils::getJSONFloatValue( ea, J_EA_DATUM_2, INVALID_FLOAT_VAL ) );
    if ( ea.contains( J_NOTIFICATION_CONFIG) )
    {
        bounds->setNotificationConfig( ea.at(J_NOTIFICATION_CONFIG));
    }

    Logger::log( Logger::TRACE, "EA_BoundsThreshold::getActionFromJSON bounds created" );
    return bounds;
}

bool EA_BoundsThreshold::initialize( HomeSystem* system )
{
    Logger::log( Logger::TRACE, "EA_BoundsThreshold::initialize" );
    // Find the device
    DeviceStore* store = system->getDeviceStore();
    _device = store->getDevice( _device_name );
    if ( _device )
        _evaluation_state = WAIT;
    else
        Logger::log( Logger::ERROR, "EA_BoundsThreshold::initialize could not find device with name: " + _device_name );
    return _device;
}

bool EA_BoundsThreshold::setNewEvaluationState( EVALUATION_STATE new_state, json& results )
{
    Logger::log( Logger::TRACE, "EA_BoundsThreshold::setNewEvaluationState" );
    return defaultSetNewEvaluationState( new_state, results );
}

bool EA_BoundsThreshold::evaluateInternal(const std::vector< device_evaluation >& evaluations, json& results, time_t& eval_time)
{
    bool triggered = false;
    for( std::vector< device_evaluation >::const_iterator itr = evaluations.begin(); itr != evaluations.end(); ++itr)
    {
        if ( _device != (*itr).device )
            continue;
        // We have the device. Get the property
        if ( !(*itr).evaluation_validation.contains( _property ) )
        {
            // Doesn't have out property
            Logger::log( Logger::ERROR, "EA_BoundsThreshold::evaluate missing our property. " + (*itr).evaluation_validation.dump() );
            continue;
        }
        float val = (*itr).evaluation_validation.at( _property );
        // Check for invalid value
        results[ J_EVALUATION_STRING ] = std::string( "Not triggered");
        if ( DeviceUtils::isValValid( val ) )
        {
            switch( _op )
            {
                case LESS_THAN:
                    if ( val < _val )
                    {
                        results[ J_EVALUATION_SUCCESS ] = false;
                        results[ J_EVALUATION_STRING ] = std::string( "Triggered for LESS_THAN: " + std::to_string( val ) + " < " + std::to_string( _val ) );
                        triggered = true;
                    }
                    else
                    {
                        results[ J_EVALUATION_SUCCESS ] = true;
                    }
                    break;
                case LESS_THAN_EQUAL_TO:
                    if ( val <= _val )
                    {
                        results[ J_EVALUATION_SUCCESS ] = false;
                        results[ J_EVALUATION_STRING ] = std::string( "Triggered for LESS_THAN_EQUAL_TO: " + std::to_string( val )+ " <= " + std::to_string( _val ) );
                        triggered = true;
                    }
                    else
                    {
                        results[ J_EVALUATION_SUCCESS ] = true;
                    }
                    break;
                case EQUAL_TO:
                    if ( ( val >= ( _val - BOUNDS_EPSILON ) ) && ( val <= ( _val + BOUNDS_EPSILON ) ) )
                    {
                        results[ J_EVALUATION_SUCCESS ] = false;
                        results[ J_EVALUATION_STRING ] = std::string( "Triggered for LESS_THAN_EQUAL_TO: " + std::to_string( val ) + " == " + std::to_string( _val ) );
                        triggered = true;
                    }
                    else
                    {
                        results[ J_EVALUATION_SUCCESS ] = true;
                    }
                    break;
                case GREATER_THAN_EQUAL_TO:
                    if ( val >= _val )
                    {
                        results[ J_EVALUATION_SUCCESS ] = false;
                        results[ J_EVALUATION_STRING ] = std::string( "Triggered for GREATER_THAN_EQUAL_TO: " + std::to_string( val ) + " >= " + std::to_string( _val ) );
                        triggered = true;
                    }
                    else
                    {
                        results[ J_EVALUATION_SUCCESS ] = true;
                    }
                    break;
                case GREATER_THAN:
                    if ( val >= _val )
                    {
                        results[ J_EVALUATION_SUCCESS ] = false;
                        results[ J_EVALUATION_STRING ] = std::string( "Triggered for GREATER_THAN: " + std::to_string( val ) + " > " + std::to_string( _val ) );
                        triggered = true;
                    }
                    else
                    {
                        results[ J_EVALUATION_SUCCESS ] = true;
                    }
                    break;
                case NOT_EQUAL:
                    if ( !( ( val >= ( _val - BOUNDS_EPSILON ) ) && ( val <= ( _val + BOUNDS_EPSILON ) ) ) )
                    {
                        results[ J_EVALUATION_SUCCESS ] = false;
                        results[ J_EVALUATION_STRING ] = std::string( "Triggered for NOT_EQUAL: " + std::to_string( val ) + " == " + std::to_string( _val ) );
                        triggered = true;
                    }
                    else
                    {
                        results[ J_EVALUATION_SUCCESS ] = true;
                    }
                    break;
                case BETWEEN:
                    if ( !( ( val > _val ) && ( val < _secondary_val ) ) )
                    {
                        results[ J_EVALUATION_SUCCESS ] = false;
                        results[ J_EVALUATION_STRING ] = std::string( "Triggered for BETWEEN: " + std::to_string( _val ) + " < " + std::to_string( val ) + " < " + std::to_string( _secondary_val ) );
                        triggered = true;
                    }
                    else
                    {
                        results[ J_EVALUATION_SUCCESS ] = true;
                    }
                    break;
            }
        }
        else
        {
            results[ J_EVALUATION_SUCCESS ] = false;
            results[ J_EVALUATION_STRING ] = std::string( "Invalid value." );
            triggered = true;
        }
        eval_time = (*itr).evaluation_time;
        break; // Only have 1
    }
    return triggered;
}

std::string EA_BoundsThreshold::getEvaluationActionAsString() const
{
//Max: -10 Temperature (C) Times watched (40) Warn time: 300s (38) Error time: 600s (38).
    std::string str = getBoundsOperationAsString( _op );
    str += ": ";
    PiUtils::roundFloat( 2, _val, str );
    str += " ";
    str += _property;
    str += " Watch time ";
    str += std::to_string( _watch_time_s );
    str += "s, Warn time ";
    str += std::to_string( _warn_time_s );
    str += "s, Error time ";
    str += std::to_string( _error_time_s );
    str += "s, Error retry time ";
    str += std::to_string( _error_retry_time_s );
    str += "s";
    return str;
}

EA_StateThreshold::EA_StateThreshold(
    json email_info,
    const std::string& device_name,
    DeviceUtils::DEVICE_STATE state
    )
    : EvaluationAction( email_info, device_name )
    , _state( state )
{
}

EA_StateThreshold* EA_StateThreshold::getActionFromJSON( HomeSystem* system, json ea )
{
    Logger::log( Logger::TRACE, "EA_StateThreshold::getActionFromJSON from: " + ea.dump() );
    if ( getEvalActionTypeFromString( JSONUtils::getJSONStringValue( ea, J_EA_TYPE, "" ) ) != STATE_THRESHOLD )
        return 0;

    json email;
    if ( ea.contains( J_EMAIL_INFO ) )
        email = ea.at( J_EMAIL_INFO );
    else
        email = system->getHomeConfig()->getEmailInfo();

    EA_StateThreshold* bounds = new EA_StateThreshold(
        email,
        JSONUtils::getJSONStringValue( ea, J_DEVICE_NAME, "" ),
        DeviceUtils::getStateFromString( JSONUtils::getJSONStringValue( ea, J_EA_DATUM, "" ) )
    );
    if ( ea.contains( J_NOTIFICATION_CONFIG) )
    {
        bounds->setNotificationConfig( ea.at(J_NOTIFICATION_CONFIG));
    }
    Logger::log( Logger::TRACE, "EA_StateThreshold::getActionFromJSON bounds created" );
    return bounds;
}

// TODO This seems to be common
bool EA_StateThreshold::initialize( HomeSystem* system )
{
    Logger::log( Logger::TRACE, "EA_StateThreshold::initialize" );
    // Find the device
    DeviceStore* store = system->getDeviceStore();
    _device = store->getDevice( _device_name );
    if ( _device )
        _evaluation_state = WAIT;
    else
        Logger::log( Logger::ERROR, "EA_StateThreshold::initialize could not find device with name: " + _device_name );
    return _device;
}

bool EA_StateThreshold::setNewEvaluationState( EVALUATION_STATE new_state, json& results )
{
    Logger::log( Logger::TRACE, "EA_StateThreshold::setNewEvaluationState" );
    return defaultSetNewEvaluationState( new_state, results );
}

bool EA_StateThreshold::evaluateInternal(const std::vector< device_evaluation >& evaluations, json& results, time_t& eval_time)
{
    bool triggered = false;
    for( std::vector< device_evaluation >::const_iterator itr = evaluations.begin(); itr != evaluations.end(); ++itr)
    {
        Logger::log( Logger::TRACE, "EA_StateThreshold::evaluateInternal Checking if " + (*itr).device->getName() + " is the same as " + _device->getName() );
        if ( _device != (*itr).device )
            continue;
        std::string state_str = (*itr).evaluation_validation.at( DeviceUtils::getShortNameForProperty( DeviceUtils::STATE ) );
        DeviceUtils::DEVICE_STATE state = DeviceUtils::getStateFromString(state_str);
        // Check for state
        std::string str = "EA_StateThreshold::evaluateInternal it is. The state is: ";
        str += state_str;
        str += " and we are state ";
        str += DeviceUtils::getStateAsString( _state );
        Logger::log( Logger::TRACE, str );
        results[ J_EVALUATION_STRING ] = std::string( "Not triggered");
        if ( _state == state )
        {
            Logger::log( Logger::TRACE, "EA_StateThreshold::evaluateInternal it is. The state different. Set triggered to true." );
            results[ J_EVALUATION_SUCCESS ] = false;
            std::string eval_str = "Triggered for state being : ";
            eval_str += DeviceUtils::getStateAsString(_state);
            eval_str += ".";
            results[ J_EVALUATION_STRING ] = eval_str;
            triggered = true;
        } else {
            Logger::log( Logger::TRACE, "EA_StateThreshold::evaluateInternal it is. The state is different." );
        }
        eval_time = (*itr).evaluation_time;
        break; // Only have 1
    }
    Logger::log( Logger::TRACE, "EA_StateThreshold::evaluateInternal done");
    return triggered;
}

std::string EA_StateThreshold::getEvaluationActionAsString() const
{
//Max: -10 Temperature (C) Times watched (40) Warn time: 300s (38) Error time: 600s (38).
    std::string str = "State is ";
    str += ": ";
    str += DeviceUtils::getStateAsString(_state);
    str += " ";
    // TODO This seems to be common
    str += " Watch time ";
    str += std::to_string( _watch_time_s );
    str += "s, Warn time ";
    str += std::to_string( _warn_time_s );
    str += "s, Error time ";
    str += std::to_string( _error_time_s );
    str += "s, Error retry time ";
    str += std::to_string( _error_retry_time_s );
    str += "s";
    return str;
}


EA_DoorClosingFailure::EA_DoorClosingFailure(
    json email_info,
    const std::string& device_name
    )
    : EvaluationAction( email_info, device_name )
    , _closing( false )
{
}

EA_DoorClosingFailure* EA_DoorClosingFailure::getActionFromJSON( HomeSystem* system, json ea )
{
    Logger::log( Logger::TRACE, "EA_DoorClosingFailure::getActionFromJSON from: " + ea.dump() );
    if ( getEvalActionTypeFromString( JSONUtils::getJSONStringValue( ea, J_EA_TYPE, "" ) ) != CUSTOM_EVAL )
    {
        Logger::log( Logger::TRACE, "EA_DoorClosingFailure::getActionFromJSON failed due to " + JSONUtils::getJSONStringValue( ea, J_EA_TYPE, "" ) + " Not being CUSTOM_EVAL");
        return 0;
    }
    if ( getCustomEvalTypeFromString( JSONUtils::getJSONStringValue( ea, J_CUSTOM_TYPE, "" ) ) != DOOR_FAILED_TO_CLOSE )
    {
        Logger::log( Logger::TRACE, "EA_DoorClosingFailure::getActionFromJSON failed due to " + JSONUtils::getJSONStringValue( ea, J_CUSTOM_TYPE, "" ) + " Not being DOOR_FAILED_TO_CLOSE");
        return 0;
    }
   
    json email;
    if ( ea.contains( J_EMAIL_INFO ) )
        email = ea.at( J_EMAIL_INFO );
    else
        email = system->getHomeConfig()->getEmailInfo();

    EA_DoorClosingFailure* ea_dcf = new EA_DoorClosingFailure(
        email,
        JSONUtils::getJSONStringValue( ea, J_DEVICE_NAME, "" )
    );
    if ( ea.contains( J_NOTIFICATION_CONFIG) )
    {
        ea_dcf->setNotificationConfig( ea.at(J_NOTIFICATION_CONFIG));
    }
    Logger::log( Logger::TRACE, "EA_DoorClosingFailure::getActionFromJSON created" );
    return ea_dcf;
}

// TODO This seems to be common
bool EA_DoorClosingFailure::initialize( HomeSystem* system )
{
    Logger::log( Logger::TRACE, "EA_DoorClosingFailure::initialize" );
    // Find the device
    DeviceStore* store = system->getDeviceStore();
    _device = store->getDevice( _device_name );
    if ( _device )
        _evaluation_state = WAIT;
    else
        Logger::log( Logger::ERROR, "EA_DoorClosingFailure::initialize could not find device with name: " + _device_name );
    return _device;
}

// TODO This seems to be common
bool EA_DoorClosingFailure::setNewEvaluationState( EVALUATION_STATE new_state, json& results )
{
    Logger::log( Logger::TRACE, "EA_DoorClosingFailure::setNewEvaluationState" );
    return defaultSetNewEvaluationState( new_state, results );
}

// If we were ON, then unknown, then back to ON, we failed to close
bool EA_DoorClosingFailure::evaluateInternal(const std::vector< device_evaluation >& evaluations, json& results, time_t& eval_time)
{
    bool triggered = false;
    for( std::vector< device_evaluation >::const_iterator itr = evaluations.begin(); itr != evaluations.end(); ++itr)
    {
        if ( _device != (*itr).device )
            continue;

        std::string state_str = (*itr).evaluation_validation.at( DeviceUtils::getShortNameForProperty( DeviceUtils::STATE ) );
        Logger::log( Logger::TRACE, std::string( "EA_DoorClosingFailure::evaluateInternal state_str as: " + state_str ) );
        DeviceUtils::DEVICE_STATE state = DeviceUtils::getStateFromString(state_str);
        std::string prev_state_str = (*itr).evaluation_validation.at( DeviceUtils::getShortNameForProperty( DeviceUtils::PREVIOUS_STATE ) );
        Logger::log( Logger::TRACE, std::string( "EA_DoorClosingFailure::evaluateInternal prev_state_str as: " + prev_state_str ) );
        DeviceUtils::DEVICE_STATE prev_state = DeviceUtils::getStateFromString(prev_state_str);

        // Check for state
        results[ J_EVALUATION_STRING ] = std::string( "Not triggered");
        // 
        if ( state == DeviceUtils::OFF ) // Door closed
        {
            // We are closed. Nothing to do.
            results[ J_EVALUATION_SUCCESS ] = true;
            _closing = false; // We are closed
        }
        else if ( ( prev_state == DeviceUtils::ON ) && ( state == DeviceUtils::NEITHER_UP_NOR_DOWN ))
        {
            // We should only be here for a minute max.
            _closing = true;
        }
        else if ( state == DeviceUtils::ON )
        {
            if ( _closing )
            {
                // We were triggered before, so we had started closing. Now we are back to open without reaching the closed step. We have an issue.
                results[ J_EVALUATION_SUCCESS ] = false;
                results[ J_EVALUATION_STRING ] = std::string( "Triggered for state being : " ) +
                                                 DeviceUtils::getStateAsString( DeviceUtils::NEITHER_UP_NOR_DOWN ) +
                                                 std::string(" and previous state being : ") +
                                                 DeviceUtils::getStateAsString( DeviceUtils::ON );
                triggered = true;
            }
        }
        eval_time = (*itr).evaluation_time;
        break; // Only have 1
    }
    return triggered;
}

std::string EA_DoorClosingFailure::getEvaluationActionAsString() const
{
//Max: -10 Temperature (C) Times watched (40) Warn time: 300s (38) Error time: 600s (38).
    std::string str = "Door Closing Failure";
    str += ": ";
    // TODO This seems to be common
    str += " Watch time ";
    str += std::to_string( _watch_time_s );
    str += "s, Warn time ";
    str += std::to_string( _warn_time_s );
    str += "s, Error time ";
    str += std::to_string( _error_time_s );
    str += "s, Error retry time ";
    str += std::to_string( _error_retry_time_s );
    str += "s";
    return str;
}


EA_WashingMachineLidDownTooLong::EA_WashingMachineLidDownTooLong(
    json email_info,
    const std::string& device_name
    )
    : EvaluationAction( email_info, device_name )
    , _closing( false )
{
}

EA_WashingMachineLidDownTooLong* EA_WashingMachineLidDownTooLong::getActionFromJSON( HomeSystem* system, json ea )
{
    Logger::log( Logger::TRACE, "EA_WashingMachineLidDownTooLong::getActionFromJSON from: " + ea.dump() );
    if ( getEvalActionTypeFromString( JSONUtils::getJSONStringValue( ea, J_EA_TYPE, "" ) ) != CUSTOM_EVAL )
    {
        Logger::log( Logger::TRACE, "EA_WashingMachineLidDownTooLong::getActionFromJSON failed due to " + JSONUtils::getJSONStringValue( ea, J_EA_TYPE, "" ) + " Not being CUSTOM_EVAL");
        return 0;
    }
    if ( getCustomEvalTypeFromString( JSONUtils::getJSONStringValue( ea, J_CUSTOM_TYPE, "" ) ) != DOOR_FAILED_TO_CLOSE )
    {
        Logger::log( Logger::TRACE, "EA_WashingMachineLidDownTooLong::getActionFromJSON failed due to " + JSONUtils::getJSONStringValue( ea, J_CUSTOM_TYPE, "" ) + " Not being DOOR_FAILED_TO_CLOSE");
        return 0;
    }
   
    json email;
    if ( ea.contains( J_EMAIL_INFO ) )
        email = ea.at( J_EMAIL_INFO );
    else
        email = system->getHomeConfig()->getEmailInfo();

    EA_WashingMachineLidDownTooLong* ea_dcf = new EA_WashingMachineLidDownTooLong(
        email,
        JSONUtils::getJSONStringValue( ea, J_DEVICE_NAME, "" )
    );
    if ( ea.contains( J_NOTIFICATION_CONFIG) )
    {
        ea_dcf->setNotificationConfig( ea.at(J_NOTIFICATION_CONFIG));
    }
    Logger::log( Logger::TRACE, "EA_WashingMachineLidDownTooLong::getActionFromJSON created" );
    return ea_dcf;
}

// TODO This seems to be common
bool EA_WashingMachineLidDownTooLong::initialize( HomeSystem* system )
{
    Logger::log( Logger::TRACE, "EA_WashingMachineLidDownTooLong::initialize" );
    // Find the device
    DeviceStore* store = system->getDeviceStore();
    _device = store->getDevice( _device_name );
    if ( _device )
        _evaluation_state = WAIT;
    else
        Logger::log( Logger::ERROR, "EA_WashingMachineLidDownTooLong::initialize could not find device with name: " + _device_name );
    return _device;
}

// TODO This seems to be common
bool EA_WashingMachineLidDownTooLong::setNewEvaluationState( EVALUATION_STATE new_state, json& results )
{
    Logger::log( Logger::TRACE, "EA_WashingMachineLidDownTooLong::setNewEvaluationState" );
    return defaultSetNewEvaluationState( new_state, results );
}

// If we were ON, then unknown, then back to ON, we failed to close
bool EA_WashingMachineLidDownTooLong::evaluateInternal(const std::vector< device_evaluation >& evaluations, json& results, time_t& eval_time)
{
    bool triggered = false;
    for( std::vector< device_evaluation >::const_iterator itr = evaluations.begin(); itr != evaluations.end(); ++itr)
    {
        if ( _device != (*itr).device )
            continue;

        std::string state_str = (*itr).evaluation_validation.at( DeviceUtils::getShortNameForProperty( DeviceUtils::STATE ) );
        Logger::log( Logger::TRACE, std::string( "EA_DoorClosingFailure::evaluateInternal state_str as: " + state_str ) );
        DeviceUtils::DEVICE_STATE state = DeviceUtils::getStateFromString(state_str);
        std::string prev_state_str = (*itr).evaluation_validation.at( DeviceUtils::getShortNameForProperty( DeviceUtils::PREVIOUS_STATE ) );
        Logger::log( Logger::TRACE, std::string( "EA_DoorClosingFailure::evaluateInternal prev_state_str as: " + prev_state_str ) );
        DeviceUtils::DEVICE_STATE prev_state = DeviceUtils::getStateFromString(prev_state_str);

        // Check for state
        results[ J_EVALUATION_STRING ] = std::string( "Not triggered");
        // 
        if ( state == DeviceUtils::OFF ) // Door closed
        {
            // We are closed. Nothing to do.
            results[ J_EVALUATION_SUCCESS ] = true;
            _closing = false; // We are closed
        }
        else if ( ( prev_state == DeviceUtils::ON ) && ( state == DeviceUtils::NEITHER_UP_NOR_DOWN ))
        {
            // We should only be here for a minute max.
            _closing = true;
        }
        else if ( state == DeviceUtils::ON )
        {
            if ( _closing )
            {
                // We were triggered before, so we had started closing. Now we are back to open without reaching the closed step. We have an issue.
                results[ J_EVALUATION_SUCCESS ] = false;
                results[ J_EVALUATION_STRING ] = std::string( "Triggered for state being : " ) +
                                                 DeviceUtils::getStateAsString( DeviceUtils::NEITHER_UP_NOR_DOWN ) +
                                                 std::string(" and previous state being : ") +
                                                 DeviceUtils::getStateAsString( DeviceUtils::ON );
                triggered = true;
            }
        }
        eval_time = (*itr).evaluation_time;
        break; // Only have 1
    }
    return triggered;
}

std::string EA_WashingMachineLidDownTooLong::getEvaluationActionAsString() const
{
//Max: -10 Temperature (C) Times watched (40) Warn time: 300s (38) Error time: 600s (38).
    std::string str = "Washing machine down too long";
    str += ": ";
    // TODO This seems to be common
    str += " Watch time ";
    str += std::to_string( _watch_time_s );
    str += "s, Warn time ";
    str += std::to_string( _warn_time_s );
    str += "s, Error time ";
    str += std::to_string( _error_time_s );
    str += "s, Error retry time ";
    str += std::to_string( _error_retry_time_s );
    str += "s";
    return str;
}

EA_CheckEmail::EA_CheckEmail(
        json email_info
    )
    : EvaluationAction( email_info, "Check Email" )
{
    _allowed_commands_default.insert(DeviceUtils::GET_STATUS);
    _allowed_commands_default.insert(DeviceUtils::HELP);
}

EA_CheckEmail* EA_CheckEmail::getActionFromJSON( HomeSystem* system, json ea )
{
    Logger::log( Logger::TRACE, "EA_CheckEmail::getActionFromJSON from: " + ea.dump() );
    if ( getEvalActionTypeFromString( JSONUtils::getJSONStringValue( ea, J_EA_TYPE, "" ) ) != PROCESS_CHECK_EMAIL )
    {
        Logger::log( Logger::TRACE, "EA_CheckEmail::getActionFromJSON: Not of type PROCESS_CHECK_EMAIL " + ea.dump() );
       return 0;
    }

    json email;
    if ( ea.contains( J_EMAIL_INFO ) )
        email = ea.at( J_EMAIL_INFO );
    else
        email = system->getHomeConfig()->getEmailInfo();

    EA_CheckEmail* ea_ec = new EA_CheckEmail(
        email
    );
    if ( ea.contains( J_NOTIFICATION_CONFIG) )
    {
        ea_ec->setNotificationConfig( ea.at(J_NOTIFICATION_CONFIG));
    }
    std::string allowed_emails = JSONUtils::getJSONStringValue( ea, J_EMAIL_ALLOWED_EMAILS, "default" );
    if ( strcmp( "default", allowed_emails.c_str() ) == 0 )
    {
        Logger::log( Logger::TRACE, "EA_CheckEmail::getActionFromJSON: allowed_emails is default. ");
    }
    std::string allowed_commands = JSONUtils::getJSONStringValue( ea, J_EMAIL_ALLOWED_COMMANDS, "default" );
    if ( strcmp( "default", allowed_commands.c_str() ) == 0 )
    {
        Logger::log( Logger::TRACE, "EA_CheckEmail::getActionFromJSON: allowed_commands is default. ");
    }
    Logger::log( Logger::TRACE, "EA_CheckEmail::getActionFromJSON created" );
    return ea_ec;
}

bool EA_CheckEmail::initialize( HomeSystem* system )
{
    Logger::log( Logger::TRACE, "EA_CheckEmail::initialize - Nothing to do" );
    return true;
}

bool EA_CheckEmail::isCommandAllowed( DeviceUtils::DEVICE_COMMAND ) const
{
    // TODO Check against set

    return true;
}

bool EA_CheckEmail::evaluateInternal(const std::vector< device_evaluation >& evaluations, json& results, time_t& eval_time)
{
    Logger::log( Logger::TRACE, "EA_CheckEmail::evaluateInternal" );
    bool triggered = false;
    for( std::vector< device_evaluation >::const_iterator itr = evaluations.begin(); itr != evaluations.end(); ++itr)
    {
        // Look for email subject containing "status"
        if ((*itr).evaluation_results.contains(J_EMAIL_RECEIVED_EMAILS ))
        {
            Logger::log( Logger::TRACE, "EA_CheckEmail::evaluateInternal contains J_EMAIL_RECEIVED_EMAILS" );
            bool done_status = false;
            json emails = (*itr).evaluation_results.at( J_EMAIL_RECEIVED_EMAILS );
            for( json::const_iterator j_itr = emails.begin(); j_itr != emails.end(); ++j_itr )
            {
                //TODO Check and see if they are authourized
                if ((*j_itr).contains("sender"))
                {
                    // Check against the list
                }
                else
                {
                    Logger::log( Logger::WARNING, "EA_CheckEmail::evaluateInternal email from unknown sender:" + (*j_itr).dump() );
                }
                if ((*j_itr).contains("subject"))
                {
                    Logger::log( Logger::TRACE, "EA_CheckEmail::evaluateInternal contains subject" );
                    std::string subject = (*j_itr).at("subject");
                    subject = PiUtils::toLowerCase(subject);
                    // Check the subject for commands
                    if (isCommandAllowed( DeviceUtils::GET_STATUS) && (subject.find(DeviceUtils::getCommandAsEmailString(DeviceUtils::GET_STATUS)) != std::string::npos))
                    {
                        if ( done_status )
                        {
                            // Only do it once.
                            continue;
                        }
                        // We need to send a status command.
                        json status_event;
                        status_event[ J_VERSION ] = 2;
                        status_event[ J_TIME_IMMEDIATE ] = true; // Make sure it happens this cycle
                        status_event[ J_EVENT_ORIGINATOR_TYPE ] = HomeEvent::getEventOriginatorTypeAsString( HomeEvent::ANALYZED );
                        status_event[ J_EVENT_PROCESSOR_TYPE ] = HomeEventProcessor::getEventProcessorTypeAsString( HomeEventProcessor::RUN_COMMAND );
                        status_event[ J_COMMAND ] = DeviceUtils::getCommandAsString( DeviceUtils::GET_STATUS );

                        // Add out event to the results
                        results[ J_NEW_EVENT ] = status_event;
                        done_status = true;
                        triggered = true;
                    }
                    else if (isCommandAllowed( DeviceUtils::MUTE) && (subject.find(DeviceUtils::getCommandAsEmailString(DeviceUtils::MUTE)) != std::string::npos))
                    {
                        // When we mute, we need to find the device and the evaluation we want to mute.
                        //json mute_event =
                        std::string device_name = "";
                        /*
                        if (EvaluationAction::getDeviceFromEASubject( subject, "monitor", device_name) )
                        {
                            // We need to send a status command.
                            json status_event;
                            status_event[ J_VERSION ] = 2;
                            status_event[ J_TIME_IMMEDIATE ] = true; // Make sure it happens this cycle
                            status_event[ J_EVENT_ORIGINATOR_TYPE ] = HomeEvent::getEventOriginatorTypeAsString( HomeEvent::ANALYZED );
                            status_event[ J_EVENT_PROCESSOR_TYPE ] = HomeEventProcessor::getEventProcessorTypeAsString( HomeEventProcessor::RUN_COMMAND );
                            status_event[ J_COMMAND ] = DeviceUtils::getCommandAsString( DeviceUtils::GET_STATUS );

                            // Add out event to the results
                            results[ J_NEW_EVENT ] = status_event;
                        }
                        */
                        done_status = true;
                        triggered = true;
                    }
                }
            }
        }
    }
    return triggered;
}

std::string EA_CheckEmail::getEvaluationActionAsString() const
{
    std::string str = "Check email supported commands:";
    str += " status";
    return str;
}

} // namespace Evaluation

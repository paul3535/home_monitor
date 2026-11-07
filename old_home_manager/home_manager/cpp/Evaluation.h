#ifndef EVALUATION_H
#define EVALUATION_H
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
#include <mutex>

#include <algorithm> 
#include <functional> 
#include <cctype>
#include <locale>

#include "nlohmann/json.hpp"
using json = nlohmann::json;
#define USE_PYTHON_FOR_GPIO
//#define USE_GPIOCLASS_FOR_GPIO
//#define USE_PSEUDO_RASPBERRY_PI

#ifdef USE_GPIOCLASS_FOR_GPIO
class GPIOClass;
#endif

#include "GPIOManager.h"
#include "DeviceUtils.h"
#include "PiUtils.h"

class HomeSystem;
class HomeConfig;
class Device;

namespace Evaluation
{

struct device_evaluation
{
    const Device* device;
    time_t evaluation_time;
    /* 
     * "property_name": "temp_c"
     * "property_pretty_name": "Temperature - C"
     * "value" : -17.00
     */
    json results;
    json evaluation_results;
    json evaluation_validation;
    json evaluation_metadata;
};

struct evaluation_stats
{
    const Device* device;

    /* TYPE_FLOAT, TYPE_INT
     * "temp_c" :
     * {
     *  J_PROPERTY_TYPE : "float"
     *  "min" : -17.00
     *  "max" : -15.00
     *  "avg" : -16.00
     *  "total" : -8000.00
     * }
     * TYPE_STATE
     * "pump_state" :
     * {
     *  "times_changed" : 3
     *  "state_a" : 40 // NOTE: state_a to be replaced by actual name
     *  "state_b" : 10
     *  "state_c" : 10
     * }
     * TYPE_BOOL
     * "on_off" :
     * {
     *  "time_on" : 3
     *  "time_off" : 57
     * }
     * "
     */
    json evaluation_stat_json;

    json evaluation_metadata;
    /*
     * {
     *  "num_samples" : 60
     *  "num_invalid_evaluations" : 0
     *  "start_time" : 0
     *  "end_time" : 0 // This is the last sample
     *  "is_valid" : true
     * }
     */
    bool is_valid;
    time_t start_time;
    time_t end_time;

    json asJson() const;
};

enum EVALUATION_STATE
{
	WAIT,
	WATCH,
	WARN,
	ERROR,
	ERROR_RETRY,
	TEMPORARILY_DISABLED,
	UNKNOWN_EVALUATION_STATE
};

inline const char * getEvaluationStateAsString( EVALUATION_STATE state, bool short_name = false )
{
    switch( state )
    {
        case WAIT:
            return short_name ? "WAIT" : "Wait";
        case WATCH:
            return short_name ? "WATCH" : "Watch";
        case WARN:
            return short_name ? "WARN" : "Warning";
        case ERROR:
            return short_name ? "ERROR" : "Error";
        case ERROR_RETRY:
            return short_name ? "ERROR_RETRY" : "Error retry";
        case TEMPORARILY_DISABLED:
            return short_name ? "TEMPORARILY_DISABLED" : "Termporarily Disabled";
        case UNKNOWN_EVALUATION_STATE:
		default:
            return short_name ? "UNKNOWN_EVALUATION_STATE" : "Unknown evaluation state";
    };
}

inline EVALUATION_STATE getEvaluationStateFromString( std::string str )
{
    for( int i = 0; i < UNKNOWN_EVALUATION_STATE; ++i )
    {
        if ( strcmp( str.c_str(), getEvaluationStateAsString( (EVALUATION_STATE) i, true ) ) == 0 )
        {
            return (EVALUATION_STATE) i;
        }
        if ( strcmp( str.c_str(), getEvaluationStateAsString( (EVALUATION_STATE) i ) ) == 0 )
        {
            return (EVALUATION_STATE) i;
        }
    }
    return UNKNOWN_EVALUATION_STATE;
}

class EvaluationStore
{
    public:
        EvaluationStore( HomeSystem* system );
        ~EvaluationStore();

        bool generateHistoryTable( time_t evaluation_time, json& results, const json& options, PiUtils::TEXT_FORMAT );
        bool generateStatsTable( time_t evaluation_time, json& results, const json& options, PiUtils::TEXT_FORMAT );
        bool generateStatus( time_t evaluation_time, json& results, const json& options, PiUtils::TEXT_FORMAT, bool is_startup = false );
        void setLoggingEnabled( bool enable ) { _logging_enabled = enable; }
        // This needs to be of the form /dir/file
        // The log will be:
        // /dir/file_date.log
        // and a symlink to /dir/file.log for the latest
        void setLogFileBaseNameLocation( const std::string& location ) { _logging_location = location + "_evaluations_"; }
        bool addEvaluations( time_t evaluation_time, std::vector< device_evaluation >& evaluations );

    private:
        bool expireEvaluations( );
        void determineNextStatsCycleEnd( bool reset );
        bool calculateStats();
        bool logEvaluations( time_t evaluation_time, std::vector< device_evaluation >& evaluations );
        bool logEvaluationStats( time_t evaluation_time, std::vector< evaluation_stats >& stats );

    private:
        HomeSystem* _system;
        std::map< time_t, std::vector< device_evaluation > > _evaluations;
        std::map< time_t, std::vector< evaluation_stats > > _evaluation_stats;
        bool _logging_enabled;
        std::string _logging_location;

        time_t _last_stats_time; // This is the time of the last stat we used. When we do the stats, we need to go until we reach this.
        time_t _cur_stats_cycle_end; // Any time between _last_stats_time and _cur_stats_cycle_end will be included
};

class EvaluationAction
{
    public:
        EvaluationAction( json email_info, const std::string& device_name );
        virtual ~EvaluationAction( ) {;}

        static EvaluationAction* getCustomAction( HomeSystem* system, json ea );
        static bool getDeviceFromEASubject( const std::string& subject, const std::string& curr_system_name, const std::string& device_name );

        json evaluate( const std::vector< device_evaluation >& evaluations );
        virtual bool initialize( HomeSystem* system ) = 0; // NOTE: This should be called once all of the devices are added
        virtual std::string getEvaluationName() const = 0;

        std::string getDeviceName() const { return _device_name; }

        virtual std::string getEvaluationActionAsString() const = 0;


    protected:
        bool defaultSetNewEvaluationState( EVALUATION_STATE new_state, json& results );
        virtual bool setNewEvaluationState( EVALUATION_STATE new_state, json& results ) { return false; }

        // Returns if the evaluation has been triggered
        virtual bool evaluateInternal(const std::vector< device_evaluation >& evaluations, json& results, time_t& eval_time) = 0;

        bool setNotificationConfig( json config );
        void setWatchWarnErrorTimes( unsigned int watch, unsigned int warn, unsigned int error, unsigned int error_retry )
        {
            _watch_time_s = watch;
            _warn_time_s = warn;
            _error_time_s = error;
            _error_retry_time_s = error_retry;
            if ( _error_retry_time_s == 0 )
                _error_retry_time_s = _error_time_s;
        }

    protected:
        Device* _device;
        EVALUATION_STATE _evaluation_state;
        json _email_info;
        std::string _device_name;

        unsigned int _watch_time_s; // NOTE: This is how long since we have triggered until we transition to watch
        unsigned int _warn_time_s; // NOTE: This is how long since we have triggered until we transition to warn
        unsigned int _error_time_s; // NOTE: This is how long since we have triggered until we transition to warn
        unsigned int _error_retry_time_s; // NOTE: This is how often we retry once we are in error
        time_t _curr_time_triggered;
        unsigned int _error_counter;
        bool _email_watch;
        bool _email_warn;
        bool _email_error;
        time_t _mute_until_time;
};

enum BOUNDS_OPERATION
{
    LESS_THAN,
    LESS_THAN_EQUAL_TO,
    EQUAL_TO,
    GREATER_THAN_EQUAL_TO,
    GREATER_THAN,
    NOT_EQUAL,
    BETWEEN,
    INVALID_BOUNDS_OPERATION
};

inline const char * getBoundsOperationAsString( BOUNDS_OPERATION command, bool short_name = false )
{
    switch( command )
    {
        case LESS_THAN:
            return short_name ? "LESS_THAN" : "less_than";
        case LESS_THAN_EQUAL_TO:
            return short_name ? "LESS_THAN_EQUAL_TO" : "less_than_equal_to";
        case EQUAL_TO:
            return short_name ? "EQUAL_TO" : "equal_to";
        case GREATER_THAN_EQUAL_TO:
            return short_name ? "GREATER_THAN_EQUAL_TO" : "greater_than_equal_to";
        case GREATER_THAN:
            return short_name ? "GREATER_THAN" : "greater_than";
        case NOT_EQUAL:
            return short_name ? "NOT_EQUAL" : "not_equal";
        case BETWEEN:
            return short_name ? "BETWEEN" : "between";
        case INVALID_BOUNDS_OPERATION:
            return "Invalid Bounds Operation";
    };
    return "Invalid Bounds Operation";
}

inline BOUNDS_OPERATION getBoundsOperationFromString( std::string str )
{
    for( int i = 0; i < INVALID_BOUNDS_OPERATION; ++i )
    {
        if ( strcmp( str.c_str(), getBoundsOperationAsString( (BOUNDS_OPERATION) i, true ) ) == 0 )
        {
            return (BOUNDS_OPERATION) i;
        }
        if ( strcmp( str.c_str(), getBoundsOperationAsString( (BOUNDS_OPERATION) i ) ) == 0 )
        {
            return (BOUNDS_OPERATION) i;
        }
    }
    return INVALID_BOUNDS_OPERATION;
}

// This only cares about 1 device and if a reading of "property" is past the bounds of what we expect.
class EA_BoundsThreshold : public EvaluationAction
{
    public:
        EA_BoundsThreshold( json email_info,
                            const std::string& device_name,
                            const std::string& property,
                            BOUNDS_OPERATION op,
                            float val,
                            float secondary_val = 0 );
        virtual ~EA_BoundsThreshold() {;}

        static EA_BoundsThreshold* getActionFromJSON( HomeSystem* system, json ea );
        virtual bool initialize( HomeSystem* system ); // NOTE: This should be called once all of the devices are added
        virtual std::string getEvaluationName() const { return "EA_BoundsThreshold"; }

        
        virtual std::string getEvaluationActionAsString() const;
    protected:
        // Returns if the evaluation has been triggered
        virtual bool evaluateInternal(const std::vector< device_evaluation >& evaluations, json& results, time_t& eval_time);

    protected:
        virtual bool setNewEvaluationState( EVALUATION_STATE new_state, json& results );

    private:
        const std::string _property;
        float _val;
        float _secondary_val; // Only used for BETWEEN
        BOUNDS_OPERATION _op;

};

// This only cares about 1 device and if it is in a specific state for a specific amount of time
class EA_StateThreshold : public EvaluationAction
{
    public:
        EA_StateThreshold( json email_info,
                            const std::string& device_name,
                            DeviceUtils::DEVICE_STATE state);
        virtual ~EA_StateThreshold() {;}

        static EA_StateThreshold* getActionFromJSON( HomeSystem* system, json ea );
        virtual bool initialize( HomeSystem* system ); // NOTE: This should be called once all of the devices are added
        virtual std::string getEvaluationName() const { return "EA_StateThreshold"; }

        virtual std::string getEvaluationActionAsString() const;
    protected:
        // Returns if the evaluation has been triggered
        virtual bool evaluateInternal(const std::vector< device_evaluation >& evaluations, json& results, time_t& eval_time);

        virtual bool setNewEvaluationState( EVALUATION_STATE new_state, json& results );

    private:
        const std::string _property;
        DeviceUtils::DEVICE_STATE _state;
};

// This only cares about 1 device and if it is in a specific state for a specific amount of time
class EA_DoorClosingFailure : public EvaluationAction
{
    public:
        EA_DoorClosingFailure( json email_info,
                               const std::string& device_name);
        virtual ~EA_DoorClosingFailure() {;}

        static EA_DoorClosingFailure* getActionFromJSON( HomeSystem* system, json ea );
        virtual bool initialize( HomeSystem* system ); // NOTE: This should be called once all of the devices are added
        virtual std::string getEvaluationName() const { return "EA_DoorClosingFailure"; }

        virtual std::string getEvaluationActionAsString() const;

    protected:
        // Returns if the evaluation has been triggered
        virtual bool evaluateInternal(const std::vector< device_evaluation >& evaluations, json& results, time_t& eval_time);

        virtual bool setNewEvaluationState( EVALUATION_STATE new_state, json& results );

    private:
        bool _closing;
};

// This only cares about 1 device and if it is in a specific state for a specific amount of time
class EA_WashingMachineLidDownTooLong : public EvaluationAction
{
    public:
        EA_WashingMachineLidDownTooLong( json email_info,
                               const std::string& device_name);
        virtual ~EA_WashingMachineLidDownTooLong() {;}

        static EA_WashingMachineLidDownTooLong* getActionFromJSON( HomeSystem* system, json ea );
        virtual bool initialize( HomeSystem* system ); // NOTE: This should be called once all of the devices are added
        virtual std::string getEvaluationName() const { return "EA_WashingMachineLidDownTooLong"; }

        virtual std::string getEvaluationActionAsString() const;

    protected:
        // Returns if the evaluation has been triggered
        virtual bool evaluateInternal(const std::vector< device_evaluation >& evaluations, json& results, time_t& eval_time);

        virtual bool setNewEvaluationState( EVALUATION_STATE new_state, json& results );

    private:
        bool _closing;
};

class EA_CheckEmail : public EvaluationAction
{
    public:
        EA_CheckEmail( json email_info );
        virtual ~EA_CheckEmail() {;}

        static EA_CheckEmail* getActionFromJSON( HomeSystem* system, json ea );
        virtual bool initialize( HomeSystem* system ); // NOTE: This should be called once all of the devices are added
        virtual std::string getEvaluationName() const { return "EA_CheckEmail"; }

        virtual std::string getEvaluationActionAsString() const;

    protected:
        // Returns if the evaluation has been triggered
        virtual bool evaluateInternal(const std::vector< device_evaluation >& evaluations, json& results, time_t& eval_time);

    private:
        bool isCommandAllowed( DeviceUtils::DEVICE_COMMAND command ) const;

    private:
        std::set< DeviceUtils::DEVICE_COMMAND > _allowed_commands;
        std::set< DeviceUtils::DEVICE_COMMAND > _allowed_commands_default;
};

class EvaluationActionManager
{

    public:
        EvaluationActionManager(HomeSystem*);
        ~EvaluationActionManager();

        //bool loadEvaluationActions( const HomeConfig* config, bool clear );

        bool initializeActions( HomeSystem* system );
        json runEvaluationActions( const std::vector< device_evaluation >& evaluations );
        bool addEvaluationAction( json ea );
        void addEvaluationActionsForDevice( const Device*& device, json& info ) const;

    private:
        HomeSystem* _system;
        std::vector< EvaluationAction* > _actions;
};

}
#endif

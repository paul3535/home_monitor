#ifndef JSON_UTILS
#define JSON_UTILS
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

namespace JSONUtils
{
    const static int J_CURRENT_VERSION = 2;
    const static std::string J_VERSION = "version";
    const static std::string J_SYSTEM_NAME = "system_name";
    const static std::string J_SYSTEM_PRETTY_NAME = "system_pretty_name";
    const static std::string J_BASE_INSTALL_DIR = "base_install_dir";
    const static std::string J_CONFIG_FILES = "config_files";
    const static std::string J_EVENT_FILES = "event_files";
    const static std::string J_DEBUG_OBJECT = "debug";
    const static std::string J_DEBUG_OBJECT_ENABLED = "enabled";
    const static std::string J_EVAL_FREQUENCY_SECONDS = "evaluation_frequency_seconds";
    const static std::string J_EVAL_FREQUENCY_SECONDS_MIN = "evaluation_frequency_seconds_min";

    const static std::string J_UUID = "uuid";
    const static std::string J_LOGGING_OBJECT = "logging";
    const static std::string J_LOGGING_ENABLED = "enabled";
    const static std::string J_LOGGING_BASE_DIR = "base_dir";
    const static std::string J_LOG_TO_FILE = "log_to_file";
    const static std::string J_LOG_LEVEL_TO_SEPARATE_FILES = "log_level_to_separate_files";
    const static std::string J_LOG_LEVEL = "log_level";
    const static std::string J_EVENT_ORIGINATOR_TYPE = "originator_type";
    const static std::string J_EVENT_PROCESSOR_TYPE = "event_type";
    const static std::string J_EVENT_INFO = "event_info";
    const static std::string J_EVENT_ACK_INFO = "event_ack_info";

    // Device files
    const static std::string J_DEVICE = "device";
    const static std::string J_DEVICES = "devices";
    const static std::string J_RESULTS = "results";
    const static std::string J_VALIDATION_RESULTS = "validation_results";

    const static std::string J_NAME = "name";
    const static std::string J_SUB_SECTIONS_NAME = "sub_sections";

    const static std::string J_TIME = "time";
    const static std::string J_TYPE = "type";
    const static std::string J_DS18B20 = "ds18b20";
    const static std::string J_EMAIL_CHECK = "email_check";
    const static std::string J_RELAY = "relay";
    const static std::string J_GPIO_DEVICE = "gpio_device";
    const static std::string J_GPIO_PIN = "gpio_pin"; // input, output
    const static std::string J_GPIO_MODE = "gpio_mode";
    const static std::string J_GPIO_TYPE = "gpio_type"; // reed_switch_normally_open, relay
    const static std::string J_DEVICE_TYPE = "device_type";
    const static std::string J_GPIO_NUM = "gpio_num";
    const static std::string J_DEVICE_GARAGE_DOOR = "device_garage_door";
    const static std::string J_DEVICE_GARAGE_UP = "device_garage_up";
    const static std::string J_DEVICE_GARAGE_DOWN = "device_garage_down";
    const static std::string J_NOTIFICATION_CONFIG = "notification_config";
    const static std::string J_NOTIFICATION_WATCH = "notification_watch";
    const static std::string J_NOTIFICATION_WARN = "notification_warn";
    const static std::string J_NOTIFICATION_ERROR = "notification_error";
    const static std::string J_DEVICE_WASHING_MACHINE = "washing_machine";
    const static std::string J_DEVICE_WASHING_MACHINE_LID_UP = "washing_machine_lid_up";
    const static std::string J_DEVICE_WASHING_MACHINE_LID_DOWN = "washing_machine_lid_down";
    const static std::string J_CUSTOM_TYPE = "custom_type";

    // EMAIL
    const static std::string J_SEND_EMAIL_CREDS_PYTHON_FILE = "send_email_creds_python_file";
    const static std::string J_GET_EMAIL_CREDS_PYTHON_FILE = "get_email_creds_python_file";
    const static std::string J_SEND_EMAIL_PYTHON_FILE = "send_email_python_file";
    const static std::string J_GET_EMAIL_PYTHON_FILE = "get_email_python_file";
    const static std::string J_EMAIL_TO = "to";
    const static std::string J_EMAIL_FROM = "from";
    const static std::string J_EMAIL_SUBJECT = "subject";
    const static std::string J_EMAIL_BODY = "body";
    const static std::string J_EMAIL_ATTACHMENT = "attachment";
    const static std::string J_EMAIL_HTML = "html";
    const static std::string J_EMAIL_BASE_DIR = "email_base_dir";
    const static std::string J_EMAIL_INFO = "email_info";
    const static std::string J_EMAIL_HEADER = "header";
    const static std::string J_EMAIL_CURR_TABLE = "curr_table";
    const static std::string J_EMAIL_STATS_TABLE = "stats_table";
    const static std::string J_EMAIL_FOOTER = "footer";
    const static std::string J_EMAIL_CHECK_LABEL = "email_check_label";
    const static std::string J_EMAIL_CHECK_FREQUENCY_SECONDS = "email_check_frequency_seconds";
    const static std::string J_EMAIL_RECEIVED_EMAILS = "received_emails";
    const static std::string J_EMAIL_QUERIES = "email_queries";
    const static std::string J_EMAIL_ALLOWED_EMAILS = "allowed_emails";
    const static std::string J_EMAIL_ALLOWED_COMMANDS = "allowed_commands";

    // TIME
    const static std::string J_TIME_FULL = "time_full";         // YYMMDD_HHMMSS // Fully defined
    const static std::string J_TIME_YYMMDD = "time_yymmdd";     // YYMMDD // Time will be assumed to be 00:00:00
    const static std::string J_TIME_MMDD = "time_mmdd";         // MMDD // Year assumed as the next MMDD to happen
    const static std::string J_TIME_DAY_TIME = "time_day_time"; // Monday 04:00:00 // 
    const static std::string J_TIME_DAY = "time_day";           // Monday    // Time will be assumed to be 00:00:00
    const static std::string J_TIME_TIME = "time_time";         // 10:00:00  // Day will be assumed to be today
    const static std::string J_TIME_DATE = "time_date";         // 10:00:00  // Day will be assumed to be today
    const static std::string J_TIME_OFFSET_SECONDS = "time_offset_seconds"; // 30 // 30 seconds after it is added
    const static std::string J_TIME_IMMEDIATE = "time_immediate";

    const static std::string J_COMMAND = "command";
    const static std::string J_GPIO_ENABLED = "gpio_enabled";
    const static std::string J_GPIO_MANAGER_TYPE = "gpio_manager_type";
    const static std::string J_USE_INTERRUPTS = "use_interrupts";
    const static std::string J_SPEED = "speed";
    const static std::string J_VS_PUMP = "vs_pump";

    const static std::string J_EVALUATION_SUCCESS = "evaluation_success";
    const static std::string J_EVALUATION_STRING = "evaluation_string";
    const static std::string J_EA_WATCH_TIME = "watch_time";
    const static std::string J_EA_WARN_TIME = "warn_time";
    const static std::string J_EA_ERROR_TIME = "error_time";
    const static std::string J_EA_ERROR_RETRY_TIME = "error_retry_time";
    const static std::string J_EA_DATUM = "datum";
    const static std::string J_EA_DATUM_2 = "datum_2";
    const static std::string J_EA_TYPE = "type";
    const static std::string J_EA_THRESHOLD_LIST = "threshold_list";
    const static std::string J_EA_THRESHOLD_TYPE = "threshold_type";
    const static std::string J_EA_PROPERTY_NAME = "property_name";
    const static std::string J_EA_OPERATION = "operation";

    const static std::string J_EVALUATION_STATS_INTERVAL_SECONDS = "evaluation_stats_internal_seconds";

    const static std::string J_ENABLED = "enabled";
    const static std::string J_TIME_OFFSET_UNITS = "time_offset_units";
    const static std::string J_TIME_OFFSET_VALUE = "time_offset_value";

    const static std::string J_EMAIL_RETRY_COUNT = "email_retry_count";
    const static std::string J_EMAIL_RETRY_LIMIT = "email_retry_limit";
    const static std::string J_EMAIL_RETRY_FREQ_SECONDS = "email_retry_freq_seconds";

    // Private
    const static std::string J_PRIVATE_FILE = "private_file";
    const static std::string J_EMAIL_USERNAME = "email_username";
    const static std::string J_EMAIL_PASSWORD = "email_password";
    const static std::string J_EMAIL_CREDS_FILE = "email_creds_file";
    const static std::string J_EMAIL_TOKEN_FILE = "email_token_file";

    const static std::string J_STATUS_ADDRESSES = "status_addresses";
    const static std::string J_WATCH_ADDRESSES = "watch_addresses";
    const static std::string J_WARN_ADDRESSES = "warn_addresses";
    const static std::string J_ERROR_ADDRESSES = "error_addresses";
    const static std::string J_EVENT_ID = "event_id";

    const static std::string J_NEW_EVENT = "new_event";
    const static std::string J_EVALUATION_ACTIONS = "evaluation_actions";
    const static std::string J_EVALUATION_PROPERTY_TYPE = "evaluation_property_type";
    const static std::string J_DEVICE_NAME = "device_name";
    const static std::string J_ADD_STATUS_HTML = "add_status_html";
    const static std::string J_ORIGINAL_TIME = "original_time";
    const static std::string J_REPEAT_EVENT = "repeat_event";
    const static std::string J_REPEAT_SECONDS = "repeat_seconds";
    const static std::string J_SYSTEM_INFO = "system_info";
    const static std::string J_ADD_SYSTEM_INFO = "add_system_info";

    // Status options
    const static std::string J_S_CONSTRUCT_BODY = "status_construct_body";
    const static std::string J_S_GENERATE_SUBJECT = "status_generate_subject";
    const static std::string J_S_NUM_EVALUATIONS = "status_number_of_evaluations";
    const static std::string J_S_TIME_EVALUATIONS_S = "status_time_evaluations_s";
    const static std::string J_S_TIME_EVALUATION_STATS_S = "status_time_evaluation_stats_s";
    const static std::string J_S_NUM_STATS = "status_number_of_statistics";
    const static std::string J_INCLUDE_EVENTS = "include_events";
    const static std::string J_EVENTS_TABLE = "events_table";
    const static std::string J_EVENTS_ACK_TABLE = "events_ack_table";
    const static std::string J_RUN_TIME = "run_time";

    const static std::string J_DS18B20_CONFIG = "ds18b20_config";
    const static std::string J_DS18B20_ONE_WIRE_PIN = "one_wire_pin";

    const static std::string J_CAPABILITIES_LIST = "capabilities";
    const static std::string J_PROPERTY_NAMES = "property_names";
    const static std::string J_PROPERTY_NAME = "property_name";
    const static std::string J_PROPERTY_LONG_NAME = "property_long_name";
    const static std::string J_PROPERTY_TYPE = "property_type";
    const static std::string J_VAL = "val";
    const static std::string J_EVAL_MIN = "min";
    const static std::string J_EVAL_MAX = "max";
    const static std::string J_EVAL_TOTAL = "total";
    const static std::string J_NUM_SAMPLES = "num_samples";
    const static std::string J_STATS = "stats";
    const static std::string J_METADATA = "metadata";
    const static std::string J_VALID = "valid";
    const static std::string J_START_TIME = "start_time";
    const static std::string J_END_TIME = "end_time";
    const static std::string J_SEND_STARTUP = "send_startup";

    time_t getTimeFromJSON_futureDayIfInPast( json object, int num_allowed_seconds );
    time_t getTimeFromJSON( json object, bool is_repeat = false, time_t start_time  = 0);
    void clearTimeFromJSON( json& object );

    std::string getJSONStringValue( json j, std::string property, std::string default_val );
    int getJSONIntValue( json j, std::string property, int default_val );
    bool getJSONBoolValue( json j, std::string property, bool default_val );
    float getJSONFloatValue( json j, std::string property, float default_val );
    json getJSONArrayValue( json j, std::string property );
};
#endif

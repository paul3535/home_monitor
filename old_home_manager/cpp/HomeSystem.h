#ifndef HOME_SYSTEM_H
#define HOME_SYSTEM_H
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

#define USE_PYTHON_FOR_GPIO
//#define USE_GPIOCLASS_FOR_GPIO
//#define USE_PSEUDO_RASPBERRY_PI

#ifdef USE_GPIOCLASS_FOR_GPIO
class GPIOClass;
#endif

#include "Device.h"
#include "Evaluation.h"
#include "HomeEvent.h"
#include "PiUtils.h"

class DeviceStore;
class EvaluationStore;
class EvaluationActionManager;
class HomeConfig;
class HomeEventManager;
class GPIOManager;

class HomeSystem
{
    public:
        HomeSystem( HomeConfig* config );
        ~HomeSystem( );

        static HomeSystem* getHomeSystem( HomeConfig* config );
        
        HomeConfig* getHomeConfig() const { return _config; }

        bool runSystem(); // NOTE! This is meant to be called as a thread

        bool refreshFromConfig();

        GPIOManager* getGPIOManager() const { return _gpio_manager; }

        DeviceStore* getDeviceStore() const { return _device_store; }

        Evaluation::EvaluationStore* getEvaluationStore() const { return _evaluation_store; }

        Evaluation::EvaluationActionManager* getEvaluationActionManager() const { return _evaluation_action_manager; }

        bool addHomeEvent( json event, HomeEvent::EVENT_ORIGINATOR_TYPE originator );

        HomeEventManager* getEventManager() const { return _event_manager; }

    private:
        bool initializeDevices( );
        bool evaluateDevices( time_t evaluation_time, std::vector< Evaluation::device_evaluation >& evaluations );
        bool processEvents( time_t evaluation_time );
        bool processAnalyzedEvaluations( time_t evaluation_time, json analyzed_results );

    private:
        HomeConfig* _config;
        unsigned int _cycle;
        DeviceStore* _device_store;
        Evaluation::EvaluationStore* _evaluation_store;
        Evaluation::EvaluationActionManager* _evaluation_action_manager;
        HomeEventManager* _event_manager;
        GPIOManager* _gpio_manager;
};

#endif

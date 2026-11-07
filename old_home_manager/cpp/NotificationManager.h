#ifndef NOTIFICATION_MANAGER_H
#define NOTIFICATION_MANAGER_H

#include <string>
#include <vector>
#include <map>

class NotificationManager {
public:
    struct Device {
        std::string name;
        std::string deviceId;
        std::vector<std::string> groups;
    };

    // Constructor
    NotificationManager(const std::string& configFile);

    // Loads device and group information from the JSON configuration file
    void loadConfiguration();

    // Sends a notification to a specific device by name
    bool sendToDevice(const std::string& deviceName, const std::string& message);

    // Sends a notification to all devices in a specific group
    bool sendToGroup(const std::string& groupName, const std::string& message);

private:
    std::string configFilePath;
    std::map<std::string, Device> devices;           // Map of device name to Device struct
    std::map<std::string, std::vector<std::string>> groups; // Map of group name to list of device IDs

    // Helper function to send a POST request to FCM
    bool sendToFCM(const std::string& deviceId, const std::string& message);

    // Parses a JSON string to populate devices and groups
    void parseJsonConfig(const std::string& jsonContent);
};

#endif // NOTIFICATION_MANAGER_H

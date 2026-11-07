#include "NotificationManager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <curl/curl.h>
#include <nlohmann/json.hpp> // JSON library: https://github.com/nlohmann/json

using json = nlohmann::json;

// Constructor
NotificationManager::NotificationManager(const std::string& configFile)
    : configFilePath(configFile) {}

// Loads device and group information from the JSON configuration file
void NotificationManager::loadConfiguration() {
    std::ifstream configFile(configFilePath);
    if (!configFile.is_open()) {
        throw std::runtime_error("Failed to open configuration file: " + configFilePath);
    }

    std::stringstream buffer;
    buffer << configFile.rdbuf();
    parseJsonConfig(buffer.str());
}

// Parses a JSON string to populate devices and groups
void NotificationManager::parseJsonConfig(const std::string& jsonContent) {
    json config = json::parse(jsonContent);

    for (const auto& device : config) {
        Device d;
        d.name = device.at("name").get<std::string>();
        d.deviceId = device.at("device_id").get<std::string>();
        d.groups = device.at("groups").get<std::vector<std::string>>();

        devices[d.name] = d;
        for (const auto& group : d.groups) {
            groups[group].push_back(d.deviceId);
        }
    }
}

// Sends a notification to a specific device by name
bool NotificationManager::sendToDevice(const std::string& deviceName, const std::string& message) {
    auto it = devices.find(deviceName);
    if (it == devices.end()) {
        std::cerr << "Device not found: " << deviceName << std::endl;
        return false;
    }
    return sendToFCM(it->second.deviceId, message);
}

// Sends a notification to all devices in a specific group
bool NotificationManager::sendToGroup(const std::string& groupName, const std::string& message) {
    auto it = groups.find(groupName);
    if (it == groups.end()) {
        std::cerr << "Group not found: " << groupName << std::endl;
        return false;
    }

    bool success = true;
    for (const auto& deviceId : it->second) {
        success &= sendToFCM(deviceId, message);
    }
    return success;
}

// Helper function to send a POST request to FCM
bool NotificationManager::sendToFCM(const std::string& deviceId, const std::string& message) {
    const std::string fcmUrl = "https://fcm.googleapis.com/fcm/send";
    const std::string serverKey = "YOUR_SERVER_KEY"; // Replace with your actual FCM server key

    json payload = {
        {"to", deviceId},
        {"notification", {{"title", "Notification"}, {"body", message}}}
    };

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL" << std::endl;
        return false;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: key=" + serverKey).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, fcmUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.dump().c_str());

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "CURL request failed: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_cleanup(curl);
    return true;
}

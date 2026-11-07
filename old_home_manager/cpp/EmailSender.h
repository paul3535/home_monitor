#ifndef EMAIL_SENDER_H
#define EMAIL_SENDER_H
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <curl/curl.h>
#include "nlohmann/json.hpp"
using json = nlohmann::json;

class EmailSender {
public:
    EmailSender(const std::string& smtp_server, int smtp_port,
                const std::string& username, const std::string& password)
        : smtp_server(smtp_server), smtp_port(smtp_port), username(username), password(password) {}

    static bool sendFromJsonFile(const std::string& jsonFilePath);
    static bool sendFromJson(const json& config);
    
    void setSender(const std::string& sender_email) {
        sender = sender_email;
    }

    void addRecipient(const std::string& recipient) {
        to_recipients.push_back(recipient);
    }

    void addCc(const std::string& cc) {
        cc_recipients.push_back(cc);
    }

    void addBcc(const std::string& bcc) {
        bcc_recipients.push_back(bcc);
    }

    void setSubject(const std::string& subject_text) {
        subject = subject_text;
    }

    void setBody(const std::string& body_text) {
        body = body_text;
    }

    void addAttachment(const std::string& file_path) {
        attachments.push_back(file_path);
    }

    bool sendEmail();

private:
    std::string smtp_server;
    int smtp_port;
    std::string username;
    std::string password;
    std::string sender;
    std::vector<std::string> to_recipients;
    std::vector<std::string> cc_recipients;
    std::vector<std::string> bcc_recipients;
    std::string subject;
    std::string body;
    std::vector<std::string> attachments;
};

#endif
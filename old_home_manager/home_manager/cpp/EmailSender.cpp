#include "EmailSender.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "Logger.h"
using json = nlohmann::json;

bool EmailSender::sendFromJsonFile(const std::string& jsonFilePath) {
    std::ifstream file(jsonFilePath);
    if (!file.is_open()) {
        Logger::log( Logger::ERROR, "EmailSender::sendFromJson: can't open email file: " + jsonFilePath);
        return false;
    }

    json config;
    file >> config;
    return EmailSender::sendFromJson(config);
}

bool EmailSender::sendFromJson(const json& config) {
    Logger::log( Logger::ERROR, "EmailSender::sendFromJson: Going to open email file: " + config.dump());
    /*
  "smtp_server": "smtp.gmail.com",
  "smtp_port": 587,
  "access_token": "your_oauth2_access_token",
  "sender": "your_email@gmail.com",
  CREDENTIALS_DIR = os.getenv('GMAIL_CREDENTIALS_DIR', '/home/pi/credentials')
CREDENTIALS_PATH = os.path.join(CREDENTIALS_DIR, 'credentials.json')
TOKEN_PATH = os.path.join(CREDENTIALS_DIR, 'token.json')
*/
    EmailSender emailSender(
        "smtp.gmail.com",
        587,
        config["username"].get<std::string>(),
        config["password"].get<std::string>()
    );

    emailSender.setSender(config["sender"].get<std::string>());
    for (const auto& email : config["recipients"].get<std::vector<std::string>>()) {
        emailSender.addRecipient(email);
    }
    for (const auto& email : config.value("cc", std::vector<std::string>())) {
        emailSender.addCc(email);
    }
    for (const auto& email : config.value("bcc", std::vector<std::string>())) {
        emailSender.addBcc(email);
    }
    for (const auto& attachment : config.value("attachments", std::vector<std::string>())) {
        emailSender.addAttachment(attachment);
    }
    emailSender.setSubject(config["subject"].get<std::string>());
    emailSender.setBody(config["body"].get<std::string>());

    return emailSender.sendEmail();
}

bool EmailSender::sendEmail() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        Logger::log( Logger::ERROR, "EmailSender::sendEmail: Failed to initialize libcurl");
        return false;
    }

    struct curl_slist* recipients = nullptr;

    // Add all recipients
    for (const auto& to : to_recipients) {
        recipients = curl_slist_append(recipients, to.c_str());
    }
    for (const auto& cc : cc_recipients) {
        recipients = curl_slist_append(recipients, cc.c_str());
    }
    for (const auto& bcc : bcc_recipients) {
        recipients = curl_slist_append(recipients, bcc.c_str());
    }

    // Create MIME structure
    curl_mime* mime = curl_mime_init(curl);
    curl_mimepart* part;

    // Add the email body
    part = curl_mime_addpart(mime);
    curl_mime_data(part, body.c_str(), CURL_ZERO_TERMINATED);
    curl_mime_type(part, "text/plain");

    // Add attachments
    for (const auto& file_path : attachments) {
        part = curl_mime_addpart(mime);
        curl_mime_filedata(part, file_path.c_str());
    }

    // Set email headers
    curl_easy_setopt(curl, CURLOPT_URL, smtp_server.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, sender.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

    // SMTP authentication
    curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
    curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);

    CURLcode res = curl_easy_perform(curl);

    // Cleanup
    curl_slist_free_all(recipients);
    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        Logger::log( Logger::ERROR, "EmailSender::sendEmail: Failed to send email "+ std::string(curl_easy_strerror(res)));
        return false;
    }

    Logger::log( Logger::INFO, "EmailSender::sendEmail: Email sent successfully! ");
    return true;
}

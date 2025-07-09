#include "Message/EMailMessage.h"
#include <curl/curl.h>

void Send() {
    /*CURL *curl;
    CURLcode res;

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "smtp://smtp.example.com");
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, "<from@example.com>");
        
        struct curl_slist *recipients = NULL;
        recipients = curl_slist_append(recipients, "<to@example.com>");
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
        
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, NULL);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        
        const char *message = "Subject: Test Email\r\n\r\nThis is a test email.";
        curl_easy_setopt(curl, CURLOPT_READDATA, message);
        
        res = curl_easy_perform(curl);
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
    }*/
}
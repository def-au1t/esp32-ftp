#include "SD.h"
#include <WiFi.h>

class FTPServer {

  private:
    String ftp_username;
    String ftp_password;
    WiFiServer ftpDataServer;
    WiFiServer ftpCommandServer;

  
  public:
    void begin(String username ){}
};

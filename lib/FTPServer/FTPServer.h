#include "SD.h"
#include <WiFi.h>

enum CommandStatus
{
  RESET = 0,
  WAIT_CONNECTION = 1,
  IDLE = 2,
  WAIT_USERNAME = 3,
  WAIT_PASSWORD = 4,
  WAIT_COMMAND = 5,
};

enum TransferStatus
{
  NO_TRANSFER = 0,
  RETRIEVE = 1,
  STORE = 2,
};

#define FTP_BUF_SIZE 8192

unsigned int FTP_TIMEOUT = 5 * 60 * 1000;

class FTPServer
{

private:
  String ftpUsername;
  String ftpPassword;

  int ftpDataPort;

  WiFiServer ftpDataServer;
  WiFiServer ftpCommandServer;

  WiFiClient ftpDataClient;
  WiFiClient ftpCommandClient;

  CommandStatus status;
  TransferStatus transfer;

  unsigned long connectTimeoutTime;
  unsigned long transactionBeginTime;

  String currentDir;
  File currentFile;

  String lastUserCommand;
  String lastUserParams;

  char buf[FTP_BUF_SIZE];
  unsigned long bytesTransfered;

  uint16_t iCL;

public:
  void begin(String username, String password, int dataPort)
  {
    this->ftpUsername = username;
    this->ftpPassword = password;
    this->ftpDataPort = dataPort;

    this->ftpDataServer = WiFiServer(dataPort);
    this->ftpCommandServer = WiFiServer(21);

    this->ftpDataServer.begin();
    this->ftpCommandServer.begin();

    this->currentDir = "/";

    this->status = RESET;
    this->transfer = NO_TRANSFER;
  }

  void configVariables()
  {
  }

  void listenCommands()
  {

    if (this->ftpCommandServer.hasClient())
    {
      Serial.println("has-client");
      this->ftpCommandClient.stop();
      this->ftpCommandClient = this->ftpCommandServer.available();
    }

    switch (this->status)
    {
    case RESET:
    {
      Serial.println("RESET");
      if (this->ftpCommandClient.connected())
      {
        this->disconnectClient();
      }
      this->status = WAIT_CONNECTION;
      break;
    }

    case WAIT_CONNECTION:
    {
      this->abortTransfer();
      Serial.println("Ftp server waiting for connection on port 21");
      this->currentDir = "/";
      this->status = IDLE;
      break;
    }
    case IDLE:
    {
      if (this->ftpCommandClient.connected())
      {
        this->handleClientConnect();
        this->connectTimeoutTime = millis() + 10 * 1000;
        this->status = WAIT_USERNAME;
      }
      break;
    }
    case WAIT_COMMAND:
    case WAIT_PASSWORD:
    case WAIT_USERNAME:
      if (this->isNewClientCommand())
      {
        switch (this->status)
        {
        case WAIT_USERNAME:
          Serial.println("WAIT_USERNAME");

          if (this->rejectEncryption())
          {
            return;
          }

          if (this->handleClientUsername())
          {
            this->status = WAIT_PASSWORD;
          }
          else
          {
            this->status = RESET;
            delay(100);
          }
          return;
        case WAIT_PASSWORD:
          Serial.println("WAIT_PASSWORD");
          if (this->handleClientPassword())
          {
            this->status = WAIT_COMMAND;
            this->connectTimeoutTime = millis() + 10 * 1000;
          }
          else
          {
            this->status = RESET;
          }
          return;

        case WAIT_COMMAND:
          Serial.println("WAIT_COMMAND");
          if (!this->processCommand(this->lastUserCommand, this->lastUserParams))
          {
            this->status = RESET;
            return;
          }
          else
          {
            this->connectTimeoutTime = millis() + FTP_TIMEOUT;
          }
        }
      }
      else if (!this->ftpCommandClient.connected() || !this->ftpCommandClient)
      {
        this->status = WAIT_CONNECTION;
        Serial.println("WAIT_DISCONNECTED");
      }
    }

    if (this->transfer == RETRIEVE)
    {
      if (!this->dataSend())
        this->transfer = NO_TRANSFER;
    }
    else if (this->transfer == STORE)
    {
      if (!this->dataReceive())
        this->transfer = NO_TRANSFER;
    }

    else if (this->status > IDLE && millis() > this->connectTimeoutTime)
    {
      this->ftpCommandClient.println("530 Timeout");
      delay(200);
      this->status = RESET;
    }
  }

  void handleClientConnect()
  {
    Serial.println("Client connected!");
    this->ftpCommandClient.println("220--- FTP SERVER FOR ESP32 ---");
    this->ftpCommandClient.println("220--- BY Jacek Nitychoruk & Karol Musur ---");
    this->ftpCommandClient.println("220 -- VERSION 0.1 --");
    this->iCL = 0;
  }

  void disconnectClient()
  {
    Serial.println("Disconnecting client");
    this->abortTransfer();
    this->ftpCommandClient.println("221 Goodbye");
    this->ftpCommandClient.stop();
  }

  // Return 530 on AUTH command, to indicate that we do not support encrypted connection
  // Returns true if user is trying to use encryption, and will try again with different protocol
  // Returns false if program can continue execution
  boolean rejectEncryption()
  {
    if (String(this->lastUserCommand) == "AUTH")
    {
      this->ftpCommandClient.println("530 Please login with USER and PASS.");
      return true;
    }
    else
    {
      return false;
    }
  }

  boolean handleClientUsername()
  {
    Serial.println("HANDLING_USERNAME");
    if (String(this->lastUserCommand) != "USER")
    {
      Serial.println(this->lastUserCommand);
      this->ftpCommandClient.println("500 Syntax error");
      return false;
    }
    if (this->ftpUsername != String(this->lastUserParams))
    {
      Serial.println(this->lastUserCommand);
      this->ftpCommandClient.println("530 user not found");
      return false;
    }
    else
    {
      this->ftpCommandClient.println("331 OK. Password required");
      return true;
    }
  }

  boolean handleClientPassword()
  {
    if (String(this->lastUserCommand) != "PASS")
    {
      Serial.println(this->lastUserCommand);
      this->ftpCommandClient.println("500 Syntax error");
      return false;
    }
    if (this->ftpPassword != String(this->lastUserParams))
    {
      this->ftpCommandClient.println("530 ");
      return false;
    }
    else
    {
      this->ftpCommandClient.println("230 OK.");
      this->currentDir = "/";
      return true;
    }
  }

  boolean processCommand(String command, String params)
  {
    if (command == "PWD")
    {
      Serial.println("Current dir: " + this->currentDir);
      this->ftpCommandClient.println("257 \"" + currentDir + "\" is your current directory");
      return true;
    }
    else if (command == "CDUP")
    {
      return cd("..");
    }
    else if (command == "CWD")
    {
      return cd(params);
    }
    else if (command == "FEAT")
    {
      this->ftpCommandClient.println("211-Extensions suported:");
      this->ftpCommandClient.println(" MLSD");
      this->ftpCommandClient.println("211 End.");
      return true;
    }

    else if (command == "TYPE")
    {
      if (params == "A")
        this->ftpCommandClient.println("200 TYPE is now ASII");
      else if (params == "I")
        this->ftpCommandClient.println("200 TYPE is now 8-bit binary");
      else
        this->ftpCommandClient.println("504 Unknown TYPE");
      return true;
    }

    else if (command == "PASV")
    {
      if (this->ftpDataClient.connected())
      {
        this->ftpDataClient.stop();
      }
      IPAddress dataIp = WiFi.localIP();
      Serial.println("Connection management set to passive");
      Serial.println("Data port set to " + String(this->ftpDataPort));
      this->ftpCommandClient.println("227 Entering Passive Mode (" + String(dataIp[0]) + "," + String(dataIp[1]) + "," + String(dataIp[2]) + "," + String(dataIp[3]) + "," + String(this->ftpDataPort >> 8) + "," + String(this->ftpDataPort & 255) + ").");
      return true;
    }

    else if (command == "MLSD" || command == "LIST")
    {
      if (!this->dataConnect())
      {
        this->ftpCommandClient.println("425 No data connection MLSD");
        return false;
      }
      else
      {
        this->ftpCommandClient.println("150 Accepted data connection");
        uint16_t nm = 0;
        //      Dir dir= SD.openDir(cwdName);
        File dir = SD.open(this->currentDir);
        // char dtStr[15];
        //  if(!SD.exists(cwdName))
        if ((!dir) || (!dir.isDirectory()))
          this->ftpCommandClient.println("550 Cannot open directory " + this->currentDir);
        //        client.println( "550 Can't open directory " +String(parameters) );
        else
        {
          //        while( dir.next())
          File file = dir.openNextFile();
          //        while( dir.openNextFile())
          while (file)
          {
            String fileName, fileSize;
            fileName = file.name();
            int sep = fileName.lastIndexOf('/');
            fileName = fileName.substring(sep + 1);
            fileSize = String(file.size());
            if (file.isDirectory())
            {
              if (command == "MLSD")
              {
                this->ftpDataClient.println("Type=dir;Size=" + fileSize + ";" + "modify=20200101000000;" + " " + fileName);
              }
              else
              {
                this->ftpDataClient.println("01-01-2000  00:00AM <DIR> " + fileName);
              }
            }
            else
            {
              if (command == "MLSD")
              {
                this->ftpDataClient.println("Type=file;Size=" + fileSize + ";" + "modify=20200101000000;" + " " + fileName);
              }
              else
              {
                this->ftpDataClient.println("01-01-2000  00:00AM " + fileSize + " " + fileName);
              }
            }
            nm++;
            file = dir.openNextFile();
          }

          if (command == "MLSD")
          {
            this->ftpCommandClient.println("226-options: -a -l");
          }
          this->ftpCommandClient.println("226 " + String(nm) + " matches total");
        }
        this->ftpDataClient.stop();
      }
      return true;
    }
    else if (command == "DELE")
    {
      if (params == "")
      {
        this->ftpCommandClient.println("501 No file name");
        return false;
      }

      String filePath = getFullPath(params);

      if (!SD.exists(filePath))
      {
        this->ftpCommandClient.println("550 File " + filePath + " not found");
        return false;
      }

      if (SD.remove(filePath))
      {
        this->ftpCommandClient.println("250 Deleted " + filePath);
        return true;
      }
      else
      {
        this->ftpCommandClient.println("450 Can't delete " + filePath);
        return false;
      }
    }

    //  RETR - Retrieve
    //
    else if (command == "RETR")
    {
      if (params.length() == 0)
      {
        this->ftpCommandClient.println("501 No file name");
        return false;
      }

      String filePath = getFullPath(params);

      this->currentFile = SD.open(filePath, "r");
      if (!this->currentFile)
        this->ftpCommandClient.println("550 File " + params + " not found");
      else if (!this->currentFile)
        this->ftpCommandClient.println("450 Cannot open " + params);
      else if (!this->dataConnect())
        this->ftpCommandClient.println("425 No data connection");
      else
      {
        Serial.println("Sending " + params);
        this->ftpCommandClient.println("150-Connected to port " + String(this->ftpDataPort));
        this->ftpCommandClient.println("150 " + String(this->currentFile.size()) + " bytes to download");
        this->transactionBeginTime = millis();
        this->bytesTransfered = 0;
        this->transfer = RETRIEVE;
      }
      return true;
    }
    else if (command == "SYST")
    {
      this->ftpCommandClient.println("215 ESP32");
    }
    else
    {
      this->ftpCommandClient.println("500 Syntax error, command unrecognized.");
      return true;
    }
    return true;
  }

  boolean dataConnect()
  {
    unsigned long startTime = millis();
    if (!this->ftpDataClient.connected())
    {
      while (!this->ftpDataServer.hasClient() && millis() - startTime < 10000)
      {
        //delay(100);
        yield();
      }
      if (this->ftpDataServer.hasClient())
      {
        this->ftpDataClient.stop();
        this->ftpDataClient = this->ftpDataServer.available();
        Serial.println("FTP data client connected");
      }
    }

    return this->ftpDataClient.connected();
  }

  boolean dataSend()
  {
    size_t numberBytesRead = this->currentFile.readBytes(buf, FTP_BUF_SIZE);
    // Serial.println("Buffer: " + String(buf));
    // Serial.println("----: ");
    // Serial.println("numberBytesRead: " + String(numberBytesRead));
    if (numberBytesRead > 0)
    {
      ftpDataClient.write((char *)buf, numberBytesRead);
      bytesTransfered += numberBytesRead;
      return true;
    }
    else
    {
      Serial.println("Transfer closed");
      this->closeTransfer();
      return false;
    }
  }

  boolean dataReceive()
  {
    if (ftpDataClient.connected())
    {
      size_t numberBytesRead = ftpDataClient.readBytes((char *)buf, FTP_BUF_SIZE);
      if (numberBytesRead > 0)
      {
        this->currentFile.write((uint8_t *)buf, numberBytesRead);
        bytesTransfered += numberBytesRead;
      }
      return true;
    }
    else
    {
      this->closeTransfer();
      return false;
    }
  }

  void abortTransfer()
  {
    if (this->transfer != NO_TRANSFER)
    {
      this->currentFile.close();
      this->ftpDataClient.stop();
      this->ftpCommandClient.println("426 Transfer aborted");
      Serial.println("Transfer aborted!");
    }
    this->transfer = NO_TRANSFER;
  }

  // TODO:  Something is certainly wrong here:
  void closeTransfer()
  {
    uint32_t deltaT = (millis() - this->transactionBeginTime);
    Serial.println("Transfer close");
    Serial.println("bytesTransfered" + String(this->bytesTransfered));
    if (deltaT > 0 && this->bytesTransfered > 0)
    {

      this->ftpCommandClient.println("226-File successfully transferred");

      this->ftpCommandClient.println("226 " + String(deltaT) + " ms, " + String(bytesTransfered / deltaT) + " kbytes/s");
    }
    else
      this->ftpCommandClient.println("226 File successfully transferred");

    this->currentFile.close();
    this->ftpDataClient.stop();
  }

  boolean isNewClientCommand()
  {
    int c;
    String commandBuffer = "";
    String command = "";
    String params = "";
    while ((c = this->ftpCommandClient.read()) != -1)
    {
      commandBuffer.concat(String((char)c));
    }
    if (commandBuffer == "")
    {
      return false;
    }
    else
    {
      int spaceIndex = -1;
      if ((spaceIndex = commandBuffer.indexOf(' ')) != -1)
      {
        command = commandBuffer.substring(0, spaceIndex);
        params = commandBuffer.substring(spaceIndex + 1);
        params.trim();
      }
      else
      {
        command = commandBuffer;
        command.trim();
        params = "";
      }

      this->lastUserCommand = command;
      this->lastUserParams = params;
      Serial.print("commandBuffer: ");
      Serial.println(String(commandBuffer));
      Serial.print("command: ");
      Serial.println(String(command));
      Serial.print("params: ");
      Serial.println(params);
      return true;
    }
  }

  boolean cd(String path)
  {
    Serial.println("Old dir: " + this->currentDir);
    if (path == ".")
      return processCommand("PWD", "");
    else if (path == "..")
    {
      String newPath;
      if (this->currentDir == "/")
      {
        newPath == "/";
      }
      int sep = this->currentDir.lastIndexOf('/');
      newPath = this->currentDir.substring(0, sep);
      if (newPath == "")
      {
        newPath = "/";
      }
      this->currentDir = newPath;
    }
    else if (path == "/")
    {
      this->currentDir = "/";
    }
    else
    {
      if (path.charAt(0) == '/')
      {
        this->currentDir = path;
      }
      else
      {
        if (this->currentDir == "/")
        {
          this->currentDir.concat(path);
        }
        else
        {
          this->currentDir.concat("/" + path);
        }
      }
    }

    Serial.println("New dir: " + this->currentDir);
    this->ftpCommandClient.println("250 Ok. Directory changed to " + this->currentDir);
    return true;
  }

  String getFullPath(String relativePath)
  {
    String filePath;
    if (currentDir == "/")
    {
      filePath = "/" + relativePath;
    }
    else
    {
      filePath = this->currentDir + "/" + relativePath;
    }
    return filePath;
  }
};

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

#define FTP_BUF_SIZE 4096

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
  String fileToRename;
  File currentFile;

  String filePath;

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
    this->filePath = "";

    this->status = RESET;
    this->transfer = NO_TRANSFER;
  }

  void configVariables()
  {
  }

  void mainFTPLoop()
  {
    // Serial.println("mainFTPLoop: Current state is " + String(this->status));

    // Continue transfer if exists
    if (this->transfer != NO_TRANSFER)
    {
      this->processTransfer();
      return;
    }

    // New client appeared
    if (this->ftpCommandServer.hasClient())
    {
      Serial.println("New client");
      this->ftpCommandClient.stop();
      this->ftpCommandClient = this->ftpCommandServer.available();
    }

    // Client timeout - disconnect
    else if (this->status > IDLE && millis() > this->connectTimeoutTime)
    {
      this->ftpCommandClient.println("530 Timeout");
      vTaskDelay(200);
      this->status = RESET;
      return;
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
    case WAIT_USERNAME:
    {
      if (this->isNewClientCommand())
      {
        Serial.println("WAIT_USERNAME");

        if (this->encryptionRejected())
        {
          Serial.println("encryptionRejected");
          return;
        }

        if (this->handleClientUsername())
        {
          this->status = WAIT_PASSWORD;
        }
        else
        {
          this->status = RESET;
          vTaskDelay(100);
        }
        return;
      }
      else if (!this->ftpCommandClient.connected() || !this->ftpCommandClient)
      {
        this->status = WAIT_CONNECTION;
        Serial.println("WAIT_DISCONNECTED");
      }
      break;
    }
    case WAIT_PASSWORD:
    {
      if (this->isNewClientCommand())
      {
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
      }
      else if (!this->ftpCommandClient.connected() || !this->ftpCommandClient)
      {
        this->status = WAIT_CONNECTION;
        Serial.println("WAIT_DISCONNECTED");
      }
      break;
    }
    case WAIT_COMMAND:
    {
      if (this->isNewClientCommand())
      {
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
      else if (!this->ftpCommandClient.connected() || !this->ftpCommandClient)
      {
        this->status = WAIT_CONNECTION;
        Serial.println("WAIT_DISCONNECTED");
      }
    }
    }
  }

  void processTransfer()
  {
    if (this->transfer == RETRIEVE)
    {
      if (!this->dataSend())
        this->transfer = NO_TRANSFER;
      return;
    }
    else if (this->transfer == STORE)
    {
      if (!this->dataReceive())
        this->transfer = NO_TRANSFER;
      return;
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
  // Returns true if user is trying to use encryption, and should try again with different protocol
  // Returns false if program can continue execution
  boolean encryptionRejected()
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
    else if (command == "NOOP")
    {
      this->ftpCommandClient.println("200 NOOP");
      return true;
    }
    else if (command == "QUIT")
    {
      return false;
    }
    else if (command == "ABOR")
    {
      Serial.println("ABORting");
      this->abortTransfer();
      this->ftpCommandClient.println("226 Data connection closed");

      return true;
    }
    else if (command == "MODE")
    {
      if (params == "S")
      {
        this->ftpCommandClient.println("200 OK");
      }
      else
      {
        this->ftpCommandClient.println("504 Only Stream is supported");
      }
      return true;
    }
    else if (command == "STRU")
    {
      if (params == "F")
      {
        this->ftpCommandClient.println("200 OK");
      }
      else
      {
        this->ftpCommandClient.println("504 Only File is supported");
      }
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
    else if (command == "MKD")
    {
      if (params == "")
      {
        this->ftpCommandClient.println("501 No file name given");
        return false;
      }

      String dirname = getFullPath(params);

      if (SD.exists(dirname))
      {
        this->ftpCommandClient.println("553 Directory " + dirname + " already exists");
        return true;
      }

      if (SD.mkdir(dirname))
      {
        this->ftpCommandClient.println("275 Directory successfully created");
        return true;
      }
      else
      {
        this->ftpCommandClient.println("550 MKD failed");
        return true;
      }
    }
    else if (command == "TYPE")
    {
      if (params == "A")
        this->ftpCommandClient.println("200 TYPE is now ASCII");
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
        File dir = SD.open(this->currentDir);
        if ((!dir) || (!dir.isDirectory()))
          this->ftpCommandClient.println("550 Cannot open directory " + this->currentDir);
        else
        {

          File file = dir.openNextFile();
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
                this->ftpDataClient.println("Type=dir;Size=" + fileSize + ";" + "modify=20210101000000;" + " " + fileName);
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
                this->ftpDataClient.println("Type=file;Size=" + fileSize + ";" + "modify=20210101000000;" + " " + fileName);
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
    else if (command == "DELE" || command == "RMD")
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

      if (command == "DELE" ? SD.remove(filePath) : SD.rmdir(filePath))
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

    else if (command == "RNFR")
    {
      if (params == "")
      {
        this->ftpCommandClient.println("501 No file name");
        return false;
      }

      fileToRename = getFullPath(params);

      if (!SD.exists(fileToRename))
      {
        this->ftpCommandClient.println("550 File " + fileToRename + " not found");
        return false;
      }

      Serial.println("Renaming " + fileToRename);
      this->ftpCommandClient.println("350 RNFR accepted");
    }

    else if (command == "RNTO")
    {
      if (params == "")
      {
        this->ftpCommandClient.println("501 No file name given");
        return false;
      }

      if (fileToRename == "")
      {
        this->ftpCommandClient.println("501 No file name set by RNFR");
        return false;
      }

      String newFileName = getFullPath(params);

      if (SD.exists(newFileName))
      {
        this->ftpCommandClient.println("553 File " + fileToRename + " already exists");
        return false;
      }

      if (SD.rename(fileToRename, newFileName))
      {
        this->ftpCommandClient.println("250 File successfully renamed or moved");
        Serial.println("Renaming " + fileToRename);
        fileToRename = "";
        return true;
      }
      else
      {
        this->ftpCommandClient.println("451 Rename failed");
        fileToRename = "";
        return false;
      }
    }

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
    else if (command == "STOR")
    {
      if (params == "")
      {
        this->ftpCommandClient.println("501 No file name given");
        return false;
      }

      String filePath = getFullPath(params);

      this->filePath = filePath;
      this->currentFile = SD.open(filePath, "w");

      if (!this->currentFile)
      {
        this->ftpCommandClient.println("451 Can't create or open " + filePath);
        return true;
      }

      if (!this->dataConnect())
      {
        this->ftpCommandClient.println("425 No data connection");
        this->currentFile.close();
        return true;
      }

      Serial.println("Receiving " + params);
      this->ftpCommandClient.println("150 Connected to port " + String(this->ftpDataPort));
      this->transactionBeginTime = millis();
      this->bytesTransfered = 0;
      this->transfer = STORE;

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
    if (numberBytesRead > 0 && ftpDataClient.connected())
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
    size_t numberBytesRead = ftpDataClient.readBytes((uint8_t *)buf, FTP_BUF_SIZE);
    if (numberBytesRead > 0)
    {
      size_t position = this->currentFile.position();
      size_t written = this->currentFile.write((uint8_t *)buf, numberBytesRead);
      while (written == 0)
      {
        this->currentFile.close();
        this->currentFile = SD.open(this->filePath, "w");
        this->currentFile.seek(position);
        written = this->currentFile.write((uint8_t *)buf, numberBytesRead);
      }
      bytesTransfered += numberBytesRead;
      return true;
    }
    else
    {
      if (!this->ftpDataClient.connected())
      {
        this->closeTransfer();
        return false;
      }
      else
      {
        return true;
      }
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

  void closeTransfer()
  {
    uint32_t deltaT = (millis() - this->transactionBeginTime);
    Serial.println("Transfer close");
    Serial.println("bytesTransfered: " + String(this->bytesTransfered));
    if (deltaT > 0 && this->bytesTransfered > 0)
    {
      this->ftpCommandClient.println("226-File successfully transferred");

      this->ftpCommandClient.println("226 " + String(deltaT) + " ms, " + String(bytesTransfered / deltaT) + " kbytes/s");
    }
    else
    {
      this->ftpCommandClient.println("226 File successfully transferred");
    }

    this->currentFile.flush();
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

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

// TODO: Remove it:
#define FTP_FIL_SIZE 255  // max size of a file name
#define FTP_BUF_SIZE 1024 //512   // size of file buffer for read/write
#define FTP_CMD_SIZE 255 + 8 // max size of a command
#define FTP_CWD_SIZE 255 + 8 // max size of a directory name

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
  unsigned long nextListenTime;

  String currentDir;
  File currentFile;

  // String userCommand;
  // String userParams;

  // TODO: Change it to dynamic:
  char* parameters;
  char buf[FTP_BUF_SIZE];
  unsigned long bytesTransfered;
  char     cmdLine[ FTP_CMD_SIZE ];   // where to store incoming char from client
  char     cwdName[ FTP_CWD_SIZE ];   // name of current directory
  char     userCommand[ 5 ];

  uint16_t iCL;

public:
  void begin(String username, String password, int dataPort)
  {
    this->ftpUsername = username;
    this->ftpPassword = password;
    this->ftpDataPort = dataPort;

    this->ftpDataServer.begin();
    this->ftpCommandServer.begin();

    this->currentDir = "/";
    // this->currentFile = NULL;

    this->nextListenTime = 0;
    this->status = RESET;
    this->transfer = NO_TRANSFER;
  }

  void configVariables()
  {
  }

  void listenCommands()
  {
    if (nextListenTime > millis())
    {
      return;
    }

    if (this->ftpCommandServer.hasClient())
    {
      this->ftpCommandClient.stop();
      this->ftpCommandClient = this->ftpCommandServer.available();
    }

    switch (this->status)
    {
    case RESET:
    {
      if (this->ftpCommandClient.connected())
      {
        this->disconnectClient();
      }
      this->status = WAIT_CONNECTION;
    }

    case WAIT_CONNECTION: // Ftp server waiting for connection
    {
      this->abortTransfer();
      Serial.println("Ftp server waiting for connection on port 21");
      this->currentDir = "/";
      this->status = IDLE;
    }
    case IDLE: // Ftp server idle
    {

      if (this->ftpCommandClient.connected()) // A client connected
      {
        this->handleClientConnect();
        this->connectTimeoutTime = millis() + 10 * 1000; // wait client id during 10 s.
        this->status = WAIT_USERNAME;
      }
    }
    default:
      if (this->readChar() > 0) // got response
      {
        switch (this->status)
        {
        case WAIT_USERNAME:
          // Ftp server waiting for user identity
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
          // Ftp server waiting for user identity
          if (this->handleClientPassword())
          {
            this->status = WAIT_COMMAND;
            this->connectTimeoutTime = millis() + FTP_TIMEOUT;
          }
          else
          {
            this->status = RESET;
          }
          return;

        case WAIT_COMMAND:
          // Ftp server waiting for user command
          if (!this->processCommand())
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
        Serial.println("client disconnected");
      }
    }

    if (this->transfer == RETRIEVE) // Retrieve data
    {
      if (!this->dataSend())
        this->transfer = NO_TRANSFER;
    }
    else if (this->transfer == STORE) // Store data
    {
      if (!this->dataReceive())
        this->transfer = NO_TRANSFER;
    }
    else if (this->status > IDLE && millis() > this->connectTimeoutTime)
    {
      this->ftpCommandClient.println("530 Timeout");
      this->nextListenTime = millis() + 200; // delay of 200 ms
      this->status = RESET;
    }
  }

  void handleClientConnect()
  {
    Serial.println("Client connected!");
    this->ftpCommandClient.println("220--- FTP SERVER FOR ESP32 ---");
    this->ftpCommandClient.println("220--- BY Jacek Nitychoruk & Karol Musur ---");
    this->iCL = 0;
  }

  void disconnectClient()
  {
    Serial.println("Disconnecting client");
    this->abortTransfer();
    this->ftpCommandClient.println("221 Goodbye");
    this->ftpCommandClient.stop();
  }

  boolean handleClientUsername()
  {
    if (String(this->userCommand) != "USER")
    {
      this->ftpCommandClient.println("500 Syntax error");
    }
    if (this->ftpUsername != String(this->parameters))
    {
      this->ftpCommandClient.println("530 user not found");
      return false;
    }
    else
    {
      this->ftpCommandClient.println("331 OK. Password required");
      this->currentDir = "/";
      return true;
    }
  }

  boolean handleClientPassword()
  {
    if (String(this->userCommand) != "PASS")
    {
      this->ftpCommandClient.println("500 Syntax error");
    }
    if (this->ftpUsername != String(this->parameters))
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

  boolean processCommand()
  {
  }

  boolean dataConnect()
  {
  }


  //TODO Improve:
  boolean dataSend()
  {
    //int16_t numberBytesRead = this->currentFile.readBytes((uint8_t*) buf, FTP_BUF_SIZE );
    int16_t numberBytesRead = this->currentFile.readBytes(buf, FTP_BUF_SIZE);
    if (numberBytesRead > 0)
    {
      ftpDataClient.write((uint8_t *)buf, numberBytesRead);
      bytesTransfered += numberBytesRead;
      return true;
    }
    else
    {
      this->closeTransfer();
      return false;
    }
  }

  boolean dataReceive()
  {
    if (ftpDataClient.connected())
    {
      int16_t numberBytesRead = ftpDataClient.readBytes((uint8_t *)buf, FTP_BUF_SIZE);
      if (numberBytesRead > 0)
      {
        // Serial.println( millis() << " " << nb << endl;
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
    uint32_t deltaT = (int32_t)(millis() - this->transactionBeginTime);
    if (deltaT > 0 && bytesTransfered > 0)
    {
      this->ftpCommandClient.println("226-File successfully transferred");
      this->ftpCommandClient.println("226 " + String(deltaT) + " ms, " + String(bytesTransfered / deltaT) + " kbytes/s");
    }
    else
      this->ftpCommandClient.println("226 File successfully transferred");

    this->currentFile.close();
    this->ftpDataClient.stop();
  }


// TODO: Make it readable:
int8_t readChar()
{
  int8_t rc = -1;

  if( this->ftpCommandClient.available())
  {
    char c = this->ftpCommandClient.read();
	 // char c;
	 // client.readBytes((uint8_t*) c, 1);
    Serial.print( c);
    if( c == '\\' )
      c = '/';
    if( c != '\r' )
      if( c != '\n' )
      {
        if( iCL < FTP_CMD_SIZE )
          cmdLine[ iCL ++ ] = c;
        else
          rc = -2; //  Line too long
      }
      else
      {
        cmdLine[ iCL ] = 0;
        userCommand[ 0 ] = 0;
        parameters = NULL;
        // empty line?
        if( iCL == 0 )
          rc = 0;
        else
        {
          rc = iCL;
          // search for space between command and parameters
          parameters = strchr( cmdLine, ' ' );
          if( parameters != NULL )
          {
            if( parameters - cmdLine > 4 )
              rc = -2; // Syntax error
            else
            {
              strncpy( userCommand, cmdLine, parameters - cmdLine );
              userCommand[ parameters - cmdLine ] = 0;
              
              while( * ( ++ parameters ) == ' ' )
                ;
            }
          }
          else if( strlen( cmdLine ) > 4 )
            rc = -2; // Syntax error.
          else
            strcpy( userCommand, cmdLine );
          iCL = 0;
        }
      }
    if( rc > 0 )
      for( uint8_t i = 0 ; i < strlen( userCommand ); i ++ )
        userCommand[ i ] = toupper( userCommand[ i ] );
    if( rc == -2 )
    {
      iCL = 0;
      this->ftpCommandClient.println( "500 Syntax error");
    }
  }
  return rc;


}

};

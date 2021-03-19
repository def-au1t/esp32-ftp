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
#define FTP_FIL_SIZE 255     // max size of a file name
#define FTP_BUF_SIZE 1024    //512   // size of file buffer for read/write
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

  String currentDir;
  File currentFile;

  String lastUserCommand;
  String lastUserParams;

  // TODO: Change it to dynamic:
  // char* parameters;
  char buf[FTP_BUF_SIZE];
  unsigned long bytesTransfered;
  // char     cmdLine[ FTP_CMD_SIZE ];   // where to store incoming char from client
  // char     cwdName[ FTP_CWD_SIZE ];   // name of current directory
  // char     userCommand[ 5 ];

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
    // this->currentFile = NULL;

    this->status = RESET;
    this->transfer = NO_TRANSFER;
  }

  void configVariables()
  {
  }

  void listenCommands()
  {
    delay(500);

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

    case WAIT_CONNECTION: // Ftp server waiting for connection
    {
      this->abortTransfer();
      Serial.println("Ftp server waiting for connection on port 21");
      this->currentDir = "/";
      this->status = IDLE;
      break;
    }
    case IDLE: // Ftp server idle
    {
      Serial.println("IDLE");
      if (this->ftpCommandClient.connected()) // A client connected
      {
        this->handleClientConnect();
        this->connectTimeoutTime = millis() + 10 * 1000; // wait client id during 10 s.
        this->status = WAIT_USERNAME;
      }
      break;
    }
    case WAIT_COMMAND:
    case WAIT_PASSWORD:
    case WAIT_USERNAME:
      if (this->isNewClientCommand()) // got response
      {
        switch (this->status)
        {
        case WAIT_USERNAME:
          Serial.println("WAIT_USERNAME");
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
          Serial.println("WAIT_PASSWORD");
          // Ftp server waiting for user identity
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
          // Ftp server waiting for user command
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
        // Serial.println("client disconnected");
      }
    }

    if (this->transfer == RETRIEVE) // Retrieve data
    {
      if (!this->dataReceive())
        this->transfer = NO_TRANSFER;
    }
    else if (this->transfer == STORE) // Store data
    {
      if (!this->dataSend())
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
    if (this->ftpUsername != String(this->lastUserParams))
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
      this->ftpCommandClient.println("257 \"" + currentDir + "\" is your current directory");
      return true;
    }
    else if (command == "CWD")
    {
      if (params == ".") // 'CWD .' is the same as PWD command
        return processCommand("PWD", "");
      else
      {
        this->currentDir.concat(params);
        this->ftpCommandClient.println("250 Ok. Directory changed to " + this->currentDir);
      }
    }
    else if (command == "TYPE")
    {
      if (params == "A")
        this->ftpCommandClient.println("200 TYPE is now ASII");
      else if (params == "I")
        this->ftpCommandClient.println("200 TYPE is now 8-bit binary");
      else
        this->ftpCommandClient.println("504 Unknown TYPE");
    }

    else if (command == "PASV")
    {
      if (this->ftpDataClient.connected())
      {
        this->ftpDataClient.stop();
      }
      //dataServer.begin();
      //dataIp = Ethernet.localIP();
      IPAddress dataIp = WiFi.localIP();
      //data.connect( dataIp, dataPort );
      //data = dataServer.available();
      Serial.println("Connection management set to passive");
      Serial.println("Data port set to " + String(this->ftpDataPort));
      this->ftpCommandClient.println("227 Entering Passive Mode (" + String(dataIp[0]) + "," + String(dataIp[1]) + "," + String(dataIp[2]) + "," + String(dataIp[3]) + "," + String(this->ftpDataPort >> 8) + "," + String(this->ftpDataPort & 255) + ").");
    }

    else if (command == "MLSD")
    {
      if (!this->dataConnect()){
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
            fileName.remove(0, 1);
            fileSize = String(file.size());
            if (file.isDirectory())
            {
              this->ftpDataClient.println("Type=dir;Size=" + fileSize + ";" + "modify=20200101000000;" + " " + fileName);
              //            data.println( "Type=dir;modify=20000101000000; " + fn);
            }
            else
            {
              //data.println( "Type=file;Size=" + fs + ";"+"modify=20000101160656;" +" " + fn);
              this->ftpDataClient.println("Type=file;Size=" + fileSize + ";" + "modify=20200101000000;" + " " + fileName);
            }
            nm++;
            file = dir.openNextFile();
          }
          this->ftpCommandClient.println("226-options: -a -l");
          this->ftpCommandClient.println("226 " + String(nm) + " matches total");
        }
        this->ftpDataClient.stop();
      }
      return true;
    }

    else
    {
      this->ftpCommandClient.println("500 Syntax error");
      return false;
    }
  }
  //      ///////////////////////////////////////
  //   //                                   //
  //   //      ACCESS CONTROL COMMANDS      //
  //   //                                   //
  //   ///////////////////////////////////////

  //   //
  //   //  CDUP - Change to Parent Directory
  //   //
  //   if( ! strcmp( command, "CDUP" ))
  //   {
  // 	  client.println("250 Ok. Current directory is " + String(cwdName));
  //   }
  //   //
  //   //  CWD - Change Working Directory
  //   //
  //   else if( ! strcmp( command, "CWD" ))
  //   {
  //     char path[ FTP_CWD_SIZE ];
  //     if( strcmp( parameters, "." ) == 0 )  // 'CWD .' is the same as PWD command
  //       client.println( "257 \"" + String(cwdName) + "\" is your current directory");
  //     else
  //       {
  //         client.println( "250 Ok. Current directory is " + String(cwdName) );
  //       }

  //   }
  //   //
  //   //  PWD - Print Directory
  //   //
  //   else if( ! strcmp( command, "PWD" ))
  //     client.println( "257 \"" + String(cwdName) + "\" is your current directory");
  //   //
  //   //  QUIT
  //   //
  //   else if( ! strcmp( command, "QUIT" ))
  //   {
  //     disconnectClient();
  //     return false;
  //   }

  //   ///////////////////////////////////////
  //   //                                   //
  //   //    TRANSFER PARAMETER COMMANDS    //
  //   //                                   //
  //   ///////////////////////////////////////

  //   //
  //   //  MODE - Transfer Mode
  //   //
  //   else if( ! strcmp( command, "MODE" ))
  //   {
  //     if( ! strcmp( parameters, "S" ))
  //       client.println( "200 S Ok");
  //     // else if( ! strcmp( parameters, "B" ))
  //     //  client.println( "200 B Ok\r\n";
  //     else
  //       client.println( "504 Only S(tream) is suported");
  //   }
  //   //
  //   //  PASV - Passive Connection management
  //   //
  //   else if( ! strcmp( command, "PASV" ))
  //   {
  //     if (data.connected()) data.stop();
  //     //dataServer.begin();
  //      //dataIp = Ethernet.localIP();
  // 	dataIp = WiFi.localIP();
  // 	dataPort = FTP_DATA_PORT_PASV;
  //     //data.connect( dataIp, dataPort );
  //     //data = dataServer.available();
  //     #ifdef FTP_DEBUG
  // 	Serial.println("Connection management set to passive");
  //       Serial.println( "Data port set to " + String(dataPort));
  //     #endif
  //    client.println( "227 Entering Passive Mode ("+ String(dataIp[0]) + "," + String(dataIp[1])+","+ String(dataIp[2])+","+ String(dataIp[3])+","+String( dataPort >> 8 ) +","+String ( dataPort & 255 )+").");
  //    dataPassiveConn = true;
  //   }
  //   //
  //   //  PORT - Data Port
  //   //
  //   else if( ! strcmp( command, "PORT" ))
  //   {
  // 	if (data) data.stop();
  //     // get IP of data client
  //     dataIp[ 0 ] = atoi( parameters );
  //     char * p = strchr( parameters, ',' );
  //     for( uint8_t i = 1; i < 4; i ++ )
  //     {
  //       dataIp[ i ] = atoi( ++ p );
  //       p = strchr( p, ',' );
  //     }
  //     // get port of data client
  //     dataPort = 256 * atoi( ++ p );
  //     p = strchr( p, ',' );
  //     dataPort += atoi( ++ p );
  //     if( p == NULL )
  //       client.println( "501 Can't interpret parameters");
  //     else
  //     {

  // 		client.println("200 PORT command successful");
  //       dataPassiveConn = false;
  //     }
  //   }
  //   //
  //   //  STRU - File Structure
  //   //
  //   else if( ! strcmp( command, "STRU" ))
  //   {
  //     if( ! strcmp( parameters, "F" ))
  //       client.println( "200 F Ok");
  //     // else if( ! strcmp( parameters, "R" ))
  //     //  client.println( "200 B Ok\r\n";
  //     else
  //       client.println( "504 Only F(ile) is suported");
  //   }
  //   //
  //   //  TYPE - Data Type
  //   //
  //   else if( ! strcmp( command, "TYPE" ))
  //   {
  //     if( ! strcmp( parameters, "A" ))
  //       client.println( "200 TYPE is now ASII");
  //     else if( ! strcmp( parameters, "I" ))
  //       client.println( "200 TYPE is now 8-bit binary");
  //     else
  //       client.println( "504 Unknow TYPE");
  //   }

  //   ///////////////////////////////////////
  //   //                                   //
  //   //        FTP SERVICE COMMANDS       //
  //   //                                   //
  //   ///////////////////////////////////////

  //   //
  //   //  ABOR - Abort
  //   //
  //   else if( ! strcmp( command, "ABOR" ))
  //   {
  //     abortTransfer();
  //     client.println( "226 Data connection closed");
  //   }
  //   //
  //   //  DELE - Delete a File
  //   //
  //   else if( ! strcmp( command, "DELE" ))
  //   {
  //     char path[ FTP_CWD_SIZE ];
  //     if( strlen( parameters ) == 0 )
  //       client.println( "501 No file name");
  //     else if( makePath( path ))
  //     {
  //       if( ! SD.exists( path ))
  //         client.println( "550 File " + String(parameters) + " not found");
  //       else
  //       {
  //         if( SD.remove( path ))
  //           client.println( "250 Deleted " + String(parameters) );
  //         else
  //           client.println( "450 Can't delete " + String(parameters));
  //       }
  //     }
  //   }
  //   //
  //   //  LIST - List
  //   //
  //   else if( ! strcmp( command, "LIST" ))
  //   {
  //     if( ! dataConnect())
  //       client.println( "425 No data connection");
  //     else
  //     {
  //       client.println( "150 Accepted data connection");
  //       uint16_t nm = 0;
  // //      Dir dir=SD.openDir(cwdName);
  //       File dir=SD.open(cwdName);
  // //      if( !SD.exists(cwdName))
  //      if((!dir)||(!dir.isDirectory()))
  //         client.println( "550 Can't open directory " + String(cwdName) );
  //       else
  //       {
  //         File file = dir.openNextFile();
  //         while( file)
  //         {
  //     			String fn, fs;
  //           fn = file.name();
  //     			fn.remove(0, 1);
  //       		#ifdef FTP_DEBUG
  //   			  Serial.println("File Name = "+ fn);
  //       		#endif
  //           fs = String(file.size());
  //           if(file.isDirectory()){
  //             data.println( "01-01-2000  00:00AM <DIR> " + fn);
  //           } else {
  //             data.println( "01-01-2000  00:00AM " + fs + " " + fn);
  // //          data.println( " " + fn );
  //           }
  //           nm ++;
  //           file = dir.openNextFile();
  //         }
  //         client.println( "226 " + String(nm) + " matches total");
  //       }
  //       data.stop();
  //     }
  //   }
  //   //
  //   //  MLSD - Listing for Machine Processing (see RFC 3659)
  //   //
  //   else if( ! strcmp( command, "MLSD" ))
  //   {
  //     if( ! dataConnect())
  //       client.println( "425 No data connection MLSD");
  //     else
  //     {
  // 	  client.println( "150 Accepted data connection");
  //       uint16_t nm = 0;
  // //      Dir dir= SD.openDir(cwdName);
  //       File dir= SD.open(cwdName);
  //       char dtStr[ 15 ];
  //     //  if(!SD.exists(cwdName))
  //      if((!dir)||(!dir.isDirectory()))
  //         client.println( "550 Can't open directory " +String(cwdName) );
  // //        client.println( "550 Can't open directory " +String(parameters) );
  //       else
  //       {
  // //        while( dir.next())
  //         File file = dir.openNextFile();
  // //        while( dir.openNextFile())
  //         while( file)
  //     		{
  //     			String fn,fs;
  //           fn = file.name();
  //     			fn.remove(0, 1);
  //           fs = String(file.size());
  //           if(file.isDirectory()){
  //             data.println( "Type=dir;Size=" + fs + ";"+"modify=20000101000000;" +" " + fn);
  // //            data.println( "Type=dir;modify=20000101000000; " + fn);
  //           } else {
  //             //data.println( "Type=file;Size=" + fs + ";"+"modify=20000101160656;" +" " + fn);
  //             data.println( "Type=file;Size=" + fs + ";"+"modify=20000101000000;" +" " + fn);
  //           }
  //           nm ++;
  //           file = dir.openNextFile();
  //         }
  //         client.println( "226-options: -a -l");
  //         client.println( "226 " + String(nm) + " matches total");
  //       }
  //       data.stop();
  //     }
  //   }
  //   //
  //   //  NLST - Name List
  //   //
  //   else if( ! strcmp( command, "NLST" ))
  //   {
  //     if( ! dataConnect())
  //       client.println( "425 No data connection");
  //     else
  //     {
  //       client.println( "150 Accepted data connection");
  //       uint16_t nm = 0;
  // //      Dir dir=SD.openDir(cwdName);
  //       File dir= SD.open(cwdName);
  //       if( !SD.exists( cwdName ))
  //         client.println( "550 Can't open directory " + String(parameters));
  //       else
  //       {
  //           File file = dir.openNextFile();
  // //        while( dir.next())
  //         while( file)
  //         {
  // //          data.println( dir.fileName());
  //           data.println( file.name());
  //           nm ++;
  //           file = dir.openNextFile();
  //         }
  //         client.println( "226 " + String(nm) + " matches total");
  //       }
  //       data.stop();
  //     }
  //   }
  //   //
  //   //  NOOP
  //   //
  //   else if( ! strcmp( command, "NOOP" ))
  //   {
  //     // dataPort = 0;
  //     client.println( "200 Zzz...");
  //   }
  //   //
  //   //  RETR - Retrieve
  //   //
  //   else if( ! strcmp( command, "RETR" ))
  //   {
  //     char path[ FTP_CWD_SIZE ];
  //     if( strlen( parameters ) == 0 )
  //       client.println( "501 No file name");
  //     else if( makePath( path ))
  // 	{
  // 		file = SD.open(path, "r");
  //       if( !file)
  //         client.println( "550 File " +String(parameters)+ " not found");
  //       else if( !file )
  //         client.println( "450 Can't open " +String(parameters));
  //       else if( ! dataConnect())
  //         client.println( "425 No data connection");
  //       else
  //       {
  //         #ifdef FTP_DEBUG
  // 		  Serial.println("Sending " + String(parameters));
  //         #endif
  //         client.println( "150-Connected to port "+ String(dataPort));
  //         client.println( "150 " + String(file.size()) + " bytes to download");
  //         millisBeginTrans = millis();
  //         bytesTransfered = 0;
  //         transferStatus = 1;
  //       }
  //     }
  //   }
  //   //
  //   //  STOR - Store
  //   //
  //   else if( ! strcmp( command, "STOR" ))
  //   {
  //     char path[ FTP_CWD_SIZE ];
  //     if( strlen( parameters ) == 0 )
  //       client.println( "501 No file name");
  //     else if( makePath( path ))
  //     {
  // 		file = SD.open(path, "w");
  //       if( !file)
  //         client.println( "451 Can't open/create " +String(parameters) );
  //       else if( ! dataConnect())
  //       {
  //         client.println( "425 No data connection");
  //         file.close();
  //       }
  //       else
  //       {
  //         #ifdef FTP_DEBUG
  //           Serial.println( "Receiving " +String(parameters));
  //         #endif
  //         client.println( "150 Connected to port " + String(dataPort));
  //         millisBeginTrans = millis();
  //         bytesTransfered = 0;
  //         transferStatus = 2;
  //       }
  //     }
  //   }
  //   //
  //   //  MKD - Make Directory
  //   //
  //   else if( ! strcmp( command, "MKD" ))
  //   {
  // 	  client.println( "550 Can't create \"" + String(parameters));  //not support on espyet
  //   }
  //   //
  //   //  RMD - Remove a Directory
  //   //
  //   else if( ! strcmp( command, "RMD" ))
  //   {
  // 	  client.println( "501 Can't delete \"" +String(parameters));

  //   }
  //   //
  //   //  RNFR - Rename From
  //   //
  //   else if( ! strcmp( command, "RNFR" ))
  //   {
  //     buf[ 0 ] = 0;
  //     if( strlen( parameters ) == 0 )
  //       client.println( "501 No file name");
  //     else if( makePath( buf ))
  //     {
  //       if( ! SD.exists( buf ))
  //         client.println( "550 File " +String(parameters)+ " not found");
  //       else
  //       {
  //         #ifdef FTP_DEBUG
  // 		  Serial.println("Renaming " + String(buf));
  //         #endif
  //         client.println( "350 RNFR accepted - file exists, ready for destination");
  //         rnfrCmd = true;
  //       }
  //     }
  //   }
  //   //
  //   //  RNTO - Rename To
  //   //
  //   else if( ! strcmp( command, "RNTO" ))
  //   {
  //     char path[ FTP_CWD_SIZE ];
  //     char dir[ FTP_FIL_SIZE ];
  //     if( strlen( buf ) == 0 || ! rnfrCmd )
  //       client.println( "503 Need RNFR before RNTO");
  //     else if( strlen( parameters ) == 0 )
  //       client.println( "501 No file name");
  //     else if( makePath( path ))
  //     {
  //       if( SD.exists( path ))
  //         client.println( "553 " +String(parameters)+ " already exists");
  //       else
  //       {
  //             #ifdef FTP_DEBUG
  // 		  Serial.println("Renaming " + String(buf) + " to " + String(path));
  //             #endif
  //             if( SD.rename( buf, path ))
  //               client.println( "250 File successfully renamed or moved");
  //             else
  // 				client.println( "451 Rename/move failure");

  //       }
  //     }
  //     rnfrCmd = false;
  //   }

  //   ///////////////////////////////////////
  //   //                                   //
  //   //   EXTENSIONS COMMANDS (RFC 3659)  //
  //   //                                   //
  //   ///////////////////////////////////////

  //   //
  //   //  FEAT - New Features
  //   //
  //   else if( ! strcmp( command, "FEAT" ))
  //   {
  //     client.println( "211-Extensions suported:");
  //     client.println( " MLSD");
  //     client.println( "211 End.");
  //   }
  //   //
  //   //  MDTM - File Modification Time (see RFC 3659)
  //   //
  //   else if (!strcmp(command, "MDTM"))
  //   {
  // 	  client.println("550 Unable to retrieve time");
  //   }

  //   //
  //   //  SIZE - Size of the file
  //   //
  //   else if( ! strcmp( command, "SIZE" ))
  //   {
  //     char path[ FTP_CWD_SIZE ];
  //     if( strlen( parameters ) == 0 )
  //       client.println( "501 No file name");
  //     else if( makePath( path ))
  // 	{
  // 		file = SD.open(path, "r");
  //       if(!file)
  //          client.println( "450 Can't open " +String(parameters) );
  //       else
  //       {
  //         client.println( "213 " + String(file.size()));
  //         file.close();
  //       }
  //     }
  //   }
  //   //
  //   //  SITE - System command
  //   //
  //   else if( ! strcmp( command, "SITE" ))
  //   {
  //       client.println( "500 Unknow SITE command " +String(parameters) );
  //   }
  //   //
  //   //  Unrecognized commands ...
  //   //
  //   else
  //     client.println( "500 Unknow command");

  //   return true;
  // }

  boolean dataConnect()
  {
    unsigned long startTime = millis();
    //wait 5 seconds for a data connection
    if (!this->ftpDataClient.connected())
    {
      while (!this->ftpDataServer.hasClient() && millis() - startTime < 10000)
      //    while (!dataServer.available() && millis() - startTime < 10000)
      {
        //delay(100);
        yield();
      }
      if (this->ftpDataServer.hasClient())
      {
        //    if (dataServer.available()) {
        this->ftpDataClient.stop();
        this->ftpDataClient = this->ftpDataServer.available();
        Serial.println("FTP data client connected");
      }
    }

    return this->ftpDataClient.connected();
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
      if ((spaceIndex = commandBuffer.lastIndexOf(' ')) != -1)
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

  // TODO: Make it readable:
  // int8_t readChar()
  // {
  //   int8_t rc = -1;

  //   if( this->ftpCommandClient.available())
  //   {
  //     char c = this->ftpCommandClient.read();
  // 	 // char c;
  // 	 // client.readBytes((uint8_t*) c, 1);
  //     Serial.print( c);
  //     if( c == '\\' )
  //       c = '/';
  //     if( c != '\r' ){
  //       if( c != '\n' )
  //       {
  //         if( iCL < FTP_CMD_SIZE )
  //           cmdLine[ iCL ++ ] = c;
  //         else
  //           rc = -2; //  Line too long
  //       }
  //       else
  //       {
  //         cmdLine[ iCL ] = 0;
  //         userCommand[ 0 ] = 0;
  //         parameters = NULL;
  //         // empty line?
  //         if( iCL == 0 )
  //           rc = 0;
  //         else
  //         {
  //           rc = iCL;
  //           // search for space between command and parameters
  //           parameters = strchr( cmdLine, ' ' );
  //           if( parameters != NULL )
  //           {
  //             if( parameters - cmdLine > 4 )
  //               rc = -2; // Syntax error
  //             else
  //             {
  //               strncpy( userCommand, cmdLine, parameters - cmdLine );
  //               userCommand[ parameters - cmdLine ] = 0;

  //               while( * ( ++ parameters ) == ' ' )
  //                 ;
  //             }
  //           }
  //           else if( strlen( cmdLine ) > 4 )
  //             rc = -2; // Syntax error.
  //           else
  //             strcpy( userCommand, cmdLine );
  //           iCL = 0;
  //         }
  //       }

  //     }
  //     if( rc > 0 )
  //       for( uint8_t i = 0 ; i < strlen( userCommand ); i ++ )
  //         userCommand[ i ] = toupper( userCommand[ i ] );
  //     if( rc == -2 )
  //     {
  //       iCL = 0;
  //       this->ftpCommandClient.println( "500 Syntax error");
  //     }
  //   }
  //   Serial.print("ReadChar: ");
  //   Serial.println(String(rc));
  //   Serial.print("cmdLine: ");
  //   Serial.println(String(cmdLine));
  //   Serial.print("userCommand: ");
  //   Serial.println(String(this->userCommand));
  //   Serial.print("parameters: ");
  //   Serial.println(String(this->parameters));
  //   return rc;

  // }
};

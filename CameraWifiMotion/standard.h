/**************************************************************************************************
 *
 *      Standard procedures - 04Mar21 (not standard)
 *      
 *      part of the BasicWebserver sketch - https://github.com/alanesq/BasicWebserver
 *      
 *             
 **************************************************************************************************/


// forward declarations
  void log_system_message(String);
  void webheader();
  void webfooter();
  void handleLogpage();
  void handleNotFound();
  void handleReboot();
  void WIFIcheck();
  String decodeIP(String);
  

// ----------------------------------------------------------------
//                    -decode IP addresses
// ----------------------------------------------------------------
// Check for known IP addresses 

String decodeIP(String IPadrs) {
    if (IPadrs == "192.168.1.176") return "HA server";
    if (IPadrs == "192.168.1.103") return "Parlour laptop";
    if (IPadrs == "192.168.1.101") return "Bedroom laptop";
    if (IPadrs == "192.168.1.169") return "Linda's laptop";
    if (IPadrs == "192.168.1.170") return "Shed 1 laptop";
    if (IPadrs == "192.168.1.143") return "Shed 2 laptop";
    return IPadrs;
}


// ----------------------------------------------------------------
//                              -Startup
// ----------------------------------------------------------------
  
// html text colour codes (obsolete html now and should use CSS instead)
  const char colRed[] = "<font color='#FF0000'>";           // red text
  const char colGreen[] = "<font color='#006F00'>";         // green text
  const char colBlue[] = "<font color='#0000FF'>";          // blue text
  const char colEnd[] = "</font>";                          // end coloured text

String system_message[LogNumber + 1];                       // system log message store


// ----------------------------------------------------------------
//                      -log a system message  
// ----------------------------------------------------------------

void log_system_message(String smes) {

  //scroll old log entries up 
    for (int i=0; i < LogNumber; i++){
      system_message[i]=system_message[i+1];
    }

  // add the new message to the end
    system_message[LogNumber] = currentTime() + " - " + smes;
  
  // also send message to serial port
    if (serialDebug) Serial.println("Log:" + system_message[LogNumber]);
}


// ----------------------------------------------------------------
//                         -header (html) 
// ----------------------------------------------------------------
// HTML at the top of each web page
//    additional style settings can be included and auto page refresh rate


void webheader(WiFiClient &client, char style[] = " ", int refresh = 0) {

      client.write("<!DOCTYPE html>\n");
      client.write("<html lang='en'>\n");
      client.write(   "<head>\n");
      client.write(     "<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n");
      client.write(     "<link rel=\'icon\' href=\'data:,\'>\n");
      client.printf(    "<title> %s </title>\n", stitle);               // insert sketch title
      client.write(     "<style>\n");                                   // Settings here for the top of screen menu appearance 
      client.write(       "ul {list-style-type: none; margin: 0; padding: 0; overflow: hidden; background-color: rgb(128, 64, 0);}\n");
      client.write(       "li {float: left;}\n");
      client.write(       "li a {display: inline-block; color: white; text-align: center; padding: 30px 20px; text-decoration: none;}\n");
      client.write(       "li a:hover { background-color: rgb(100, 0, 0);}\n" );
      client.printf(      "%s\n", style);                              // insert aditional style if supplied 
      client.write(     "</style>\n");
      client.write(   "</head>\n");
      client.write(  "<body style='color: rgb(0, 0, 0); background-color: yellow; text-align: center;'>\n");
      client.write(     "<ul>\n");             
      client.write(       "<li><a href='/'>Home</a></li>\n");                       /* home menu button */
      client.write(       "<li><a href='/log'>Log</a></li>\n");                     /* log menu button */
      client.write(       "<li><a href='/bootlog'>BootLog</a></li>\n");             /* boot log menu button */
      client.write(       "<li><a href='/stream'>Live Video</a></li>\n");           /* stream live video */
      client.write(       "<li><a href='/images'>Stored Images</a></li>\n");        /* last menu button */
      client.write(       "<li><a href='/live'>Capture Image</a></li>\n");          /* capture image menu button */
      client.write(       "<li><a href='/imagedata'>Raw Data</a></li>\n");          /* raw data menu button */
      client.printf(      "<h1> %s %s %s </h1>\n", colRed, stitle, colEnd);         /* display the project title in red */
      client.write(     "</ul>\n");
}


// ----------------------------------------------------------------
//                             -footer (html)
// ----------------------------------------------------------------
// HTML at the end of each web page

void webfooter(WiFiClient &client) {

   // get mac address
     byte mac[6];
     WiFi.macAddress(mac);
   
   client.print("<br>\n"); 
   
   /* Status display at bottom of screen */
   client.print("<div style='text-align: center;background-color:rgb(128, 64, 0)'>\n");
   client.printf("<small> %s", colRed); 
   client.printf("%s %s", stitle, sversion); 
   client.printf(" | Memory: %dK", ESP.getFreeHeap() /1000); 
   client.printf(" | Wifi: %ddBm", WiFi.RSSI()); 
   // NTP server link status
    int tstat = timeStatus();   // ntp status
    if (tstat == timeSet) client.print(" | NTP OK");
    else if (tstat == timeNeedsSync) client.print(" | NTP Sync failed");
    else if (tstat == timeNotSet) client.print(" | NTP Failed");
   // client.printf(" | Spiffs: %dK", ( SPIFFS.totalBytes() - SPIFFS.usedBytes() / 1000 ) );             // if using spiffs 
   // client.printf(" | MAC: %2x%2x%2x%2x%2x%2x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);       // mac address
   client.printf("%s </small>\n", colEnd);
   client.print("</div>\n"); 
     
  /* end of HTML */  
   client.print("</body>\n");
   client.print("</html>\n");

}


// ----------------------------------------------------------------
//   -log web page requested    i.e. http://x.x.x.x/log
// ----------------------------------------------------------------

void handleLogpage() {

  WiFiClient client = server.client();                     // open link with client

// log page request including clients IP address
  IPAddress cip = client.remoteIP();
  String clientIP = String(cip[0]) +"." + String(cip[1]) + "." + String(cip[2]) + "." + String(cip[3]);
  clientIP = decodeIP(clientIP);               // check for known IP addresses
  log_system_message("Log page requested from: " + clientIP);  
      

    // build the html for /log page

      webheader(client);                    // send html page header  

      client.print("<P>\n");                // start of section
  
      client.print("<br>SYSTEM LOG<br><br>\n");
  
      // list all system messages
      for (int i=LogNumber; i != 0; i--){
        client.print(system_message[i].c_str());
        if (i == LogNumber) {
          client.printf("%s  {Most Recent Entry} %s", colRed, colEnd);          // build line of html
        }
        client.print("<br>\n");    // new line
      }
    
      client.print("<br>");
    
      // close html page
        webfooter(client);                          // send html page footer
        delay(3);
        client.stop();

}


// ----------------------------------------------------------------
//                      -invalid web page requested
// ----------------------------------------------------------------

void handleNotFound() {
  
  log_system_message("invalid web page requested");      
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }

  server.send ( 404, "text/plain", message );
  message = "";      // clear variable
  
}


// ----------------------------------------------------------------
//   -reboot web page requested        i.e. http://x.x.x.x/reboot  
// ----------------------------------------------------------------
//
//     note: this fails if the esp has just been reflashed and not restarted


void handleReboot(){

      String message = "Rebooting....";
      server.send(404, "text/plain", message);   // send reply as plain text

      // rebooting
        delay(500);          // give time to send the above html
        ESP.restart();   
        delay(5000);         // restart fails without this line

}


// --------------------------------------------------------------------------------------
//                                -wifi connection check
// --------------------------------------------------------------------------------------

void WIFIcheck() {
  
    if (WiFi.status() != WL_CONNECTED) {
      if ( wifiok == 1) {
        log_system_message("Wifi connection lost");          // log system message if wifi was ok but now down
        wifiok = 0;                                          // flag problem with wifi
      }
    } else { 
      // wifi is ok
      if ( wifiok == 0) {
        log_system_message("Wifi connection is back");       // log system message if wifi was down but now back
        wifiok = 1;                                          // flag wifi is now ok
      }
    }

}


// --------------------------- E N D -----------------------------

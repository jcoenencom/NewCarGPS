//#include <Arduino.h>
#include <WiFi.h>
//#include <WiFiClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include "ESPTelnet.h"
#include <ArduinoJson.h>


#define LED 2
#define SerialAT  Serial1
#define UART_BAUD   115200
#define PIN_DTR     25
#define PIN_TX      27
#define PIN_RX      26
#define PWR_PIN     4


// GPS varialbe
bool BGps = false;   // flag to determine GPS validity

// WiFi related
const char* ssid = "Malperthuis";
const char* password = "9725145239910203";
AsyncWebServer server(80);

//GPRS network
const char apn[]      = "mworld.be";
const char gprsUser[] = "";
const char gprsPass[] = "";


/* --- Telnet related --- */
ESPTelnet telnet;
IPAddress ip;
uint16_t  port = 23;

// GPS json

JsonDocument gps;

JsonDocument gpsHA;

String gpsindex[11]={"lat","lati","long","longi", "date","UTC","alt","speed","course","timer","NULL"};
String gnssindex[17]={"mode","GPS","GLONASS","BEIDU","lat","lati","long","longi","date","UTC","alt","speed","course","PDOP","HDOP","VDOP","NULL"};
char output[256];
String deviceSN = "+CGSN";
// utilities
void prompt(void) {
  telnet.print(deviceSN+">");
}

/* ------------------- WiFi activation and starting OTA/Telnet servers --- */

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  ip = WiFi.localIP(); // setup ip for telent initialization
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "OTA server GPS Modem on /update");
  });


  ElegantOTA.begin(&server);

  server.begin();
  Serial.println("HTTP server started");
  Serial.print("- Telnet setup: "); Serial.print(ip); Serial.print(":"); Serial.println(port);
  setupTelnet();

}


// serial interaction

bool send_modem(String & cmd) {
/* write a command to the serial modem line
The command is of the form +xxx an AT will be prepended before issuing
The reply is checked for ERROR -> returns false
If no error the command argument variable is replaced by the reply
*/

//telnet.println("send_modem cmd in:"+cmd);
  String temp;
  String r;
  if (cmd.startsWith("+")) {
    temp = "AT"+cmd;
  } else {
    temp=cmd;
  }
  //telnet.println("send_modem:"+temp);
  SerialAT.println(temp);   // send AT+ command
  r = SerialAT.readString(); // read reply 
  if (r.indexOf("ERROR") >= 0) {
    cmd = r;    // set the cmd to the error replied
    return false;
  }
  // remove AT+xxx command from reply
  r.remove(0, r.indexOf("\r\n")+2);
  //get the CMD: result, follows :\b in repsonse
  if (temp.indexOf("CMGR") >=0) {
    // incomming SMS readout, need to return info in first line and text in second line
    temp = r.substring(r.indexOf(":")+2);
  } else if (temp.indexOf("CGSN") >=0) {
    temp = r.substring(0,r.indexOf("\r"));
  } else if (r.indexOf(":") > 0) {
    temp = r.substring(r.indexOf(":")+2,r.indexOf("\r"));
  } 
  cmd = temp;
  //telnet.println("Sending back in cmd var:"+cmd);
  return true;
}

bool writeAT(String & cmd) {
  telnet.println("writeAT: CMD: "+ cmd);
  SerialAT.println(cmd);
  String r = SerialAT.readString();
  telnet.println("writeAT: reply "+ r);
 
  // remove the command from the reply
  if (r.startsWith(cmd)) {
    r.remove(0, r.indexOf("\r\n")+2);
  }
  if (r.startsWith("ERROR")) {
    return false;
  } else {
    cmd = r;
//    printhex(r);
    return true;
  }

}


// utilities

String getValue(String reading) {
/*
In: returned string from modem read command ie. +CMD: value
Out: Int value as returned
*/

  int index =  reading.indexOf(":");
  String valeur = reading.substring(index+1);
  return valeur;
}


void printhex(String chaine) {
telnet.print(chaine+" == HEX == ");
for (int i=0; i< chaine.length(); i++)
  {
    telnet.print("0x");
    telnet.print(chaine.charAt(i), HEX);
    telnet.print(" ");
  }
  telnet.println();

}


bool isNetworkConnected(void) {
    String response = "+CREG?";
    send_modem(response);
    if (response.endsWith("1") ) {
      return (true);
    }
}

String getSN(void){

  String temp = "+CGSN";
  telnet.println("Curent SN ="+deviceSN);
  if (send_modem(temp)) {
  //  telnet.println("GetSN : reply to +CGSN ="+temp);
    return temp.substring(0,temp.indexOf('\r\n')-1);
  } else {
    return "NO_SERIAL";
  }
}

String getNBR(void) {
  String resp = "+CNUM";
  if (send_modem(resp)) {
    String temp = resp.substring(resp.indexOf(",")+2,resp.lastIndexOf(",")-1);
    return temp;
  }
  return "Number_unknown";
}

String getIP(void) {
  String resp = "+CGPADDR";
  if (send_modem(resp)) {
    String temp = resp.substring(resp.indexOf(",")+1);
    return temp;
  }
  return "Number_unknown";
}

bool isGPRSConnected(void) {

  // check if attached to packet domain
    String response = "+NETOPEN?";
    if (send_modem(response)) {
         if (!response.startsWith("1") ) {
          telnet.println("***"+response+"***");
          return false;
         }
    response = "+IPADDR";
    if ( send_modem(response) ) {
          if (! response.startsWith("1") ) {
          telnet.println("***"+response+"***");
          return false;
         } else {
          return true;
        }
    }
  }
  return false;
}

bool isGPSon(void) {

  String resp = "+CGPS?";
  send_modem(resp);
  if (resp.startsWith("0")) {
    return false;
  } else {
    return true;
  }
}

/*
connection routines

GPRS
GPS
MQTT

*/


void connectToGPRS(void) {
  String resp;
  if (!isGPRSConnected() ){
    resp = "+CGATT=1";
    send_modem(resp);
  }
  // define the PDP Context, IP V4 in position 1
  resp = "+CGDCONT=1,\"IP\",\"dialogbb\"";
  send_modem(resp);
  // Activate PDP context 1
  resp="+CGACT=1,1";
  send_modem(resp);
  // Get the address of PDP conext 1  
  resp="+CGPADDR=1";
  send_modem(resp);
  // activate PDP context 1 to start. TCP/IP socket service
  resp="+NETOPEN";
  send_modem(resp);
}

void connectToGPS(void) {
  String resp;
  if (!isGPSon()) {
    String resp = "+CGPS=1";
    send_modem(resp);
  }
}

void connectToMQTT(void){
  String resp = "+CMQTTSTART";
  send_modem(resp);
  resp = "+CMQTTACCQ=0,\"MQTTGSM test\",0";
  send_modem(resp);
  String command = "+CMQTTCONNECT=0,\"tcp://www.jcoenen.com:8883\",60,1,\"jcoenen\",\"Beatr1ce\"";
  send_modem(command);
  delay(2000);
  subscribe();
}

/* ------------------- Telnet configuration ------------------------------ */

void errorMsg(String error, bool restart = true) {
  Serial.println(error);
  if (restart) {
    Serial.println("Rebooting now...");
    delay(2000);
    ESP.restart();
    delay(2000);
  }
}

/* ------------------ Telnet related --------------------------- */

void setupTelnet() {  
  // passing on functions for various telnet events
  telnet.onConnect(onTelnetConnect);
  telnet.onConnectionAttempt(onTelnetConnectionAttempt);
  telnet.onReconnect(onTelnetReconnect);
  telnet.onDisconnect(onTelnetDisconnect);
  telnet.onInputReceived(onTelnetInput);

  Serial.print("- Telnet: ");
  if (telnet.begin(port)) {
    Serial.println("running");
  } else {
    Serial.println("error.");
    errorMsg("Will reboot...");
  }
}

// (optional) callback functions for telnet events
void onTelnetConnect(String ip) {
  Serial.print("- Telnet: ");
  Serial.print(ip);
  telnet.println(" connected to modem serial <"+deviceSN+">");
  
  telnet.println("\nWelcome " + telnet.getIP());
  telnet.println("(Use ^] + q  to disconnect.)");
  prompt();
}

void onTelnetDisconnect(String ip) {
  Serial.print("- Telnet: ");
  Serial.print(ip);
  Serial.println(" disconnected");
}

void onTelnetReconnect(String ip) {
  Serial.print("- Telnet: ");
  Serial.print(ip);
  Serial.println(" reconnected");
  prompt();
}

void onTelnetConnectionAttempt(String ip) {
  Serial.print("- Telnet: ");
  Serial.print(ip);
  Serial.println(" tried to connected");
}

void onTelnetInput(String str) {
  // checks for a certain command
  if (str == "gps") {
    telnet.println("telnet: calling for gps"); 
    Serial.println("gps");
    getGPS();
    prompt();
    return;
  } else if (str == "gnss") {
    getGNSS();
    serializeJson(gps,telnet);
    telnet.println("");
    prompt();
    return;
  } else if (str == "bye") {
    telnet.println("> disconnecting you...");
    telnet.disconnectClient();
  } if (str == "reset") {
    telnet.println("telnet: Rebooting the system ");
    telnet.disconnectClient();
    ESP.restart();
  } else if (str == "gettime") {
    getnwtime();
  } else if (str == "mqtt") {
    telnet.println("telnet: Calling for location and health data into json sent to mqtt server");
    Serial.println("Calling for location and health data into json sent to mqtt server");
    getGPS();
    getvt();
    mqttsendHA();
    prompt();
  } else if ((str == "mqttHA") or (str == "HA")) {
    telnet.println("telnet: Calling for location and health data into json sent to mqtt server");
    Serial.println("Calling for location and health data into json sent to mqtt server");
    getGNSS();
    getvt();
    mqttsendHA();
    prompt();
  } else if (str.startsWith("AT+")) {
    String Cmd = str;
    if (!writeAT(str)) {
      telnet.println("telnet: Cmd "+Cmd+"- not successfull");
      telnet.println("Modem replied: "+str);
    } else {
//      if (str.startsWith(Cmd)) {
//        str.remove(0, str.indexOf("\n")+1);
//      }
      telnet.println("telnet: modem replied:"+str);
    }
    prompt(); 
  } else if (str.startsWith("+")) {
    String Cmd = str;
    if (!send_modem(str)) {
      telnet.println("telnet: Cmd "+Cmd+"- failed");
      telnet.println("Modem replied: "+str);
    } 
    telnet.println("telnet: modem replied:"+str);
    prompt();
    } else if (str =="status") {
      if ( isNetworkConnected() ) {
        telnet.println("Network connected ");
        } else {
            telnet.println("Network not connected");
        }
      if ( isGPRSConnected() ) {
        telnet.println("GPRS connected ");
      } else {
          telnet.println("GPRS not connected -> connecting");
          connectToGPRS();
      
        telnet.println("Connect MQTT server ");
      
      }
      connectToMQTT();
      telnet.println(getNBR());
      connectToGPS();
      prompt();
  } else if (str == "health") {
    getvt();
    serializeJson(gps,telnet);
  } else if (str == "mqttstatus") {
    bool res = mqttstatus();
    prompt();
  } else if (str == "sub") {
    subscribe();
    prompt();
  } else {
    String Cmd = str;
    telnet.println("telnet: Command "+str+ " not recognized");
    telnet.println("Available commands are:\n\
    gps: one GPS reading\n\
    gnss: One GNSS reading\n\
    gettime: get current time on modem\n\
    bye exit telnet\n\
    reset reboot ESP32\n\
    mqtt get health + GPS send to mqtt\n\
    mqttHA or HA get health, GNSS and send to mqtt from Homeassistant\n\
    AT+<cmd>: send cmd to modem\n\
    +xxx send AT+xxx command via WriteAT\n\
    +<cmd>: sends command via send_modem\n\
    status check networks connections\n\
    sub: subscribe to homeassistant/device_tracker/866442071178294/cmd\n\
    mqttstatus get status and send to mqtt\n");
  } 
}


/* --------------------------------------------------- */

bool mqttstatus(void) {
  JsonDocument resp;
  getvt();
  if (isGPSon()) { 
    resp["GPS"] = 1; 
  } else {
    resp["GPS"] = 0;
  }
  resp["temperature"]=gps["temp"];
  resp["voltage"]=gps["volt"];
  resp["GPSintrvl"]=gps["GPSintrvl"];
  if ( isNetworkConnected() ) {
        resp["network"]="connected";
        } else {
            resp["network"]="not connected";
        }
      if ( isGPRSConnected() ) {
        resp["GPRS"]="connected ";
      } else {
        resp["GPRS"]="connecting";
        connectToGPRS();
        resp["MQTT"]="Connecting to MQTT server ";
        connectToMQTT();
        resp["Number"]=getNBR();
        resp["IPaddr"]=getIP();

      }
      resp["Number"]=getNBR();
      resp["IPaddr"]=getIP();
      connectToGPS();
      if (isGPSon()) {
        resp["GPS"]="ON";
       } else {
        resp["GPS"]="OFF";
       }

  String uplinkTopic = "homeassistant/device_tracker/"+deviceSN+"/status";
  serializeJson(resp,output,256);
  String payld = String(output);
  if (sendmqtt(uplinkTopic, payld)) {
  //  telnet.println("mqttstatus :"+uplinkTopic);
  //  telnet.println("mqttstatus :"+payld);
    return true;
  }
  return false;
}

bool sendmqtt(String topic, String payload)
 {
  String resp;
  resp = "+CMQTTTOPIC=0,"+String(topic.length());
  send_modem(resp);
  send_modem(topic);
  delay(1000);
  resp = "+CMQTTPAYLOAD=0,"+String(payload.length());
  send_modem(resp);
  resp = payload+"\x1A";
  send_modem(resp);
  resp="+CMQTTPUB=0,0,120";
  if (send_modem(resp)){
    telnet.println("Send to mqtt: <"+topic+"> "+payload);
  } else {
    telnet.println("mqtt publish failed :"+resp);
  }
  return true;
 }

void subscribe() {
  String topic = "homeassistant/device_tracker/866442071178294/cmd";
  String res = "+CMQTTSUB=0,"+String(topic.length())+",0";
  send_modem(res);
  send_modem(topic);

}

void mqttsendHA(void) {
/*
In: gps global variable
Out:nothing
Formats mqtt topic and json payload to be sent to modem MQTT I/F
*/

  String resp;
  String uplinkTopic = "homeassistant/device_tracker/"+deviceSN+"/attributes";
  resp = "+CMQTTTOPIC=0,"+String(uplinkTopic.length());
  send_modem(resp);
  send_modem(uplinkTopic);
  if ( (abs(float(gps["lat"]) - float(gpsHA["latitude"])) > 0.0001)  or ( abs( float(gpsHA["longitude"])-float(gps["long"])) > 0.0001 ) or ( abs(float(gpsHA["alt"])-float(gps["alt"]))>1 ) ) {
      gpsHA["latitude"]=gps["lat"];
      gpsHA["longitude"]=gps["long"];
      gpsHA["alt"]=gps["alt"];
      // speed is in knots convert in km/h
      gpsHA["speed"]= String(1.852 * double(gps["speed"]));
      gpsHA["gps_accuracy"]=1;
      gpsHA["course"]=gps["course"];
      gpsHA["date"]=gps["date"];

      serializeJson(gpsHA,output,256);
      String Payload = String(output);
      resp = "+CMQTTPAYLOAD=0,"+String(Payload.length());
      send_modem(resp);
      resp = Payload+"\x1A";
      send_modem(resp);
      resp="+CMQTTPUB=0,0,120";
      if (send_modem(resp)){
        telnet.println("Send to mqtt: <"+uplinkTopic+"> "+Payload);
      } else {
        telnet.println("mqtt publish failed :"+resp);
      }
    delay(2000);
  } else {
    telnet.println("Device has not moved no mqtt update ");
  }
}



void getvt() {
  // get voltage and temperature from the board
  
  String temp = "+CPMUTEMP";
  send_modem(temp);
  gps["temp"]=temp.toInt();
  temp = "+CBC";
  send_modem(temp);
  String volt = temp.substring(0,temp.indexOf('V'));
  gps["volt"]=volt.toFloat();
  temp = "+CGNSSINFO?";
  send_modem(temp);
  telnet.println(temp);
  gps["GPSintrvl"]=temp.toInt();
}

float toDeg(char* x) {
  int y = atoi(x);
  float degre = (float) (y / 100);
//  telnet.println("y "+String(y) + "/100 =" + String(degre));
  float min = (atof(x) - (degre * 100.0)) / 60.0;
  degre = degre + min;
//  telnet.println("deg = "+String(degre)+" min "+String(min));
//  telnet.println("Convert ddmm"+ String(x) + " into degs:"+String(degre));

  return degre;

}

void getGPS( void ) {

  String GPS = "+CGPSINFO";
  //Start GPS if not started
  if ( send_modem(GPS)) {
    parseGPS(GPS);
  } else {
    telnet.println("GetGPS error");
  }
}

void parseGPS(String GPS) {
  char *token;
  const char *delimiter = ",";
  //telnet.println("parseGPS: with "+GPS);
  if (GPS.indexOf(",,,,,,,,") > 0) {
    Serial.println("No GPS info returned");
    telnet.println("No GPS info returned");
    BGps = false;
  } else {
    Serial.println("GPS INFO RCVD:" + GPS);
  //  telnet.println("GPS INFO RCVD:" + GPS);
    BGps=true;
    char * my_argument = const_cast<char*> (GPS.c_str() );
    token = strtok(my_argument,delimiter);
    int i=0;
    while (token != NULL) {
      if ((gpsindex[i] == "lat") or (gpsindex[i] == "long")) {
        float result = atof(token) / 100.0;
        gps[gpsindex[i]]= result;
      } else if ((gpsindex[i] == "alt") or (gpsindex[i] == "course") or (gpsindex[i] == "speed")) {
        gps[gpsindex[i]]=String(token).toFloat();
      } else {
      gps[gpsindex[i]]=token;
      }
    //  Serial.println(gpsindex[i]+ "  " + token);
    //  telnet.println(gpsindex[i]+ " + " + token);
      token=strtok(NULL, delimiter);
      i++;
    }

  }

}

void getGNSS( void ) {

  String GPS = "+CGNSSINFO";
  //Start GPS if not started
  if ( send_modem(GPS)) {
    //telnet.println("getGNSS : "+r);
    parseGNSS(GPS);
  } else {
    telnet.println("GetGNSS error");
  }
}

void parseGNSS(String GPS) {
  char *token;
  const char *delimiter = ",";
  if (GPS.indexOf(",,,,,,,,") > 0) {
    Serial.println("No GNSS info returned");
    telnet.println("No GNSS info returned");
    BGps = false;
  } else {
    Serial.println("GNSS INFO RCVD:" + GPS);
  //  telnet.println("GNSS INFO RCVD:" + GPS);
    char * my_argument = const_cast<char*> (GPS.c_str() );
    token = strtok(my_argument,delimiter);
    int i=0;
    while (token != NULL) {
      if ((gnssindex[i] == "lat") or (gnssindex[i] == "long")) {
        gps[gnssindex[i]]= toDeg(token);
      } else if ((gnssindex[i] == "alt") or (gnssindex[i] == "course") or (gnssindex[i] == "speed") or (gnssindex[i] == "PDOP") or (gnssindex[i] == "HDOP") or (gnssindex[i] == "VDOP")) {
        gps[gnssindex[i]]=String(token).toFloat();
      } else {
      gps[gnssindex[i]]=token;
      }
      //telnet.println(gnssindex[i]+ " + " + String(token) + "gps["+gnssindex[i]+"]="+ String(gps[gnssindex[i]]));
      token=strtok(NULL, delimiter);
      i++;
    }
    BGps = true;
  }
  return;
}


void getnwtime() {
  String Dtime = "+CCLK?";
  send_modem(Dtime);
  String datetime = Dtime.substring(Dtime.indexOf('"')+1, Dtime.indexOf('"',Dtime.indexOf('"')+1));

  gps["date"]=datetime.substring(0,datetime.indexOf(","));
  gps["time"]=datetime.substring(datetime.indexOf(",")+1);
//  telnet.println("parsed Date,Time"+datetime);
//  Serial.println("parsed Date,Time"+datetime);
}


void setup(void) {
  Serial.begin(115200);
// Setting up WiFi
  initWiFi();

  //Turn on the modem
  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, HIGH);
  delay(300);
  digitalWrite(PWR_PIN, LOW);
  delay(1000);
  
  SerialAT.begin(UART_BAUD, SERIAL_8N1, PIN_RX, PIN_TX);
  delay(5000);
  connectToGPRS();
  delay(5000);
  connectToMQTT();
  delay(5000);
  connectToGPS();
  deviceSN=getSN();
}



void loop(void) {
  telnet.loop();
  ElegantOTA.loop();
  // get the modem serial number, will be used as device ID for Homeassisatnt mqtt topic

/* wait for incomming SMS
   if new SMS then
    readdelete it
    if valid command
      execute it
*/

  while (SerialAT.available()>0) {
    String cmd;
    String r = SerialAT.readString();
    //printhex(r);
    telnet.println("loop :"+r);
    if (r.indexOf("+CMTI:")>0) {
/* received an unsollicited SMS
  get the message received
*/
      String SMSem = r.substring(r.indexOf(':')+1, r.indexOf(',')-1);
      String SMSidx = r.substring(r.indexOf(',')+1);
      String SMS = "+CMGRD=" + SMSidx;
      send_modem(SMS);
      telnet.println("loop: SMS: "+SMS);
      String smsinfo = SMS.substring(0, SMS.indexOf('\n'));
      String smstext = SMS.substring(SMS.indexOf('\n')+1);
      
      // parse the message for commands
      // first turn the GPS automatic update ON
      if (smstext.indexOf("GPSon") >=0 ) {
        String timer = smstext.substring(smstext.indexOf("GPSon")+6);
        int time = timer.toInt();
        if (time == 0) {timer = "180"; }
        if (time > 255) { timer ="255"; }
        cmd = "+CGNSSINFO="+timer;
      //  telnet.println("GPSon :"+cmd+"***");
        send_modem(cmd);
        cmd="+CMGD="+SMSidx;
        send_modem(cmd);
      // Turne the automatic update Off
      } else if (SMS.indexOf("GPSoff") >=0 ) {
        cmd="+CGNSSINFO=0";
        send_modem(cmd);
        cmd="+CMGD="+SMSidx;
        send_modem(cmd);
      } else if (SMS.indexOf("GNSS") >=0 ) {
        getGNSS();
        mqttsendHA();
        cmd="+CMGD="+SMSidx;
        send_modem(cmd);
      } else if (SMS.indexOf("status") >=0 ) {
          mqttstatus();
        cmd="+CMGD="+SMSidx;
        send_modem(cmd);
      }
//  Received a GPSINFO update
    } else if (r.indexOf("+CGNSSINFO:")>=0) {
      // got a GNSSINFO message, parse it into gps array and send it to MQTT broker 
      parseGNSS(r.substring(r.indexOf("+CGNSSINFO:")));
      mqttsendHA();
    } else if (r.indexOf("+CMQTTRXSTART:")>=0) {
      // received an mqtt message on topic homeassistant/device_tracker/866442071178294/cmd
      String temp = r.substring(r.indexOf('\n',r.indexOf("+CMQTTRXTOPIC:"))+1);
      String topic = temp.substring(0,temp.indexOf('\n')-1);
      //telnet.println("RX topic:"+topic);
      temp = r.substring(r.indexOf('\n',r.indexOf("+CMQTTRXPAYLOAD:"))+1);
      String payload = temp.substring(0, temp.indexOf('\n')-1);
      telnet.println("RX payload:"+payload);
      if (topic.indexOf("cmd") >= 0) {
        telnet.println("Command received");
        if (payload.startsWith("status")){
          bool res = mqttstatus();
        } else if (payload.startsWith("gps")) {
          getGNSS();
          mqttsendHA();
        } else if (payload.startsWith("interval")) {
          String timer = payload.substring(9);
          int time = timer.toInt();
        if (time == 1) {timer = "180"; }
        if (time > 255) { timer ="255"; }
        cmd = "+CGNSSINFO="+timer;
      //  telnet.println("GPSon :"+cmd+"***");
        send_modem(cmd);
        telnet.println("Set GPS update interval to "+timer+" seconds");
        }
      } 
    } else {
      // other message
      telnet.println("modem sent:"+r);
    }
  }
}

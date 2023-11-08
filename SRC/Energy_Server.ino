#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <LEAmDNS.h>
#include <ModbusRTUMaster.h>
#include <StreamString.h>

int ledState = LOW;   // the current state of LED

#ifndef STASSID
#define STASSID "your-ssid"
#define STAPSK "your-password"
#endif

#define SLAVE_ADDRESS 1

const char* ssid = STASSID;
const char* password = STAPSK;

WebServer server(80);

float voltage, current, power, apparentPower,
      powerDMD, imported_energy,
      exported_energy, frequency, PF;

char result[8];
int16_t  temp_data = 0;
int32_t  temp_data32 = 0;

const int led = LED_BUILTIN;
const int RunPin = 22;  //Pin 29 & 30 shorted together
const int WLreset = 23;

void handleRoot() {

  static int cnt = 0;
  //digitalWrite(led, 1);
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;

  StreamString temp;
  temp.reserve(500); // Preallocate a large chunk to avoid memory fragmentation
  temp.printf("<html>\
  <head>\
    <meta http-equiv='refresh' content='5'/>\
    <title>Pico-W Energy</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <h1>Hello from the Pico W!</h1>\
    <p>Voltage: %.1fV</p>\
    <p>Current: %.3fA</p>\
    <p>Power: %.1fW</p>\
    <p>Power (DMD): %.1fW</p>\
    <p>Power Factor: %.2f</p>\
    <p>Frequency: %.1f</p>\
    <p>Imported energy: %.1fkWh</p>\
    <p>Exported energy: %.1fkWh</p>\
  </body>\
</html>", voltage, current, power, powerDMD, PF, frequency,
              imported_energy, exported_energy);
  server.send(200, "text/html", temp);
  //digitalWrite(led, 0);

}

void handleNotFound() {
  //digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  //digitalWrite(led, 0);
}

const uint8_t dePin = 3;
uint16_t holdingRegisters[60];
ModbusRTUMaster modbus(Serial2, dePin);

String s1;

void setup(void) {
  int cnt = 0;
  digitalWrite(RunPin, HIGH);
  delay(200);

  pinMode(led, OUTPUT);
  //pinMode(RunPin, OUTPUT);
  digitalWrite(led, 0);
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    cnt++;
    if (cnt > 120) {
      //After 1 minute, hard reset MCU
      pinMode(RunPin, OUTPUT);
    }
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("picow")) {
    Serial.println("MDNS responder started");
  }

  server.on("/", handleRoot);

  server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
  });

  server.on("/gif", []() {
    static const uint8_t gif[] = {
      0x47, 0x49, 0x46, 0x38, 0x37, 0x61, 0x10, 0x00, 0x10, 0x00, 0x80, 0x01,
      0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x2c, 0x00, 0x00, 0x00, 0x00,
      0x10, 0x00, 0x10, 0x00, 0x00, 0x02, 0x19, 0x8c, 0x8f, 0xa9, 0xcb, 0x9d,
      0x00, 0x5f, 0x74, 0xb4, 0x56, 0xb0, 0xb0, 0xd2, 0xf2, 0x35, 0x1e, 0x4c,
      0x0c, 0x24, 0x5a, 0xe6, 0x89, 0xa6, 0x4d, 0x01, 0x00, 0x3b
    };
    char gif_colored[sizeof(gif)];
    memcpy_P(gif_colored, gif, sizeof(gif));
    // Set the background to a random set of colors
    gif_colored[16] = millis() % 256;
    gif_colored[17] = millis() % 256;
    gif_colored[18] = millis() % 256;
    server.send(200, "image/gif", gif_colored, sizeof(gif_colored));
  });

  server.onNotFound(handleNotFound);

  /////////////////////////////////////////////////////////
  // Hook examples

  server.addHook([](const String & method, const String & url, WiFiClient * client, WebServer::ContentTypeFunction contentType) {
    (void)method;       // GET, PUT, ...
    (void)url;          // example: /root/myfile.html
    (void)client;       // the webserver tcp client connection
    (void)contentType;  // contentType(".html") => "text/html"
    //Serial.printf("A useless web hook has passed\n");
               
    //If a client is connected, read holding registers
    modbus_function(); 
    
    return WebServer::CLIENT_REQUEST_CAN_CONTINUE;
  });

  server.addHook([](const String&, const String & url, WiFiClient*, WebServer::ContentTypeFunction) {
    if (url.startsWith("/fail")) {
      Serial.printf("An always failing web hook has been triggered\n");
      return WebServer::CLIENT_MUST_STOP;
    }
    return WebServer::CLIENT_REQUEST_CAN_CONTINUE;
  });

  server.addHook([](const String&, const String & url, WiFiClient * client, WebServer::ContentTypeFunction) {
    if (url.startsWith("/dump")) {
      Serial.printf("The dumper web hook is on the run\n");

      // Here the request is not interpreted, so we cannot for sure
      // swallow the exact amount matching the full request+content,
      // hence the tcp connection cannot be handled anymore by the
      auto last = millis();
      while ((millis() - last) < 500) {
        char buf[32];
        size_t len = client->read((uint8_t*)buf, sizeof(buf));
        if (len > 0) {
          Serial.printf("(<%d> chars)", (int)len);
          Serial.write(buf, len);
          last = millis();
        }
      }
      // Two choices: return MUST STOP and webserver will close it
      //                       (we already have the example with '/fail' hook)
      // or                  IS GIVEN and webserver will forget it
      // trying with IS GIVEN and storing it on a dumb WiFiClient.
      // check the client connection: it should not immediately be closed
      // (make another '/dump' one to close the first)
      Serial.printf("\nTelling server to forget this connection\n");
      static WiFiClient forgetme = *client;  // stop previous one if present and transfer client refcounter
      return WebServer::CLIENT_IS_GIVEN;
    }
    return WebServer::CLIENT_REQUEST_CAN_CONTINUE;
  });

  // Hook examples
  /////////////////////////////////////////////////////////

  server.begin();
  Serial.println("HTTP server started");

  ///UART-modbus//////////////////////////////////////////
  Serial2.setRX(5);
  Serial2.setTX(4);
  Serial2.begin(9600);
  modbus.setTimeout(1000); //ms
  modbus.begin(9600);
}

void loop(void) {
  
  if (WiFi.status() == WL_CONNECTED)
  {
    server.handleClient();
    MDNS.update();

    //Serial.println(" connected");

    // toggle state of LED
    ledState = !ledState;
    digitalWrite(led, ledState);
  }
  else
  {
    digitalWrite(led, 0);
    //Serial.print(WiFi.status());  //0=idle, 4=CONNECT_FAILED, 6=DISCONNECTED
    delay(5000);

    //hard reset MCU
    pinMode(RunPin, OUTPUT);
  }
  //dtostrf(holdingRegisters[15], 6, 3, result);
  //Serial.printf(result);
  
  delay(1000);
}

void modbus_function(void) {
    modbus.readHoldingRegisters(SLAVE_ADDRESS, 0, holdingRegisters, 38);

    temp_data32 = ((holdingRegisters[1] << 16) | holdingRegisters[0]);
    voltage = float(temp_data32) / 10;

    temp_data32 = ((holdingRegisters[3] << 16) | holdingRegisters[2]);
    current = float(temp_data32) / 1000;

    temp_data32 = ((holdingRegisters[5] << 16) | holdingRegisters[4]);
    power = float(temp_data32) / 10;

    temp_data32 = ((holdingRegisters[11] << 16) | holdingRegisters[10]);
    powerDMD = float(temp_data32) / 10;

    temp_data = (holdingRegisters[14]);
    PF = float(temp_data) / 1000;

    temp_data = (holdingRegisters[15]);
    frequency = float(temp_data) / 10;

    temp_data32 = ((holdingRegisters[17] << 16) | holdingRegisters[16]);
    imported_energy = float(temp_data32) / 10;

    temp_data32 = ((holdingRegisters[33] << 16) | holdingRegisters[32]);
    exported_energy = float(temp_data32) / 10;  
}

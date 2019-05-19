#include <Arduino.h>
//#include "TestPic.h"

#include <GxEPD.h>
#include <GxGDEW075T8/GxGDEW075T8.h>      // 7.5" b/w
//#include <GxGDEW042T2/GxGDEW042T2.h>      // 4.2" b/w
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <FS.h>

// Settings
#define WAIT_TIME 5000

#define WIFI_SSID       "MY_WIFI"
#define WIFI_PASSWORD   "MY_PASSWORD"

// Pins (default EP8266)

#define SCLK_PIN  14
#define MOSI_PIN  13
#define CS_PIN    5
#define RST_PIN   2
#define DC_PIN    0
#define BUSY_PIN  4

// Pins (integrated solution)
/*
#define SCLK_PIN  14
#define MOSI_PIN  13
#define CS_PIN    D8
#define RST_PIN   D4
#define DC_PIN    D3
#define BUSY_PIN  D6
*/
//#define SUPPORTS_PARTIAL_UPDATE

const char* ssid = WIFI_SSID;
#if defined(WIFI_PASSWORD)
const char* password = WIFI_PASSWORD;
#endif
const char* host = "frame";

GxIO_Class io(SPI, CS_PIN, DC_PIN, RST_PIN);
GxEPD_Class display(io, RST_PIN, BUSY_PIN);

ESP8266WebServer server(80);

File uploadFile;

MDNSResponder mdns;

void setup() {
  // put your setup code here, to run once:
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.begin(115200);
  SPIFFS.begin();
  display.init();

  Serial.println();
  
  WiFi.mode(WIFI_STA);

  #if defined(WIFI_PASSWORD)
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  #else
  WiFi.begin(WIFI_SSID);
  #endif

  Serial.print("Connecting to WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println();
  Serial.println("Connected!");
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Starting HTTP Server...");

  Serial.println("Starting mDNS responder...");
  if (!mdns.begin(host, WiFi.localIP())) {
    Serial.println("Error setting up mDNS responder!");
    Serial.print("Fallback URL: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/upload");
  } else {  
    Serial.println("mDNS responder started");
  }
  
  server.on("/upload", HTTP_PUT, handleCreate);
  server.on("/upload", HTTP_DELETE, handleDelete);

  server.on("/format", HTTP_GET, []() {
    display.fillRect(10, 10, 40, 20, GxEPD_WHITE);
    for (byte i = 1; i < 4; i++) {
      display.fillCircle(i * 10 + 10, 20, 3, GxEPD_BLACK);
    }
    #if defined(SUPPORTS_PARTIAL_UPDATE)
    display.updateWindow(10, 10, 40, 20, true);
    display.powerDown();
    display.init();
    #else
    display.update();
    #endif
    Serial.println("Formatting SPIFFS...");
    SPIFFS.format();
    server.send(200, "text/plain", "SPIFFS formatted and cleared.");
    Serial.println("Format Complete.");
  });
  
  server.on("/upload", HTTP_POST, []() {
    server.send(200, "text/plain", "");
  }, handleUpload);

  server.on("/update", HTTP_GET, []() {
    server.send(200, "text/plain", "Updated!");
    display.fillScreen(GxEPD_WHITE);
    loadFromFS("/out.bin");
    display.update();
  });
  
  server.on("/upload", HTTP_GET, handleServer);

  server.onNotFound([]() {
    if (!handleRead(server.uri())) {
      server.send(404, "text/plain", "The file you requested is not available.");
    }
  });

  server.begin();

  mdns.addService("http", "tcp", 80);

  Serial.println("Server Started!");
  Serial.print("URL: http://");
  Serial.print(host);
  Serial.println(".local/upload");

  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  // put your main code here, to run repeatedly:
  server.handleClient();
  mdns.update();
}

/*
 * Handles serving a server
 */

void handleServer() { // send the right file to the client (if it exists)
  Serial.println("Handling Server Loading...");
  if (SPIFFS.exists("/server.html")) {                   // If the file exists
    File file = SPIFFS.open("/server.html", "r");        // Open it
    size_t sent = server.streamFile(file, "text/html"); // And send it to the client
    file.close();                                       // Then close the file again
    //return true;
  }
  Serial.println("\tServer file Not Found!");
  //return false;                                         // If the file doesn't exist, return false
}

/*
 * Handles a file upload
 */

void handleUpload() {
  if (server.uri() != "/upload") {
    return;
  }
  HTTPUpload& upload = server.upload();

  String filename = upload.filename;

  if (upload.status == UPLOAD_FILE_START) {
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }

    Serial.println("Handling file upload");
    Serial.print("Filename: ");
    Serial.println(filename);

    uploadFile = SPIFFS.open(filename, "w");

    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    
    //Serial.println("Handling file rewrite");
    //Serial.print("Filename: ");
    //Serial.println(filename);
    
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }    
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
    }

    Serial.println("Upload complete!");
    Serial.print("Size: ");
    Serial.println(upload.totalSize);

    server.send(200, "text/plain", "Picture uploaded, updating frame.");
    display.fillScreen(GxEPD_WHITE);
    loadFromFS("/out.bin");
    display.update();
  }
}

void handleCreate() {
  if (server.args() == 0) {
    return server.send(500, "text/plain", "BAD ARGS");
  }
  String path = server.arg(0);

  Serial.println("Handling file create");
  Serial.print("File Path: ");
  Serial.println(path);

  if (path == "/") {
    return server.send(500, "text/plain", "BAD PATH");
  }
  if (SPIFFS.exists(path)) {
    return server.send(500, "text/plain", "FILE EXISTS");
  }

  File file = SPIFFS.open(path, "w");

  if (file) {
    file.close();
  } else {
    return server.send(500, "text/plain", "CREATE FAILED");
  }

  server.send(200, "text/plain", "");
  path = String();
}

void handleDelete() {
  if (server.args() == 0) {
    return server.send(500, "text/plain", "BAD ARGS");
  }
  String path = server.arg(0);
  
  Serial.println("Handling file delete");
  Serial.print("File Path: ");
  Serial.println(path);

  if (path == "/") {
    return server.send(500, "text/plain", "BAD PATH");
  }
  if (!SPIFFS.exists(path)) {
    return server.send(404, "text/plain", "FileNotFound");
  }
  
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

bool handleRead(String path) {
  Serial.println("Handling file read");
  Serial.print("File Path: ");
  Serial.println(path);
  
  if (path.endsWith("/")) {
    path += "index.htm";
  }
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz)) {
      path += ".gz";
    }
    File file = SPIFFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

String getContentType(String filename) {
  if (server.hasArg("download")) {
    return "application/octet-stream";
  } else if (filename.endsWith(".htm")) {
    return "text/html";
  } else if (filename.endsWith(".html")) {
    return "text/html";
  } else if (filename.endsWith(".css")) {
    return "text/css";
  } else if (filename.endsWith(".js")) {
    return "application/javascript";
  } else if (filename.endsWith(".png")) {
    return "image/png";
  } else if (filename.endsWith(".gif")) {
    return "image/gif";
  } else if (filename.endsWith(".jpg")) {
    return "image/jpeg";
  } else if (filename.endsWith(".ico")) {
    return "image/x-icon";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".pdf")) {
    return "application/x-pdf";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  }
  return "text/plain";
}

void loadFromFS(String filepath) {

  if (!SPIFFS.exists(filepath)) {
    Serial.println("File does not exist");
    return;
  }
  
  uint32_t start = millis();
  
  File pic = SPIFFS.open(filepath, "r");

  Serial.println("Loading image...");

  for (uint16_t y = 0; y < display.height(); y++) {
    for (uint16_t x = 0; x < display.width() >> 3; x++) {
      byte pixel = pic.read();
      for (byte i = 0; i < 8; i++) {
        display.drawPixel((x << 3) + i, y, 
        pixel & 1<<(7-i) ? GxEPD_WHITE : GxEPD_BLACK);
      }
    }
  }
  
  pic.close();

  Serial.println("Done!");
  
  Serial.print("Process took ");
  Serial.print(millis() - start);
  Serial.println("ms.");
}

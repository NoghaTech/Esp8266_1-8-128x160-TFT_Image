#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <vector>
#include <algorithm>

/*
 * ESP8266 TFT 1.8‑inch 128×160 "Gallery"  
 * V1.6 – 2025‑06‑26  
 * Nogha_TraBiDao04
 * Mọi thắc mắc có thể liên hệ ZL:0336803185
 * Có thể tìm hiểu và tìm kiếm linh kiện vật tư tại: CAKA.VN 40/12 Lữ Gia, P15, Q11
 * ──────────────────────────────────────────
 * ► Mỗi ảnh hiển thị 5 s rồi tự chuyển sang ảnh kế tiếp, lặp vô hạn.  
 * ► Vẫn giữ nút bấm để chuyển tay ngay lập tức (reset timer).  
 * ► CSS + API không đổi.  
 */

// ---------------- PIN MAP ------------------
#define TFT_CS      15
#define TFT_DC      2
#define TFT_RST     16
#define BUTTON_PIN  0   

// ------------ Wi‑Fi credentials ------------
const char *ssid     = "$$$$$$$$";      // Đổi tên wifi
const char *password = "$$$$$$$$";  //  Đổi mật khẩu wifi

#define WIFI_TIMEOUT_MS 15000            
#define TFT_ROTATION    0               // 0=Portrait,1=Landscape,…
#define SLIDE_INTERVAL_MS 5000          

// -------------------------------------------
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);
AsyncWebServer   server(80);

std::vector<String> images;             
uint8_t  currentIndex = 0;              
uint32_t lastSlideMs = 0;               

// =========== LittleFS: list files ==========
void listFiles(){
  images.clear();
  Dir dir = LittleFS.openDir("/");
  while(dir.next()){
    String name = "/" + dir.fileName();
    if(name.endsWith(".bin")) images.push_back(name);
  }
  std::sort(images.begin(), images.end());
  if(images.empty()) images.push_back("/image1.bin");
}

// =========== Show image on TFT =============
void displayImage(const String &path){
  File pic = LittleFS.open(path, "r");
  if(!pic){ Serial.println("[TFT] open failed"); return; }

  tft.startWrite();
  tft.setAddrWindow(0,0,128,160);
  static uint16_t buf[128];
  while(pic.available()){
    size_t len = pic.read((uint8_t*)buf, sizeof(buf));
#ifdef ADAFRUIT_ST77XX_H
    tft.pushColors(buf, len/2, false);
#else
    for(size_t i=0;i<len/2;i++) tft.pushColor(buf[i]);
#endif
  }
  tft.endWrite();
  pic.close();
}

// ============== Slide logic ===============
void showCurrent(){
  displayImage(images[currentIndex]);
  lastSlideMs = millis();
}

void nextImage(){
  if(images.size()<=1) return;          // nothing to cycle
  currentIndex = (currentIndex + 1) % images.size();
  showCurrent();
}

void IRAM_ATTR onButton(){
  static uint32_t lastInt=0;
  uint32_t now=millis();
  if(now - lastInt < 250) return;       // debounce
  lastInt = now;
  nextImage();
}

// ============ HTML / JS page ===============
String htmlIndex(){
  return R"rawliteral(
<!DOCTYPE html><html lang="vi"><meta charset="utf-8">
<title>ESP8266 Gallery</title>
<style>
body{font-family:sans-serif;max-width:480px;margin:auto}
ul{max-height:240px;overflow:auto;border:1px solid #ccc;padding:4px}
li{white-space:nowrap;margin:2px 0}
</style>
<body>
<h2>Danh sách ảnh</h2><ul id="list"></ul>
<h3>Thêm ảnh mới (JPG/PNG hoặc .bin)</h3>
<input type="file" id="file" accept="image/*,.bin"><br><br>
<progress id="prog" value="0" max="100" style="width:100%;display:none"></progress>
<script>
const list=document.getElementById('list');
const prog=document.getElementById('prog');
function refresh(){ fetch('/files').then(r=>r.json()).then(arr=>{ list.innerHTML=''; arr.forEach(f=>{ const li=document.createElement('li'); li.textContent=f; if(f!='/image1.bin'){ const b=document.createElement('button'); b.textContent='Xóa'; b.style.marginLeft='1em'; b.onclick=()=>fetch('/delete?name='+encodeURIComponent(f)).then(refresh); li.appendChild(b);} list.appendChild(li);});}); }
refresh();
document.getElementById('file').onchange=async e=>{ const file=e.target.files[0]; if(!file)return; let blob,name;if(file.name.endsWith('.bin')){blob=file;name=file.name;}else{ const img=await createImageBitmap(file); const canvas=new OffscreenCanvas(128,160); const ctx=canvas.getContext('2d'); ctx.drawImage(img,0,0,128,160); const d=ctx.getImageData(0,0,128,160).data; const buf=new Uint8Array(128*160*2); for(let i=0,j=0;i<d.length;i+=4){ const r=d[i],g=d[i+1],b=d[i+2]; const rgb565=((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3); buf[j++]=rgb565&0xFF; buf[j++]=rgb565>>8;} blob=new Blob([buf],{type:'application/octet-stream'}); name=file.name.replace(/\.[^.]+$/,'')+'.bin';} const fd=new FormData(); fd.append('upload',blob,name); prog.style.display='block'; prog.value=0; const xhr=new XMLHttpRequest(); xhr.upload.onprogress=e=>{if(e.lengthComputable)prog.value=e.loaded/e.total*100}; xhr.onload=()=>{prog.style.display='none';refresh();}; xhr.open('POST','/upload'); xhr.send(fd);} ;
</script>
</body></html>)rawliteral";
}

// =========== ESP8266 Web routes ============
void setupWeb(){
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(200,"text/html",htmlIndex()); });
  server.on("/files", HTTP_GET, [](AsyncWebServerRequest *req){ DynamicJsonDocument doc(8192); for(auto &f:images) doc.add(f); String out; serializeJson(doc,out); req->send(200,"application/json",out); });
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *req){ req->send(200,"text/plain","OK"); listFiles(); currentIndex=0; showCurrent(); }, [](AsyncWebServerRequest *req,String filename,size_t index,uint8_t *data,size_t len,bool final){ if(!index) req->_tempFile=LittleFS.open("/"+filename,"w"); if(req->_tempFile) req->_tempFile.write(data,len); if(final && req->_tempFile) req->_tempFile.close(); });
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *req){ if(req->hasParam("name")){ String n=req->getParam("name")->value(); if(n!="/image1.bin") LittleFS.remove(n);} req->redirect("/"); listFiles();});
  server.begin();
}

// ================ setup ====================
void setup(){
  Serial.begin(115200);
  pinMode(BUTTON_PIN,INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN),onButton,FALLING);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(TFT_ROTATION);
  tft.fillScreen(ST77XX_BLACK);

  if(!LittleFS.begin()){ Serial.println("[FS] mount failed"); return; }
  listFiles();
  showCurrent();

  WiFi.begin(ssid,password);
  uint32_t t0=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t0<WIFI_TIMEOUT_MS){ delay(250); }
  if(WiFi.status()!=WL_CONNECTED){ WiFi.softAP("ESP_Gallery"); }

  setupWeb();
}

// ================ loop =====================
void loop(){
  if(images.size()>1 && millis()-lastSlideMs >= SLIDE_INTERVAL_MS){
    nextImage();
  }
}

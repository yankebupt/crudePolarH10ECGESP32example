#include <NimBLEDevice.h>
#include <Adafruit_NeoPixel.h>

#define HR_SERVICE_UUID "180D"
#define NEOPIXEL_PIN 48
#define NEOPIXEL_NUM 1

Adafruit_NeoPixel pixel(NEOPIXEL_NUM, NEOPIXEL_PIN, NEO_GRBW + NEO_KHZ800);


// ------- 动态获取 PMD Service 和 ECG 特征值 UUID -------
#define PMD_SERVICE_UUID "FB005C80-02E7-F387-1CAD-8ACD2D8DF0C8"
#define PMD_CONTROL_CHAR_UUID "FB005C81-02E7-F387-1CAD-8ACD2D8DF0C8"
#define PMD_DATA_CHAR_UUID "FB005C82-02E7-F387-1CAD-8ACD2D8DF0C8"
//String pmdServiceUUID = "FB005C80-02E7-F387-1CAD-8ACD2D8DF0C8";
//String controlCharUUID = "FB005C81-02E7-F387-1CAD-8ACD2D8DF0C8";
//String dataCharUUID = "FB005C82-02E7-F387-1CAD-8ACD2D8DF0C8";


NimBLEAdvertisedDevice* hrBand = nullptr;
NimBLEClient* hrclient = nullptr;
NimBLERemoteCharacteristic* controlChar = nullptr;
NimBLERemoteCharacteristic* dataChar = nullptr;

// ECG 滑动窗口, 存储5帧, 每帧最多30个样本
#define MAX_FRAME_HISTORY 5
std::vector<int32_t> ecg_history[MAX_FRAME_HISTORY];  // 5帧
int frame_idx = 0;                                    // 最新帧写入下标

// --- 单独存储正在播放的帧&样本索引 ---
std::vector<int32_t> ecg_show_frame;
int ecg_show_index = 0;
unsigned long last_show_time = 0;

// --------- ECG数据解析 ---------
void handle_ecg_frame(const uint8_t* pData, size_t len) {
  if (len < 10) return;
  if (pData[0] != 0x00 || pData[9] != 0x00) return;
  int sampleCount = (len - 10) / 3;
  ecg_history[frame_idx].clear();
  for (int i = 0; i < sampleCount; ++i) {
    int32_t ecg = (int32_t)(pData[10 + i * 3] | (pData[10 + i * 3 + 1] << 8) | (pData[10 + i * 3 + 2] << 16));
    if (ecg & 0x800000) ecg |= 0xFF000000;
    ecg_history[frame_idx].push_back(ecg);
  }
  // 新帧就绪时，准备显示用帧内容（线程安全的场合建议锁）
  ecg_show_frame = ecg_history[frame_idx];
  ecg_show_index = 0;
  frame_idx = (frame_idx + 1) % MAX_FRAME_HISTORY;
}

// --------- BLE通知回调 ---------
void notifyCallback(NimBLERemoteCharacteristic* pChr, uint8_t* pData, size_t len, bool isNotify) {
  handle_ecg_frame(pData, len);
}


// --------- 启动ECG数据流命令 ---------
static const uint8_t START_ECG_CMD[] = { 0x02, 0x00, 0x00, 0x01, 0x82, 0x00, 0x01, 0x01, 0x0E, 0x00 };

void setup() {
  //Serial.begin(115200);
  //Serial.println("Setup started");

  pixel.begin();
  //Serial.println("pixelbegin finished");
  pixel.show();
  //Serial.println("pixelshow finished");
  pixel.setBrightness(0);  // 初始灭灯
  //Serial.println("pixelbrightness finished");

  //Serial.println("Pixel init finished");


  NimBLEDevice::init("");
  //Serial.println("NimBLE init finished");
  NimBLEScan* scan = NimBLEDevice::getScan();
  //Serial.println("Scan init finished");
  NimBLEScanResults results = scan->getResults(10 * 1000);
  //Serial.println("Scan finished");
  //scan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  //scan->setActiveScan(true);
  //scan->start(5, false);
  NimBLEUUID serviceUuid("180D");
  for (int i = 0; i < results.getCount(); i++) {
      const NimBLEAdvertisedDevice *device = results.getDevice(i);
      
      if (device->isAdvertisingService(serviceUuid)) {
          // create a client and connect
          //hrBand = device; //won't work because const
          
          hrclient = NimBLEDevice::createClient();
          if (!hrclient->connect(device)) {
            //Serial.println("BLE Connect fail");
            return;
          }
          //  if (!findPMDService(client)) {
          //    Serial.println("Cannot find PMD service/char");
          //    return;
          //  }
          //Serial.printf("Found PMD Service:%s, Control:%s, Data:%s\n",
          //              pmdServiceUUID.c_str(), controlCharUUID.c_str(), dataCharUUID.c_str());
          NimBLERemoteService* service = hrclient->getService(PMD_SERVICE_UUID);
          //Serial.println("Get service passed");
          if (!service) return;
          //Serial.println("Service not null");
          controlChar = service->getCharacteristic(PMD_CONTROL_CHAR_UUID);
          //Serial.println("Get CONTROL passed");
          dataChar = service->getCharacteristic(PMD_DATA_CHAR_UUID);
          //Serial.println("Get DATA passed");
          if (!controlChar || !dataChar) return;
          if (!dataChar->subscribe(true, notifyCallback)) {
            //Serial.println("Subscribe failed");
            return;
          }
          //Serial.println("Subscribed");
          // 启动ECG流
          controlChar->writeValue(START_ECG_CMD, sizeof(START_ECG_CMD), false);
          //Serial.println("Start ECG collection");
      }
  }
  //Serial.println("Setup finished");
  // 等待找到设备
  //while (!hrBand) delay(100);
  
}

void loop() {
  unsigned long now = millis();
  // 每7ms切换显示下一个样本
  if (!ecg_show_frame.empty() && (now - last_show_time >= 7)) {
    // 获取滑动窗口内（5帧）所有样本用于min-max归一化
    int32_t vmin = INT32_MAX, vmax = INT32_MIN;
    int64_t vsum = 0;
    int vcount = 0;
    for (int i = 0; i < MAX_FRAME_HISTORY; ++i)
      for (auto v : ecg_history[i]) {
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
        vsum += v;
        vcount++;
      }
    float vaverage = vcount > 0 ? static_cast<float>(vsum) / vcount : 0.0;
    int32_t vcur = ecg_show_frame[ecg_show_index];
    float ratio = 0.5;
    if (vmax > vmin) ratio = float(vcur - vaverage) / (vmax - vmin) + 0.1;
    // 亮度和色彩映射
    if (ratio<0) ratio = 0.0;
    int brightness = 0 + ratio * 225;
    uint32_t color = pixel.Color(255, 0, 0);
    pixel.setBrightness(brightness);
    pixel.setPixelColor(0, color);
    pixel.show();
    ecg_show_index++;
    if (ecg_show_index >= ecg_show_frame.size()) ecg_show_index = 0;  // 一帧播完，从头循环
    last_show_time = now;
  }
  // 若无数据，保持黑灯
  if (ecg_show_frame.empty()) {
    //pixel.setBrightness(0);
    //pixel.show();
    delay(5);
  }
  
}
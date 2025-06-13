#include <NimBLEDevice.h>
#include <Adafruit_NeoPixel.h>

#define HR_SERVICE_UUID "180D"
#define NEOPIXEL_PIN 48
#define NEOPIXEL_NUM 1

Adafruit_NeoPixel pixel(NEOPIXEL_NUM, NEOPIXEL_PIN, NEO_GRBW + NEO_KHZ800);

#define PMD_SERVICE_UUID "FB005C80-02E7-F387-1CAD-8ACD2D8DF0C8"
#define PMD_CONTROL_CHAR_UUID "FB005C81-02E7-F387-1CAD-8ACD2D8DF0C8"
#define PMD_DATA_CHAR_UUID "FB005C82-02E7-F387-1CAD-8ACD2D8DF0C8"

NimBLEClient* hrclient = nullptr;
NimBLERemoteCharacteristic* controlChar = nullptr;
NimBLERemoteCharacteristic* dataChar = nullptr;

#define MAX_FRAME_HISTORY 5
std::vector<int32_t> ecg_history[MAX_FRAME_HISTORY];
int frame_idx = 0;
std::vector<int32_t> ecg_show_frame;
int ecg_show_index = 0;
unsigned long last_show_time = 0;

static const uint8_t START_ECG_CMD[] = { 0x02, 0x00, 0x00, 0x01, 0x82, 0x00, 0x01, 0x01, 0x0E, 0x00 };

unsigned long last_ecg_data_time = 0;
unsigned long last_ble_try_time = 0;
bool ble_connected = false;

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
  ecg_show_frame = ecg_history[frame_idx];
  ecg_show_index = 0;
  frame_idx = (frame_idx + 1) % MAX_FRAME_HISTORY;
  last_ecg_data_time = millis();
}

void notifyCallback(NimBLERemoteCharacteristic* pChr, uint8_t* pData, size_t len, bool isNotify) {
  handle_ecg_frame(pData, len);
}

void disconnect_ble() {
  if (hrclient) {
    if (hrclient->isConnected()) hrclient->disconnect();
    hrclient = nullptr; // 只是清空指针，不 delete
  }
  controlChar = nullptr;
  dataChar = nullptr;
  ble_connected = false;
}

bool try_connect_ble() {
  disconnect_ble();
  NimBLEScan* scan = NimBLEDevice::getScan();
  NimBLEScanResults results = scan->getResults(3000);
  NimBLEUUID serviceUuid(HR_SERVICE_UUID);

  for (int i = 0; i < results.getCount(); i++) {
    const NimBLEAdvertisedDevice* device = results.getDevice(i);
    if (device->isAdvertisingService(serviceUuid)) {
      hrclient = NimBLEDevice::createClient();
      if (!hrclient->connect(device)) {
        disconnect_ble();
        continue;
      }
      NimBLERemoteService* service = hrclient->getService(PMD_SERVICE_UUID);
      if (!service) { disconnect_ble(); continue; }
      controlChar = service->getCharacteristic(PMD_CONTROL_CHAR_UUID);
      dataChar = service->getCharacteristic(PMD_DATA_CHAR_UUID);
      if (!controlChar || !dataChar) { disconnect_ble(); continue; }
      if (!dataChar->subscribe(true, notifyCallback)) { disconnect_ble(); continue; }
      if (!controlChar->writeValue(START_ECG_CMD, sizeof(START_ECG_CMD), false)) { disconnect_ble(); continue; }
      ble_connected = true;
      last_ecg_data_time = millis();
      return true;
    }
  }
  disconnect_ble();
  return false;
}

void setup() {
  pixel.begin();
  pixel.show();
  pixel.setBrightness(0);
  NimBLEDevice::init("");
  try_connect_ble();
  last_ble_try_time = millis();
}

void loop() {
  unsigned long now = millis();

  if (!ble_connected || (now - last_ecg_data_time > 2000)) {
    if (now - last_ble_try_time > 3000) {
      try_connect_ble();
      last_ble_try_time = now;
    }
  }

  if (!ble_connected) {
    pixel.setBrightness(0);
    pixel.show();
    delay(5);
    return;
  }

  if (!ecg_show_frame.empty() && (now - last_show_time >= 7)) {
    int32_t vmin = INT32_MAX, vmax = INT32_MIN;
    std::vector<int32_t> samples;
    for (int i = 0; i < MAX_FRAME_HISTORY; ++i) {
      for (auto v : ecg_history[i]) {
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
        samples.push_back(v);
      }
    }
    float vmedian = 0.0f;
    if (!samples.empty()) {
      std::sort(samples.begin(), samples.end());
      size_t n = samples.size();
      vmedian = (n % 2 == 1) ? static_cast<float>(samples[n / 2]) : 0.5f * (samples[n / 2 - 1] + samples[n / 2]);
    }
    int32_t vcur = ecg_show_frame[ecg_show_index];
    float ratio = 0.5;
    if (vmax > vmin) ratio = 2.0f * float(vcur - vmedian) / (vmax - vmin);
    if (ratio < 0) ratio = 0.0;
    int brightness = 0 + ratio * 225;
    uint32_t color = pixel.Color(255, 0, 0);
    pixel.setBrightness(brightness);
    pixel.setPixelColor(0, color);
    pixel.show();
    ecg_show_index++;
    if (ecg_show_index >= ecg_show_frame.size()) ecg_show_index = 0;
    last_show_time = now;
  }

  if (ecg_show_frame.empty()) {
    pixel.setBrightness(0);
    pixel.show();
    delay(5);
  }
}

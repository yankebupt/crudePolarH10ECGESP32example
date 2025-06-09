#include <NimBLEDevice.h>

// Default onboard LED pin for ESP32
#define LED_PIN LED_BUILTIN

// Heart Rate Service & Characteristic UUIDs (standard)
#define HR_SERVICE_UUID        "180D"
#define HR_CHARACTERISTIC_UUID "2A37"

// Default blink intervals (ms)
#define BLINK_ON_MS  100

// BLE & Blink State
NimBLEAdvertisedDevice* hrDevice = nullptr;
NimBLEClient* pClient = nullptr;
NimBLERemoteCharacteristic* pHRChar = nullptr;

volatile int heartRateBPM = 0;
volatile bool hrActive = false;
unsigned long lastHRTime = 0;
unsigned long lastBlinkTime = 0;
bool ledState = false;

// Calculate the blink cycle (ms) from BPM (1 beat per X ms)
unsigned long getBlinkCycleMs(int bpm) {
    if (bpm <= 0) return 0;
    return 60000 / bpm;
}

// Heart Rate Notification Handler
void onHRNotify(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    if (length < 2) return; // At least 2 bytes for HRM
    uint8_t hrValue = pData[1]; // Second byte is usually BPM (8-bit)
    heartRateBPM = hrValue;
    lastHRTime = millis();
    hrActive = true;
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    NimBLEDevice::init("ESP32-HR-Blinker");
    NimBLEScan* scan = NimBLEDevice::getScan();
    NimBLEScanResults results = scan->getResults(10 * 1000);
//    pScan->setAdvertisedDeviceCallbacks(new HRAdvertisedDeviceCallbacks());
//    pScan->setActiveScan(true);
//    pScan->start(10, false); // Scan for 10 seconds, but will stop on first device
    NimBLEUUID serviceUuid("180D");

    for (int i = 0; i < results.getCount(); i++) {
        const NimBLEAdvertisedDevice *device = results.getDevice(i);
        
        if (device->isAdvertisingService(serviceUuid)) {
            // create a client and connect
            //hrDevice = device; //won't work because const

            Serial.println("Connecting to HR Band...");
            pClient = NimBLEDevice::createClient();
            if (!pClient->connect(device)) {
                Serial.println("Failed to connect.");
                NimBLEDevice::deleteClient(pClient);
                pClient = nullptr;
                return;
            }
            Serial.println("Connected!");
            NimBLERemoteService* pService = pClient->getService(HR_SERVICE_UUID);
            if (pService) {
                pHRChar = pService->getCharacteristic(HR_CHARACTERISTIC_UUID);
                if (pHRChar && pHRChar->canNotify()) {
                    pHRChar->subscribe(true, onHRNotify);
                    hrActive = false;
                    lastHRTime = millis();
                    Serial.println("Subscribed to HR characteristic.");
                }
            }
        }
    }


}

void loop() {

    // If connected, check for HR data timeout
    if (pClient && pClient->isConnected()) {
        if (millis() - lastHRTime > 5000) { // 5 seconds no data
            hrActive = false;
            heartRateBPM = 0;
        }
    } else {
        hrActive = false;
        heartRateBPM = 0;
        pClient = nullptr;
        pHRChar = nullptr;
    }

    // Blinker logic
    if (hrActive && heartRateBPM > 0) {
        unsigned long cycleMs = getBlinkCycleMs(heartRateBPM);
        unsigned long now = millis();
        if (ledState) {
            if (now - lastBlinkTime >= BLINK_ON_MS) {
                digitalWrite(LED_PIN, LOW);
                ledState = false;
                lastBlinkTime = now;
            }
        } else {
            if (now - lastBlinkTime >= (cycleMs - BLINK_ON_MS)) {
                digitalWrite(LED_PIN, HIGH);
                ledState = true;
                lastBlinkTime = now;
                Serial.printf("BPM: %d (cycle: %lums)\n", heartRateBPM, cycleMs);
            }
        }
    } else {
        // Pause (turn off) if no HR data or disconnected
        digitalWrite(LED_PIN, LOW);
        ledState = false;
    }

    delay(1); // Keep it snappy
}

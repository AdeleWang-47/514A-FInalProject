#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Wire.h>
#include <Adafruit_TSL2591.h>

// BLE UUID 定义
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// FSR 传感器引脚（模拟输入）
#define FSR_PIN1 A0
#define FSR_PIN2 A1

// TSL2591 I2C 引脚（SDA = D4, SCL = D5）
#define TSL_SDA D4
#define TSL_SCL D5

// 创建 TSL2591 对象
Adafruit_TSL2591 tsl = Adafruit_TSL2591(2591);

BLEServer *pServer = nullptr;
BLECharacteristic *pCharacteristic = nullptr;
bool deviceConnected = false;

// BLE 连接回调
class MyServerCallbacks : public BLEServerCallbacks {
public:
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("Client connected");
  }
  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("Client disconnected");
    // 断开后重新启动广告
    BLEDevice::startAdvertising();
    Serial.println("Restarting advertising...");
  }
};

void setupTSL2591() {
  Wire.begin(TSL_SDA, TSL_SCL);
  if (!tsl.begin()) {
    Serial.println("TSL2591 not detected! Check wiring.");
    while(1);
  }
  tsl.setGain(TSL2591_GAIN_MED);
  tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting Server...");

  setupTSL2591();

  // 初始化 BLE
  BLEDevice::init("ESP32C3_SERVER");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->setValue("0,0,0\n");
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("BLE Server advertising...");
}

void loop() {
  if (deviceConnected) {
    // 获取 TSL2591 数据：32 位原始数据，低16位为 broadband，高16位为 IR
    uint32_t lum = tsl.getFullLuminosity();
    uint16_t broadband = lum & 0xFFFF;
    uint16_t ir = lum >> 16;
    float lux = tsl.calculateLux(broadband, ir);

    // 读取 FSR 数据
    int fsr1 = analogRead(FSR_PIN1);
    int fsr2 = analogRead(FSR_PIN2);

    // 只保留 lux 整数部分，格式：lux,fsr1,fsr2\n
    String sensorData = String((int)lux) + "," + String(fsr1) + "," + String(fsr2) + "\n";
    pCharacteristic->setValue(sensorData.c_str());
    pCharacteristic->notify();
    Serial.println(sensorData);
  }
  else {
    Serial.println("Waiting for BLE connection...");
  }
  
  // 延长发送间隔为 10000 毫秒
  delay(10000);
}

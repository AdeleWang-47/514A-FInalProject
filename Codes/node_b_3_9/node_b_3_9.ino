#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLEAdvertisedDevice.h>
#include <Adafruit_NeoPixel.h>
#include <Stepper.h>

// BLE UUID 定义（与 Server 保持一致）
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// LED 灯带定义
#define LED_PIN A10
#define NUM_LEDS 28

// 步进电机引脚定义（按顺序连接到 A4, A5, A9, A8）
#define STEPPER_IN1 A8
#define STEPPER_IN2 A9
#define STEPPER_IN3 A5
#define STEPPER_IN4 A4
#define STEPS_PER_REV 600  // 步进电机一转600步

// POTENTIOMETER 接 A3，用于设置 LED 最大亮度
#define POTENTIOMETER_PIN A3

// 全局 BLE 变量
static BLEAddress *pServerAddress;
bool doConnect = false;
bool connected = false;
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;

// 全局变量保存从 Server 接收到的传感器数据（格式："lux,fsr1,fsr2"）
String sensorData = "";

// LED 灯带对象
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
// 步进电机对象
Stepper stepper(STEPS_PER_REV, STEPPER_IN1, STEPPER_IN2, STEPPER_IN3, STEPPER_IN4);

// LED 强制点亮状态和结束时间
bool ledForced = false;
unsigned long ledForcedUntil = 0;

// 通知回调：接收到数据后保存到 sensorData 并打印
void notifyCallback(BLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  sensorData = "";
  for (size_t i = 0; i < length; i++) {
    sensorData += (char)pData[i];
  }
  Serial.print("收到通知: ");
  Serial.println(sensorData);
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    Serial.println(advertisedDevice.toString().c_str());
    if (advertisedDevice.haveServiceUUID() &&
        advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
      pServerAddress = new BLEAddress(advertisedDevice.getAddress());
      Serial.print("Found our server: ");
      Serial.println(advertisedDevice.toString().c_str());
      doConnect = true;
      BLEDevice::getScan()->stop();
    }
  }
};

bool connectToServer(BLEAddress pAddress) {
  Serial.print("Forming a connection to ");
  Serial.println(pAddress.toString().c_str());
  
  pClient = BLEDevice::createClient();
  Serial.println(" - Created client");
  
  if (!pClient->connect(pAddress)) {
    Serial.println(" - Failed to connect to server");
    return false;
  }
  Serial.println(" - Connected to server");
  
  BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
  if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(SERVICE_UUID);
      pClient->disconnect();
      return false;
  }
  
  pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
  if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(CHARACTERISTIC_UUID);
      pClient->disconnect();
      return false;
  }
  
  String value = pRemoteCharacteristic->readValue().c_str();
  Serial.print("Initial characteristic value: ");
  Serial.println(value);
  
  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->registerForNotify(notifyCallback);
    Serial.println("注册通知成功");
  }
  
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("启动 Client ...");
  
  // 初始化 LED 灯带和步进电机
  strip.begin();
  strip.show();  // 关闭所有 LED
  stepper.setSpeed(10);
  
  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  Serial.println("开始扫描 BLE 设备...");
  pBLEScan->start(60);  // 扫描 60 秒
}

void loop() {
  if (doConnect) {
    if (connectToServer(*pServerAddress)) {
      Serial.println("We are now connected to the BLE Server.");
      connected = true;
    } else {
      Serial.println("Failed to connect; restarting scan.");
    }
    doConnect = false;
  }
  
  Serial.print("connected = ");
  Serial.println(connected);
  if (pClient != nullptr) {
    Serial.print("pClient->isConnected() = ");
    Serial.println(pClient->isConnected() ? "true" : "false");
  }
  
  // 如果 LED 处于强制点亮状态，实时更新 potentiometer 并调整 LED 亮度
  if (ledForced) {
    // 在强制点亮期间，每次循环更新 LED 亮度
    int potVal = analogRead(POTENTIOMETER_PIN);
    int brightness = map(potVal, 0, 4095, 0, 255);
    Serial.print("Potentiometer raw: ");
    Serial.print(potVal);
    Serial.print(" -> brightness: ");
    Serial.println(brightness);
    if (brightness < 10) brightness = 255; // 默认100%
    
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(brightness, brightness, brightness));
    }
    strip.show();
    
    // 更新步进电机位置
    static int previousStepperPos = 0;
    int targetStepperPos = map(brightness, 0, 255, 0, STEPS_PER_REV);
    int stepsToMove = targetStepperPos - previousStepperPos;
    if (stepsToMove != 0) {
      stepper.step(stepsToMove);
      previousStepperPos = targetStepperPos;
    }
    
    // 检查是否10秒到期
    if (millis() >= ledForcedUntil) {
      // 关闭 LED
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, 0);
      }
      strip.show();
      Serial.println("LED 灯带已自动关闭（10秒到期）");
      ledForced = false;
    }
  }
  // 如果不在强制点亮状态，则处理新的 sensorData
  else if (connected && sensorData.length() > 0) {
    // 解析 sensorData，格式应为 "lux,fsr1,fsr2"
    int firstComma = sensorData.indexOf(",");
    int secondComma = sensorData.indexOf(",", firstComma + 1);
    if (firstComma != -1 && secondComma != -1) {
      int luxValue = sensorData.substring(0, firstComma).toInt();
      int fsr1Value = sensorData.substring(firstComma + 1, secondComma).toInt();
      int fsr2Value = sensorData.substring(secondComma + 1).toInt();
      
      Serial.print("Parsed values: Lux=");
      Serial.print(luxValue);
      Serial.print(", FSR1=");
      Serial.print(fsr1Value);
      Serial.print(", FSR2=");
      Serial.println(fsr2Value);
      
      // 判断条件：如果 lux < 50 且 (fsr1 > 3000 或 fsr2 > 3000)
      if (luxValue < 50 && (fsr1Value > 3000 || fsr2Value > 3000)) {
        Serial.println("条件满足: LED 灯带点亮 10秒");
        
        // 读取 potentiometer设置 LED 最大亮度
        int potVal = analogRead(POTENTIOMETER_PIN);
        int brightness = map(potVal, 0, 4095, 0, 255);
        Serial.print("Potentiometer raw value: ");
        Serial.print(potVal);
        Serial.print(" -> Mapped brightness: ");
        Serial.println(brightness);
        if (brightness < 10) brightness = 255; // 默认 100%
        
        // 点亮 LED 灯带
        for (int i = 0; i < NUM_LEDS; i++) {
          strip.setPixelColor(i, strip.Color(brightness, brightness, brightness));
        }
        strip.show();
        
        // 更新步进电机位置（映射亮度到步进数）
        static int previousStepperPos = 0;
        int targetStepperPos = map(brightness, 0, 255, 0, STEPS_PER_REV);
        int stepsToMove = targetStepperPos - previousStepperPos;
        if (stepsToMove != 0) {
          stepper.step(stepsToMove);
          previousStepperPos = targetStepperPos;
        }
        
        // 设置 LED 强制点亮状态，10秒内实时更新亮度
        ledForcedUntil = millis() + 10000;
        ledForced = true;
      }
    } else {
      Serial.println("解析数据格式出错");
    }
    // 清空 sensorData，避免重复处理
    sensorData = "";
  } else {
    Serial.println("等待 BLE 连接或接收数据...");
  }
  
  delay(1000);
}

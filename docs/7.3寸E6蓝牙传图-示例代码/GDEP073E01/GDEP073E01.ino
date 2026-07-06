#include <SPI.h>
#include "Display_EPD_W21_spi.h"
#include "Display_EPD_W21.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEClient.h>

const int BUFFER_SIZE = ALLSCREEN_BYTES;  // 屏幕缓存大小

int dataIndex = 0;
bool dataReceived = false;
unsigned long lastReceiveTime = 0;             // 记录上次接收到数据的时间
const unsigned long RECEIVE_TIMEOUT = 100000;  // 超时时间（毫秒）

// BLE UUIDs
#define SERVICE_UUID "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    if (dataIndex == 0) {
      EPD_init();  //接收到第一个数据块时初始化EPD
      EPD_W21_WriteCMD(0x10);  //DTM准备传输数据到EPD缓存
      Serial.println("初始化EPD");
    }
    String rxValue = pCharacteristic->getValue();
    int dataLength = rxValue.length();
    if (dataLength > 0) {
      unsigned char buff[dataLength];
      memcpy(buff, rxValue.c_str(), dataLength);
      for (int i = 0; i < dataLength; i++) {
        EPD_W21_WriteDATA(buff[i]);  //传输接收到的数据，BLE每次传输最大512字节
      }

      dataIndex += dataLength;
      Serial.printf("已接收: %d字节数据\n", dataIndex);
      lastReceiveTime = millis();
    }

    if (dataIndex >= BUFFER_SIZE) {
      dataReceived = true;
      Serial.println("接收到完整数据");
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(5, INPUT);   // BUSY
  pinMode(4, OUTPUT);  // RES
  pinMode(3, OUTPUT);  // DC
  pinMode(2, OUTPUT);  // CS

  // SPI
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  SPI.begin();

  Serial.println("ESP32已启动");
  uint8_t mac[6];
  Serial.print("ESP32 MAC 地址: ");
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 16)
      Serial.print("0");
    Serial.print(mac[i], HEX);
    if (i < 5)
      Serial.print(":");
  }
  Serial.println();

  dataIndex = 0;
  dataReceived = false;

  // Initialize BLE
  BLEDevice::init("ESP32_BLE_EPD_TAG");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_WRITE);

  pCharacteristic->setCallbacks(new MyCallbacks());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // Functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("等待蓝牙连接...");
  Serial.println("蓝牙服务和特征 UUID 已启动");
  dataIndex = 0;
  dataReceived = false;
}

void loop() {
  if ((millis() - lastReceiveTime) > RECEIVE_TIMEOUT && dataIndex >= BUFFER_SIZE) {
    dataReceived = true;
  }

  if (dataReceived) {
    // 直接使用接收到的数据
    // EPD_init();
    // PIC_display(receivedData);  // 直接传入接收到的数据
    EPD_autoSequence();  //自动序列：上电->刷新->下电->睡眠
    // EPD_sleep();
    //delay(2000);
    Serial.println("数据接收并显示在EPD上");
    dataIndex = 0;
    dataReceived = false;
    //esp_deep_sleep_start();
  }
}

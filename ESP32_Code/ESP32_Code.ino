#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b" // UART service UUID
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };  

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

float ppg_array[20000] = {0}; // 16384  
int red_counter = 0;
int ir_counter = 0; // 8192 + 525
long red_DC = 0;
long ir_DC = 0;
int state = 0;
int i = 0;
long sum_raw_data = 0;


volatile int interruptCounter;

hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR onTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  interruptCounter++;
  //ADCValue = analogRead(GPIO_38);
  portEXIT_CRITICAL_ISR(&timerMux);
 
}

void setup() {
  
  Serial.begin(115200);// 115200

  // Creating the BLE Device
  BLEDevice::init("MQP");
  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE
                                       );

  pCharacteristic->addDescriptor(new BLE2902());

  // Start the service
  pService->start();

  // Start advertising
  pServer->getAdvertising()->start();


  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 2000, true); // 500 hz interrupt
  timerAlarmEnable(timer);

  // ADC setup (voltage ref)
  analogSetAttenuation(ADC_11db);

  pinMode(4, OUTPUT);
  pinMode(2, OUTPUT);
  digitalWrite(4, HIGH);
  digitalWrite(2, HIGH);
  dacWrite(26, 0); // RED
  dacWrite(25, 0); // IR - 112
  
}

void loop() { 

  if (interruptCounter > 0) {

      portENTER_CRITICAL(&timerMux);
      interruptCounter--; // critical section
      portEXIT_CRITICAL(&timerMux);


      switch (state) {
        case 0:
          {
            digitalWrite(4, LOW);
            dacWrite(26, 70); // 80, 70 RED - was previously 75, 92
            for (int raw_cnt=0; raw_cnt<8; raw_cnt++) {
              sum_raw_data += analogRead(38)*3300/4095;
            }
            red_DC += analogRead(34)*3300/4095;
        
            state = 1;
          }
          break;
        case 1:
          {
              digitalWrite(4, HIGH);
              dacWrite(26, 0); // RED
              ppg_array[red_counter] = sum_raw_data/8;
              red_counter++;
              sum_raw_data = 0;
              state = 0;
            if (red_counter == 20000) { // 8192 + (525 to transmit values...)
              red_DC = red_DC/20000;
              state = 4;
              red_counter = 0;
            }
          }
          break;
        case 2:
          {
            digitalWrite(2, LOW);
            dacWrite(25, 70); // 80, 70 RED - was previously 75, 92
            for (int raw_cnt=0; raw_cnt<8; raw_cnt++) {
              sum_raw_data += analogRead(38)*3300/4095;
            }

            ir_DC += analogRead(34)*3300/4095;
        
            state = 3;
          }
          break;
        case 3:
          {
              digitalWrite(2, HIGH);
              dacWrite(25, 0); // RED
              ppg_array[ir_counter] = sum_raw_data/8;
              ir_counter++;
              sum_raw_data = 0;
              state = 2;
            if (ir_counter == 20000) { // 8192 + (525 to transmit values...)
              ir_DC = ir_DC/20000;
              state = 5;
              ir_counter = 0;
            }
          }
          break;
        case 4:
          {
            if (deviceConnected) {
              
                char txString[8]; 
                dtostrf(ppg_array[i], 7, 2, txString);
                pCharacteristic->setValue(txString);  
                pCharacteristic->notify();
                i++;
                delay(100);
              }
           
                
            if (i==20000) { // 16384
                char txString[8];
                dtostrf(red_DC, 7, 2, txString);
                pCharacteristic->setValue(txString);  
                pCharacteristic->notify();
                state = 2;
                i = 0;
            }            
          }
          break;
        case 5:
          {
            if (deviceConnected) {
              
                char txString[8]; 
                dtostrf(ppg_array[i], 7, 2, txString);
                pCharacteristic->setValue(txString);  
                pCharacteristic->notify();
                i++;
                delay(100);
              }
           
                
            if (i==20000) { // 16384
                char txString[8]; 
                dtostrf(red_DC, 7, 2, txString);
                pCharacteristic->setValue(txString);  
                pCharacteristic->notify();
                state = 0;
                i = 0;
            }
          }
          break;
      }

    
  }
}

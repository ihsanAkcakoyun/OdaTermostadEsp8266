

/* PINOUT
  nRF24L01 from pin side/top:
  -------------
  |1 3 5 7    |
  |2 4 6 8    |
  |           |
  |           |
  |           |
  |           |
  |           |
  -------------
  1 - GND  blk   GND
  2 - VCC  wht   3V3
  3 - CE   orng  D4-3,3Kohm ile pulldown yap
  4 - CSN  yell  D8
  5 - SCK  grn   D5
  6 - MOSI blue  D7
  7 - MISO viol  D6
  8 - IRQ  gray  BOŞ
  6-- role pin
  7--esp32 pin
  5-- ds18b20
 
*/
#include <DallasTemperature.h>

#include <OneWire.h>
#define BLYNK_PRINT Serial
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <Wire.h>


#include <SPI.h>
#include <RF24.h>
#include <EEPROM.h>


RF24 radio(2, 15);


#define DATA_PIN 5
OneWire oneWire (DATA_PIN);
DallasTemperature sensors (&oneWire);


int sdebug = 0;
int seTemp ;
float set;
String a;
float energiSicaklik;

char auth[] = "";

char ssid[] = "Wifi adi";
char pass[] = "Wifi sifresi";
WidgetLED yesil_blynk(V4);

#define relay 16
#define kombiPin 3
boolean KombiDurum ;


int addr =0;
int sabit;
// -------------------------

void setup()
{
  sensors.begin();
  pinMode(relay, OUTPUT);
  digitalWrite(relay, LOW);

  initBLE();
  Serial.begin(9600);

  Blynk.begin(auth, ssid, pass);
  Serial.println(F("BLE Mi Bluetooth Temperature & Humidity Monitor"));
EEPROM.begin(512);
 
}

// -------------------------

const uint8_t channel[3]   = {37, 38, 39}; // BLE advertisement channel number
const uint8_t frequency[3] = { 2, 26, 80}; // real frequency (2400+x MHz)

struct bleAdvPacket { // for nRF24L01 max 32 bytes = 2+6+24
  uint8_t pduType;
  uint8_t payloadSize;  // payload size
  uint8_t mac[6];
  uint8_t payload[24];
};

uint8_t currentChan = 0;
bleAdvPacket buffer;

void initBLE()
{
  radio.begin();
  radio.setAutoAck(false);
  radio.setDataRate(RF24_1MBPS);
  radio.disableCRC();
  radio.setChannel( frequency[currentChan] );
  radio.setRetries(0, 0);
  radio.setPALevel(RF24_PA_MAX);
  radio.setAddressWidth(4);
  radio.openReadingPipe(0, 0x6B7D9171); // advertisement address: 0x8E89BED6 (bit-reversed -> 0x6B7D9171)
  radio.openWritingPipe(  0x6B7D9171);
  radio.powerUp();
}

void hopChannel()
{
  currentChan++;
  if (currentChan >= sizeof(channel)) currentChan = 0;
  radio.setChannel( frequency[currentChan] );
}

bool receiveBLE(int timeout)
{
  radio.startListening();
  delay(timeout);
  if (!radio.available()) return false;
  while (radio.available()) {
    radio.read( &buffer, sizeof(buffer) );
    swapbuf( sizeof(buffer) );
    whiten( sizeof(buffer) );
  }
  return true;
}

// change buffer content to "wire bit order"
void swapbuf(uint8_t len)
{
  uint8_t* buf = (uint8_t*)&buffer;
  while (len--) {
    uint8_t a = *buf;
    uint8_t v = 0;
    if (a & 0x80) v |= 0x01;
    if (a & 0x40) v |= 0x02;
    if (a & 0x20) v |= 0x04;
    if (a & 0x10) v |= 0x08;
    if (a & 0x08) v |= 0x10;
    if (a & 0x04) v |= 0x20;
    if (a & 0x02) v |= 0x40;
    if (a & 0x01) v |= 0x80;
    *(buf++) = v;
  }
}

void whiten(uint8_t len)
{
  uint8_t* buf = (uint8_t*)&buffer;
  // initialize LFSR with current channel, set bit 6
  uint8_t lfsr = channel[currentChan] | 0x40;
  while (len--) {
    uint8_t res = 0;
    // LFSR in "wire bit order"
    for (uint8_t i = 1; i; i <<= 1) {
      if (lfsr & 0x01) {
        lfsr ^= 0x88;
        res |= i;
      }
      lfsr >>= 1;
    }
    *(buf++) ^= res;
  }
}

// -------------------------



// -------------------------
float sicaklik;
char buf[100];
int temp = 0.0;
int hum = -1;
int bat = -1;
int x, cnt = 0, mode = 0, v1, v10;
int tempOld = -1230;
int humOld = -123;
int batOld = -123;
int cntOld = -1;
unsigned long tmT = 0;
unsigned long tmH = 0;
unsigned long tmB = 0;
unsigned long tmD = 0;
char *modeTxt = "";
bool isTempOK(int v) {
  return (v >= -400 && v <= 800);
}
bool isHumOK(int v) {
  return (v >= 0 && v <= 1000);
}
// Xiaomi advertisement packet decoding
//          18       21 22 23 24
//          mm       tl th hl hh
// a8 65 4c 0d 10 04 da 00 de 01  -> temperature+humidity
//          mm       hl hh
// a8 65 4c 06 10 04 da 01        -> humidity
//          mm       tl th
// a8 65 4c 04 10 04 db 00        -> temperature
//          mm       bb
// a8 75 4c 0a 10 01 60           -> battery
// 75 e7 f7 e5 bf 23 e3 20 0d 00  -> ???
//          21 e6 f6 18 dc c6 01  -> ???
// b8 65 5c 0e 10 41 60           -> battery??
// a8 65 4c 46 10 02 d4 01        -> ??



void loop()
{
   sabit = EEPROM.read(addr);
  Blynk.run();

    receiveBLE(100);
  uint8_t *recv = buffer.payload;
  // Kendi Xaiomi Termometrenizin Mac adresini alt satıra güncellemeyi unutma !!
  if (buffer.mac[5] == 0x58 && buffer.mac[0] == 0x44 && buffer.mac[1] == 0xe9) // limit to my Xiaomi MAC address (1st and last number only)
    if (recv[5] == 0x95 && recv[6] == 0xfe && recv[7] == 0x50 && recv[8] == 0x20)
    {
      cnt = recv[11];
      mode = recv[18];
      int mesSize = recv[3];
      int plSize = buffer.payloadSize - 6;
      if (mode == 0x0d && plSize == 25) { 
        temp = recv[21] + recv[22] * 256;
        modeTxt = "TH";

        if (sdebug) snprintf(buf, 100, "#%02x %02x %s %02x %3d'C (%3d%%)", cnt, mode, modeTxt, recv[3], recv[21] + recv[22] * 256, recv[23]);

        if (humOld > 0) { 
          hum = (humOld & ~0xff) | recv[23];
          if (hum - humOld > 128) hum -= 256; else if (humOld - hum > 128) hum += 256;
          tmH = millis();
        }
        tmT = millis();
      } else if (mode == 0x04 && plSize == 23) { 
        temp = recv[21] + recv[22] * 256;
        modeTxt = "T ";

        if (sdebug)
        {
          snprintf(buf, 100, "#%02x %02x %s %02x %3d'C       ", cnt, mode, modeTxt, recv[3], recv[21] + recv[22] * 256);

        }

        tmT = millis();

      } else if (mode == 0x06 && plSize == 23) { 
        hum = recv[21] + recv[22] * 256;
        modeTxt = "H ";
        if (sdebug) snprintf(buf, 100, "#%02x %02x %s %02x %3d%%        ", cnt, mode, modeTxt, recv[3], recv[21] + recv[22] * 256);
        tmH = millis();
      } else if (mode == 0x0a && plSize == 22) { // battery level
        bat = recv[21];
        modeTxt = "B ";
        if (sdebug) snprintf(buf, 100, "#%02x %02x %s %02x %03d%% batt   ", cnt, mode, modeTxt, recv[3], recv[21]);
        tmB = millis();
      } else {
        modeTxt = "??";
        if (sdebug) snprintf(buf, 100, "!!!!!!%02x %02x %s %02x %03d %03d", cnt, mode, modeTxt, recv[3], recv[21], recv[22]);
      }
         }
  hopChannel();
if (seTemp!=0 & seTemp != sabit)
{
      EEPROM.write(addr, seTemp); // sıcaklık ayarı değiştirildiğinde eeproma sıcaklık ayarı atanıyor.
         
}
energi(); // güneş enerjisi döngüsü
  kombi(); // kombi çalışma döngüsü

  ekran(); // ölçülen ve atanan değerlerin Serial ekrana yazırma ve telefona gönderme döngüsü



}

void energi()
{
    sensors.requestTemperatures();
  energiSicaklik = sensors.getTempCByIndex(0);
}
void kombi()
{
  
 
if(sabit>50)
{
  sabit=24; // esp ilk çalıştığında atanan sıcaklık değeri
}
          set = sabit * 10;

  KombiDurum = digitalRead(kombiPin);

  float sicaklik = temp;

  Serial.print("kombi= ");
  Serial.println(KombiDurum);
 

  if ( KombiDurum == 0)
  {
    if (temp <= 500)
    {
      if (temp >= set) //Kombinin çalışmayı durduracağı sıcaklık değeri
      {

        digitalWrite(relay, LOW);
        yesil_blynk.off();
        
      }
      if (temp <= (set - 5.00)) //Kombinin çalışmaya başlayacağı sıcaklık değeri
      {
        digitalWrite(relay, HIGH);
        yesil_blynk.on();
       
      }
    }
  }
  if (KombiDurum == 1)
  {

    digitalWrite(relay, LOW);
    yesil_blynk.off();
    
  }
}
void ekran()
{
  Serial.print("sicaklik= ");
  Serial.println(temp);
  Serial.print("batarya= ");
  Serial.println(bat);
  Serial.print("nem= ");
  Serial.println(hum);
  Serial.print("seTemp= ");
  Serial.println(seTemp);  
  Serial.print("SABIT= ");
  Serial.println(sabit);
  Serial.print("energi= ");
  Serial.println(energiSicaklik);


  if (temp < 500)
  {
    Blynk.virtualWrite(V1, (float(temp) / 10));
  }
  if (hum < 1000)
  {
    Blynk.virtualWrite(V2, hum / 10);
  }
  Blynk.virtualWrite(V3, energiSicaklik);
  Blynk.virtualWrite(V6, sabit);
}

BLYNK_WRITE_DEFAULT() {
  Serial.print("input V");
  Serial.print(request.pin);
  Serial.println(":");
  // Print all parameter values
  for (auto i = param.begin(); i < param.end(); ++i) {

    Serial.print("* ");
    Serial.println(i.asString());
//    a = i.asString();
//    seTemp = a.toInt();

    if (request.pin == 5)
    {
      a = i.asString();
      seTemp = a.toInt();
    }
  }

}

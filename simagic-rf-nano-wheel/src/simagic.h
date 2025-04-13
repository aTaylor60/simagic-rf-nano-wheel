// talk to Simagic wheelbase wirelessly using nRF24L01
// Bassed on work by Keijo 'Kegetys' Ruotsalainen, www.kegetys.fi
// Modified for updated(?) packet structure used on the Simagic Alpha wheelbase by Andrew 'aTaylor60' Taylor

// uncomment this if you are usin an RF24 clone to disable ACKs and instead send multiple messages for redundancy
#define SIMAGIC_NO_ACKS

//#define DUMP_SENT
//#define DUMP_RECEIVED

#include <SPI.h>
#include <RF24.h>

#define SIMAGIC_ADDRESS   0x0110104334LL // network ID used by simagic
#define SIMAGIC_REPEAT    4 // how many times non-keepalive messages are repeated. hack for non-ack transmission

uint8_t crc8(const uint8_t *addr, uint8_t len);
  
class simagic
{
public:
  // init nRF24 chip, need CE and CS pins as parameters.
  // SPI used so CSN, MOSI, MISO and SCK must be wired to HW pins (IRQ not needed)
  // chan parameter is the channel number the base is set to (check SimPro manager)
  simagic(byte pinCe, byte pinCs, byte chan) : _pinCe(pinCe), _pinCs(pinCs), _chan(chan) {};

  void begin()
  {
    _radio.begin(_pinCe, _pinCs);
    _radio.setPALevel(RF24_PA_MAX); // consider lowering this, maximum can affect WiFi nearby
    _radio.setDataRate(RF24_250KBPS);
    _radio.setCRCLength(RF24_CRC_8);
    _radio.setPayloadSize(sizeof(packetTx)); // redundant?
    _radio.setChannel(_chan);
#ifdef SIMAGIC_NO_ACKS    
    _radio.setAutoAck(0, false);
#else
    _radio.setAutoAck(0, true);
    _radio.setRetries(5, 5);
#endif
    _radio.setRetries(0, 0);
    _radio.enableAckPayload();
    _radio.enableDynamicPayloads();
    _radio.openReadingPipe(0, SIMAGIC_ADDRESS);
    _radio.openWritingPipe(SIMAGIC_ADDRESS);

    // allow the radio to settle
    delay(250);
    _radio.printDetails();

    if (!_radio.isChipConnected())
      Serial.println("No connection!");
    else
      Serial.println("nRF24 connected");

    _lastKeepalive = 0;
    _updateNeeded = true;
    memset(_axes, 0, numAxes*2); //Axis now 2 byte value to capture 12bits
  }

  // sends button and axis states to the base
  // must be called periodically or the rim is considered disconnected from the base
  void tick()
  {
    if (!_updateNeeded)
    {
      // no update, send keepalive if needed
      if (millis() - _lastKeepalive > keepaliveRate)
      {
        // send state as keepalive
        sendState(0x00);
        _lastKeepalive = millis();
      }
    }
    else
    {
      // button or axis state has changed, send new state
      sendState(SIMAGIC_REPEAT);

      _updateNeeded = false;
      _lastKeepalive = millis();  
    }
  }

  // Register button changes and notify need to update base
  // TODO: probably can have more than 32 buttons too? Bigger packet size?
  void setButtonBits(uint32_t buttons)
  {
    if (buttons == _buttons)
      return;
    _buttons = buttons;  
    _updateNeeded = true;
  }

  // Set axis state, value range is 0 - 4095, and notify need to update base
  void setAxis(int axis, uint16_t value)
  {
    if (_axes[axis] == value)
      return;
    _axes[axis] = value;
    _updateNeeded = true;
  }

  /* Organise data according to data structure
  Byte 1 = Button 1-8
  Byte 2 = Button 9-16
  Byte 3 = Axis 1 [0-7]
  Byte 4 = Axis 1 [9-12] & Axis 2 [0-3]
  Byte 5 = Axis 2 [4-12]
  Byte 6 = Button 25-32
  Byte 7 = Button 17-24

  Axis values are 12bit across 3 bytes */
  void sendState(int repeat)
  {
    sendRaw(
      (uint32_t)_axes[0] << 28 | ((uint32_t)_axes[1] << 16) | (_buttons & 0xFFFF), 
      (_buttons & 0xFF000000) >> 16 | (_buttons & 0x00FF0000) | (uint32_t)_axes[0] >> 4,
      SIMAGIC_REPEAT);
  }
  
  // send raw data. last byte of b is overriden with CRC
  void sendRaw(uint32_t a, uint32_t b, int repeat)
  {
    packetTx m;
    m.a = a;
    m.b = b;
  
    // calculate crc, ignoring last byte
    const uint8_t crc = crc8((const uint8_t*) &m, sizeof(packetTx) - 1);
    // replace last byte with crc
    m.b = (m.b & (uint32_t)0x00FFFFFF) | ((uint32_t) crc << 24);

#ifdef SIMAGIC_NO_ACKS
    // if no ACKs then repeat important messages multiple times
    for (int i = 0; i < repeat; i++)
      _radio.write(&m, sizeof(packetTx));
#else
    // real nRF24 chip can receive ACKs
    const bool ok = _radio.write(&m, sizeof(packetTx));
    if (!ok)
      Serial.println("transmit failed!");
    else
    {
      // read ACK payload
      if (_radio.available())
      {
        const int packetLen = _radio.getPayloadSize();
        uint8_t buf[8];
        _radio.read(buf, min(8, packetLen));

#ifdef DUMP_RECEIVED   
        char buffer[32];
        for (int i = 0; i < packetLen; i++) {
          sprintf(buffer, "%02x", buf[i]);
          Serial.print(buffer);
        }
        Serial.println(" RECV");
#endif
      }      
    }
#endif // SIMAGIC_NO_ACKS
  
#ifdef DUMP_SENT
    char buffer[32];
    for (int i = 0; i < 8; i++) {
      sprintf(buffer + i * 2, "%02x", ((const uint8_t*)&m)[i]);
    }
    Serial.print(buffer);
    Serial.println(" SENT");
#endif
  }  
  
private:
  const static int keepaliveRate = 2; // ms between keepalive packets. the GTS rim seems to send packets at 500Hz
  const static int numAxes = 2;

  byte _pinCe, _pinCs, _chan;
  RF24 _radio;

  uint32_t _buttons; // current button state
  uint16_t _axes[numAxes];    // axis data (12 bits)
  bool _updateNeeded;     // if button/axis state changed and update is needed in tick()
  uint32_t _lastKeepalive; // last millis() keepalive was sent
  
  // buffer used to talk to the base
  struct packetTx
  {
    uint32_t a;
    uint32_t b;
  };
};

// CRC-8/MAXIM stolen from OneWire library by Jim Studt
static const uint8_t PROGMEM simagic_dscrc2x16_table[] = {
  0x00, 0x5E, 0xBC, 0xE2, 0x61, 0x3F, 0xDD, 0x83,
  0xC2, 0x9C, 0x7E, 0x20, 0xA3, 0xFD, 0x1F, 0x41,
  0x00, 0x9D, 0x23, 0xBE, 0x46, 0xDB, 0x65, 0xF8,
  0x8C, 0x11, 0xAF, 0x32, 0xCA, 0x57, 0xE9, 0x74
};

uint8_t crc8(const uint8_t *addr, uint8_t len)
{
  uint8_t crc = 0;
  while (len--) {
    crc = *addr++ ^ crc;  // just re-using crc as intermediate
    crc = pgm_read_byte(simagic_dscrc2x16_table + (crc & 0x0f)) ^
    pgm_read_byte(simagic_dscrc2x16_table + 16 + ((crc >> 4) & 0x0f));
  }
  return crc;
}  
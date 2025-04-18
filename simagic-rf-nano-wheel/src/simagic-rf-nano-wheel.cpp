// Functinoal code to send button and axis data to simagic wheel bases
// Based on work by Keijo 'Kegetys' Ruotsalainen, www.kegetys.fi
// Modified for 12-bit analog packet structure used on the Simagic Alpha wheelbase by Andrew 'aTaylor60' Taylor

#include <Arduino.h>
#include <simagic.h>
#include <EEPROM.h>

#define PIN_SIMAGIC_CE  7
#define PIN_SIMAGIC_CS  8
#define SIMAGIC_CHANNEL 80 // channel configured in SimPro manager

//#define CALIBRATION_DEBUG
//#define ANALOG_DEBUG

simagic base(PIN_SIMAGIC_CE, PIN_SIMAGIC_CS, SIMAGIC_CHANNEL);

const uint8_t switchPins[] = {2, 3, 4, 5, 6, 9};
const uint8_t numButtons = 6;
const uint8_t axisPins[] = {15, 16};
const uint8_t numAxis = 2;
const uint8_t analogSamples = 6;  //Number of times the analog inputs are read and averaged per loop
const uint8_t deadZone = 1; //Deadzone as a percentage of range applied to calibration at each end limit

uint16_t ewmaFilter (uint8_t i, uint32_t input);
const uint32_t alpha = 10;
const unsigned int alphaScale = 40;
uint32_t outputScaled[numAxis];
bool hasInitial[numAxis] = {false};


struct axisCal
  {
    bool calibrated;
    uint16_t min;
    uint16_t max;
  };

axisCal c[numAxis];

//#include "initpacket.h"

void setup()
{
  Serial.begin(115200);
  delay(100);
  base.begin();

  Serial.println("simagic-rf-nano-wheel by a.taylor");

  for(int i : switchPins)
  {
    pinMode(i, INPUT_PULLUP);
  }
  analogReference(EXTERNAL);
  for(int i : axisPins)
  {
    pinMode(i, INPUT_PULLUP);
  }

  //Clear calibration if button 2 is held during startup
  if (!digitalRead(switchPins[4]))
  {
    for (int i = 0; i < numAxis; i++)
    {
      c[i].calibrated = false;
      c[i].max = 0;
      c[i].min = 0;
      EEPROM.put(i * sizeof(axisCal), c[i]);
    }
    #ifdef CALIBRATION_DEBUG 
      Serial.println("Calibration cleared");
    #endif
  }

  //Enter axis calibration mode if button 0 is held at startup
  if (!digitalRead(switchPins[3]))
  {
    uint16_t axis[numAxis] = {0};

    //Do an initial read to set max and min out of range
    for (int i = 0; i < numAxis; i++)
    {
      axis[i] = analogRead(axisPins[i]);
      c[i].max = axis[i]*0.95;
      c[i].min = axis[i]*1.05;
    }

    //Loop until button 1 is pressed
    while (digitalRead(switchPins[5]))
    {
      for (int i = 0; i < numAxis; i++)
      {
        axis[i] = analogRead(axisPins[i]);
        if (axis[i] > c[i].max)
        {
          c[i].max = axis[i];
        }
        if (axis[i] < c[i].min)
        {
          c[i].min = axis[i];
        }
        #ifdef CALIBRATION_DEBUG
          //Stream calibration values
          Serial.print(axis[i]);
          Serial.print("\t");
          Serial.print(c[i].min);
          Serial.print("\t");
          Serial.print(c[i].max);
          if (i + 1 == numAxis) Serial.println();
          else Serial.print("\t");
        #endif
      }
    }
    //Verify some sort of calibration has been reached by checking max has is higher than min and apply dead zone
    for (int i = 0; i < numAxis; i++)
    {
      if (c[i].max > c[i].min)
      {
        c[i].calibrated = true;
        int range = c[i].max - c[i].min;
        int deadZoneAdujust = range * deadZone / 100;
        c[i].max -= deadZoneAdujust;
        c[i].min += deadZoneAdujust;
        
        #ifdef CALIBRATION_DEBUG 
        //Report calibration results
        Serial.print("Axis ");
        Serial.print(i);
        Serial.print(" calibrated to min:");
        Serial.print(c[i].min);
        Serial.print(" max:");
        Serial.println(c[i].max);
        #endif
      }     
    }

    //Save calibration data to EEPROM
    for (int i = 0; i < numAxis; i++)
    {
      EEPROM.put(i * sizeof(axisCal), c[i]);
    }

  } else {
    //Read calibration data from EEPROM
    for (int i = 0; i < numAxis; i++)
    {
      EEPROM.get(i * sizeof(axisCal), c[i]);
    }
  }
  

  // uncomment this and the include above to send the rim identification packet
  // you should see the rim appear in SimPro manager then.
  // The Buttons and axes however work even without doing this, you just won't see a rim in the manager.
  //sendRimInit();
}

void loop()
{
  // poll button states
  bool state[numButtons] = {0};
  for (int i = 0; i < numButtons; i++)
  {
    state[i] = !digitalRead(switchPins[i]);
  }
  
  // convert Buttons to bits
  uint32_t bits = 0;
  for (int i = 0; i < numButtons; i++)
  {
    if (state[i])
    {
      bits |= (uint32_t)1 << i;
    }
  }

  //send buttons
  base.setButtonBits(bits);
  
  //Read axis data
  uint16_t axis[numAxis] = {0};
  
  
  for (int i = 0; i < numAxis; i++)
  {
    uint16_t raw = 0;
    for (int j = 0; j < analogSamples; j++)
    {
      raw += analogRead(axisPins[i]);
    }
    raw /= analogSamples;
    axis[i] = ewmaFilter(i, raw);
    #ifdef ANALOG_DEBUG
      Serial.print(raw);
      Serial.print("\t");
      Serial.print(axis[i]);
      if (i + 1 == numAxis) Serial.println();
      else Serial.print("\t");
    #endif
    axis[i] = constrain(axis[i], c[i].min, c[i].max);
    axis[i] = map(axis[i], c[i].min, c[i].max, 0, 4095);
    base.setAxis(i, axis[i]);
  }

  base.tick();

  delayMicroseconds(100);
}

uint16_t ewmaFilter (uint8_t i, uint32_t input)
{
    if (hasInitial[i]) {
        outputScaled[i] = alpha * input + (alphaScale - alpha) * outputScaled[i] / alphaScale;
    } else {
        outputScaled[i] = input * alphaScale;
        hasInitial[i] = true;
    }
    return (outputScaled[i] + alphaScale / 2) / alphaScale;
}
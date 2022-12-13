#include<Arduino.h>
#include <U8g2lib.h>
//https://registry.platformio.org/libraries/olikraus/U8g2/installation

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

U8G2_SSD1306_128X64_ALT0_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE); 

void setup() {

    Serial.begin(115200);
    pinMode(D5, OUTPUT); 

    u8g2.begin();
}

void loop() {

    Serial.println("Hello world!");
    digitalWrite(D5, HIGH);
    delay(1000);
    digitalWrite(D5, LOW);
    delay(1000);
    u8g2.clearBuffer();                   // clear the internal memory
    u8g2.setFont(u8g2_font_ncenB08_tr);   // choose a suitable font
    u8g2.drawStr(0,10,"Hello World!");    // write something to the internal memory
    u8g2.sendBuffer();                    // transfer internal memory to the display
    delay(1000);  
    
}


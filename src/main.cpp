#include<Arduino.h>

void setup() {
  // put your setup code here, to run once:
    Serial.begin(115200);
    pinMode(D5, OUTPUT); //Set pin 13 as an 'output' pin as we will make it output a voltage.
    // digitalWrite(13, HIGH); //This turns on pin 13/supplies it with 3.3 Volts.

    // pinMode(11, OUTPUT);
    // analogWrite(11, 127); 
    // pinMode(11, INPUT);
}

void loop() {
    // put your main code here, to run repeatedly:
    Serial.println("Hello world!");
    digitalWrite(D5, HIGH); //Step 1: The LED Turns on.
    delay(1000);
    digitalWrite(D5, LOW);
    delay(1000);
    // int tstate = digitalRead(11);
}


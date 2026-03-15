#include <avr/sleep.h>
#include <avr/power.h>
unsigned long tmr2 = 0; 
int count = 0; 

void setup() {
  pinMode(PB3, INPUT_PULLUP);
  pinMode(PB4, INPUT_PULLUP);
  pinMode(PB0, OUTPUT);
  pinMode(PB1, OUTPUT);
  pinMode(PB2, OUTPUT);
  for (int i=0; i < 3; i++){
  digitalWrite(PB0,1);
  delay(100);
  digitalWrite(PB0,0);
  delay(100);
  }
  while (millis() < 5000){
   if ((analogRead(A3) < 100 or analogRead(A2) < 100) and millis()-tmr2 > 100){
      tmr2 = millis();
      count++;
   }
  }
  //если 2 срабатывания, значит только 1 магнитная головка - зажигает зеленый светодиод
  if (count == 2){
   digitalWrite(PB2,1);
   delay(1000);
   digitalWrite(PB2,0);
  }
  //если 4 срабатывания, значит 2 магнитные головки - зажигает желтый светодиод
  if (count == 4){
   digitalWrite(PB1,1);
   digitalWrite(PB2,1);
   delay(1000);
   digitalWrite(PB1,0);
   digitalWrite(PB2,0);
  }
  //если срабатываний больше 4, магнитных головок больше 2 - зажигает красный светодиод
  if (count > 4){
   digitalWrite(PB1,1);
   delay(1000);
   digitalWrite(PB1,0);
  }
  //если срабатываний меньше 2, магнитная головка отсутствует - моргает поочередно всеми цветами
  if (count < 2){
  digitalWrite(PB0,1);
  delay(100);
  digitalWrite(PB0,0);
  delay(100);
  digitalWrite(PB1,1);
  delay(100);
  digitalWrite(PB1,0);
  delay(100);
  digitalWrite(PB2,1);
  delay(100);
  digitalWrite(PB2,0);
  delay(100);
  }
  //Спящий режим
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); 
  sleep_enable();
  power_all_disable();
  sleep_cpu();
  sleep_disable();
}
void loop() {
 }
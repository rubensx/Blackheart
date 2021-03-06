#include <EEPROM.h>
#include <U8x8lib.h>
#include "Adafruit_MCP23017.h"
#include "MultiMap.h"
#include <Wire.h>
#include <OneWire.h>

/******************************
 * 
 *  Definition section
 *  
 ******************************/

U8X8_SSD1327_MIDAS_128X128_4W_SW_SPI u8x8(12, 13, 11, 10, 9);                       // u8x8(clock, data, cs, dc, reset)
Adafruit_MCP23017 mcp;                                                              // MCP23017 Port Expander

byte ACC = 4, VOLT = 14, FUEL = 15, TEMP = 16, OIL = 17,  DALLAS = 8;               // Pin assignment

int tmI[] = {  0, 10, 20, 30,40,50,60,70,80,90,100,140,200,288,354};                // Temperature sensor values
int tmO[] = {130,120,110,100,90,80,70,65,62,60, 55, 39, 30, 13,  0};                // Temperature real values

const int oi_num = 10, fu_num = 50, tm_num = 50;                                    // Average constants:
int oi_buf[oi_num], oi_idx = 0;
int fu_buf[fu_num], fu_idx = 0;
int tm_buf[tm_num], tm_idx = 0, tm = 0;
long oi_tot = 0, fu_tot = 0, tm_tot = 0, fu = 0, oi= 0;
volatile unsigned long   micros_sp = 0, micros_th = 0, millis_ds = 0, millis_t  = 0, millis_d  = 0, millis_fu  = 0, millis_vo  = 0, millis_tm  = 0, millis_oi  = 0; // Timers
volatile boolean st = false, tt = false;                                            // Triggers 
volatile byte sz = 0, tz = 0;                                                       // Reset counters
volatile unsigned int sp = 0, th = 0, vo = 0, ds_tm = 0;
unsigned long trip1, trip1_old, trip2;
int pin = 0;
String  mcparray;
OneWire ds(DALLAS);                                                                 // Temperature sensor DS18B20 port

/******************************
 * 
 *  Setup section
 *  
 ******************************/
 
void setup(){
    Serial.begin(115200);
    attachInterrupt(0, spd, FALLING);                                               // Speedometer falling interrupt INT0
    attachInterrupt(1, tah, RISING);                                                // Tahometer rising interrupt INT1
    mcp.begin(7);                                                                   // addr 7 = A2 high, A1 high, A0 high 111
    for (pin = 0; pin  < 16; pin ++) { mcp.pinMode(pin, INPUT); }                   // MCP23017 readonly init
    trip1 = EEPROM_ulong_read(0);                                                   // Read Trip1 from EEPROM
    trip2 = EEPROM_ulong_read(1);                                                   // Read Trip2 from EEPROM
    trip1_old = trip1;                                                              // Write EEPROM if it changed
    
    initOLED();

    for (int idx = 0; idx < oi_num; idx++) oi_buf[idx] = 0;                         // Buffer reset for oil 
    for (int idx = 0; idx < fu_num; idx++) fu_buf[idx] = 0;                         // Buffer reset for fuel 
    for (int idx = 0; idx < tm_num; idx++) tm_buf[idx] = 0;                         // Buffer reset for temperature
}

/******************************
 * 
 *  Main loop section
 *
 ******************************/
 
void loop(){
    DallasRd();
    if(((millis() - millis_t) >= 20) && digitalRead(ACC)){                          // Serial refresh interval in milliseconds if ACC present
        millis_t = millis();

        if ((millis() - millis_fu)  >= 20) {                                        // Fuel refresh interval in milliseconds (Full - 70~73 / Empty - ?)
            millis_fu = millis();
            fu = constrain(analogRead(FUEL), 70, 200);
            fu_tot -= fu_buf[fu_idx];                                               // Average fuel buffer
            fu_buf[fu_idx] = fu; 
            fu_tot += fu_buf[fu_idx];
            fu_idx++; if (fu_idx >= fu_num) fu_idx = 0; 
            fu = fu_tot / fu_num;
            fu = map(fu, 70, 200, 100, 0);
            fu = constrain(fu, 0, 100);
        }

        if ((millis() - millis_vo)  >= 500) {                                       // Voltage refresh interval in milliseconds
            millis_vo = millis();
            vo = analogRead(VOLT);
        }

        if ((millis() - millis_tm)  >= 20) {                                        // Temperature refresh interval in milliseconds
            millis_tm = millis();
            tm = analogRead(TEMP);
            tm_tot -= tm_buf[tm_idx];                                               // Average temperature buffer
            tm_buf[tm_idx] = tm; 
            tm_tot += tm_buf[tm_idx];
            tm_idx++; if (tm_idx >= tm_num) tm_idx = 0; 
            tm = tm_tot / tm_num;        
            tm = constrain(multiMap(tm, tmI, tmO, 15), 0, 120);            
        }

        if ((millis() - millis_oi)  >= 10) {                                      // Oil pressure refresh interval in milliseconds
            millis_oi = millis();
            //oi = map(analogRead(OIL), 0, 730, 100, 0);
            oi = analogRead(OIL);
            oi_tot -= oi_buf[oi_idx];                                               // Average oil buffer
            oi_buf[oi_idx] = oi;
            oi_tot += oi_buf[oi_idx];
            oi_idx++; if (oi_idx >= oi_num) oi_idx = 0;
            oi = oi_tot / oi_num;
            oi = map(constrain(oi, 0, 200), 0, 200, 1000, 0);

        }

        mcparray = "";
        for (pin = 0; pin  < 16; pin ++) { mcparray += ","; mcparray += mcp.digitalRead(pin); }
        
        Serial.print(az(sp, 3) + "," + az(th, 4) + "," + az(vo, 4) + "," + az(fu, 3) + "," + tm + "," + az(ds_tm, 3) + "," + az(oi, 4) + mcparray + "," + trip1 + "," + trip2 +"\n");
                
        Serial.flush();
        if(tz != 0){tz--;}else{th = 0;}; 
        if(sz != 0){sz--;}else{sp = 0;};
    } 

    if(((millis() - millis_d) >= 100) && digitalRead(ACC)){                         // OLED refresh interval in milliseconds if ACC present
        millis_d = millis();
        drawOLED();
    }
    
    if (!digitalRead(ACC) && trip1 > trip1_old) {                                   // Write EEPROM if not ACC present
        //EEPROM_ulong_write(0, trip1);
        //EEPROM_ulong_write(1, trip2);
        //delay(2000);
    }
}

/******************************
 * 
 *  Functions section  
 *  
 ******************************/
 
void spd() {                                                                        // Speedometer interupt
    if (!st) { micros_sp = micros(); }
    else { sp = 3600000 / 4 / (micros() - micros_sp); }                             // Sensor pulses per rotation
    st = !st; sz = 30;
    trip1 ++; trip2 ++;
}

void tah() {                                                                        // Tahometer interrupt
    if (!tt) { micros_th = micros(); }
    else { th = 30000000 / (micros() - micros_th) * 2; }
    tt = !tt; tz = 10;
}

unsigned long EEPROM_ulong_read(int addr) {                                         // Write EEPROM
    byte raw[4];
    for(byte i = 0; i < 4; i++) raw[i] = EEPROM.read(addr+i);
    unsigned long &num = (unsigned long&)raw;
    return num;
}

void EEPROM_ulong_write(int addr, unsigned long num) {                              // Read EEPROM
    byte raw[4];
    (unsigned long&)raw = num;
    for(byte i = 0; i < 4; i++) EEPROM.write(addr+i, raw[i]);
}

int DallasRd(){                                                                     // Read DS18B20 temperature sensor 
    byte data[2];
    if ((millis() - millis_ds) > 1000){                                             // DS18B20 read interval in milliseconds
        ds.reset(); ds.write(0xCC); ds.write(0xBE);
        data[0] = ds.read(); data[1] = ds.read();
        ds_tm = (data[1] << 8) + data[0];
        ds_tm = (ds_tm >> 4) + 200;
        ds_tm = constrain(ds_tm, 0, 296);
        millis_ds = millis();
        ds.reset(); ds.write(0xCC); ds.write(0x44);                                 // 2 wire connection - (0x44,1) / 3 wire connection (0x44)
    }
}

void initOLED() {                                                                   // Initialize OLED and draw titles
    char * txtOLED[16] = { "Blackheart Board", "", "Speed :", "  RPM :", "", "Volts :", "  Oil :", " Fuel :", "TempA :", "TempB :", "", "TripA :",  "TripB :", "", "PortA :", "PortB :" };
    u8x8.begin();
    u8x8.setPowerSave(0);
    u8x8.setFont(u8x8_font_victoriabold8_r);
    u8x8.setInverseFont(1);
    u8x8.drawString(0,  0,txtOLED[0]);
    u8x8.setFont(u8x8_font_chroma48medium8_r);
    u8x8.setInverseFont(0);
    for (pin = 1; pin  < 16; pin ++) u8x8.drawString(0, pin, txtOLED[pin]);

}

void drawOLED() {                                                                   // Redraw OLED
    u8x8.setCursor(8, 2); u8x8.print(az(sp, 3));
    u8x8.setCursor(8, 3); u8x8.print(az(th, 4));
    u8x8.setCursor(8, 5); u8x8.print(vo * (15.800 / 1024));
    u8x8.setCursor(8, 6); u8x8.print(az(analogRead(OIL), 4));
    u8x8.setCursor(8, 7); u8x8.print(az(analogRead(FUEL), 4));
    u8x8.setCursor(8, 8); u8x8.print(az(analogRead(TEMP), 4));
    u8x8.setCursor(8, 9); if (ds_tm < 296) u8x8.print(az(ds_tm, 3)); else u8x8.print("   ");
    u8x8.setCursor(8, 11); u8x8.print(trip1);
    u8x8.setCursor(8, 12); u8x8.print(trip2);
    mcparray = "";
    for (pin=0; pin  < 8; pin ++)  mcparray += mcp.digitalRead(pin);
    u8x8.setCursor(8, 14); u8x8.print(mcparray);
    mcparray = "";
    for (pin=8; pin  < 16; pin ++)  mcparray += mcp.digitalRead(pin);
    u8x8.setCursor(8, 15); u8x8.print(mcparray);
}

String az(const int& src, int num) {                                                // Adding zeroes
    String result = "";
    if (num >  3) result += (src/1000)  % 10; 
    if (num >  2) result += (src/100)   % 10; 
    if (num >  1) result += (src/10)    % 10; 
    if (num >  0) result += (src)       % 10;
    if (num == 0) result += src;
    return result;
}

/* OpenGarage Firmware
 *
 * OpenGarage library
 * Mar 2016 @ OpenGarage.io
 *
 * This file is part of the OpenGarage library
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include "OpenGarage.h"

byte  OpenGarage::state = OG_STATE_INITIAL;
File  OpenGarage::log_file;
byte  OpenGarage::alarm = 0;
byte  OpenGarage::led_reverse = 0;
byte  OpenGarage::dirty_bits = 0xFF;
Ticker ud_ticker;


static const char* config_fname = CONFIG_FNAME;
static const char* log_fname = LOG_FNAME;

OneWire* OpenGarage::oneWire = NULL;
DallasTemperature* OpenGarage::ds18b20 = NULL;
AM2320* OpenGarage::am2320 = NULL;
DHTesp* OpenGarage::dht = NULL;
extern OpenGarage og;
/* Options name, default integer value, max value, default string value
 * Integer options don't have string value
 * String options don't have integer or max value
 */
OptionStruct OpenGarage::options[] = {
  {"fwv", OG_FWV,      255, ""},
  {"sn2", OG_SN2_NONE,   2, ""},
  {"riv", 5,           300, ""},
  {"alm", OG_ALM_5,      2, ""},
  {"aoo", 0,             1, ""},
  {"lsz", DEFAULT_LOG_SIZE,400,""},
  {"tsn", OG_TSN_NONE, 255, ""},
  {"htp", 80,        65535, ""},
  {"cdt", 1000,       5000, ""},
  {"dri", 500,        3000, ""},
  {"sfi", OG_SFI_CONSENSUS,1,""},
  {"cmr", 10,          100, ""},
  {"sto", 0,             1, ""},
  {"mod", OG_MOD_AP,   255, ""},
  {"ati", 30,          720, ""},
  {"ato", OG_AUTO_NONE,255, ""},
  {"atib", 3,           24, ""},
  {"atob", OG_AUTO_NONE,255, ""},
  {"noto", OG_NOTIFY_DO|OG_NOTIFY_DC,255, ""},
  {"usi", 0,             1, ""},
  {"ssid", 0, 0, ""},  // string options have 0 max value
  {"pass", 0, 0, ""},
  {"auth", 0, 0, ""},
  {"bdmn", 0, 0, "blynk-cloud.com"},
  {"bprt", 80,65535, ""},
  {"dkey", 0, 0, DEFAULT_DKEY},
  {"name", 0, 0, DEFAULT_NAME},
  {"iftt", 0, 0, ""},
  {"mqtt", 0, 0, ""},
  {"mqpt", 1883, 65535, ""},
  {"mqur", 0, 0, ""},
  {"mqpw", 0, 0, ""},
  {"mqtp", 0, 0, ""},
  {"dvip", 0, 0, ""},
  {"gwip", 0, 0, ""},
  {"subn", 0, 0, "255.255.255.0"},
  {"dns1", 0, 0, "8.8.8.8"},
  {"ntp1", 0, 0, ""},
  {"host", 0, 0, ""},
};

    
void OpenGarage::begin() {
  digitalWrite(PIN_RESET, HIGH);
  pinMode(PIN_RESET, OUTPUT);
  
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_BUZZER, OUTPUT);
  
  digitalWrite(PIN_RELAY, LOW);
  pinMode(PIN_RELAY, OUTPUT);

  // detect LED logic
  pinMode(PIN_LED, INPUT);
  // use median filtering to detect led logic
  byte nl=0, nh=0;
  for(byte i=0;i<KAVG;i++) {
    if(digitalRead(PIN_LED)==HIGH) nh++;
    else nl++;
    delay(50);
  }
  if(nh>nl) { // if we get more HIGH readings
    led_reverse = 1;  // if no external LED connected, reverse logic
    //Serial.println(F("reverse logic"));
  } else {
    //Serial.println(F("normal logic"));
  }

  pinMode(PIN_LED, OUTPUT);
  set_led(LOW);
  
  digitalWrite(PIN_TRIG, HIGH);
  pinMode(PIN_TRIG, OUTPUT);
  
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  pinMode(PIN_SWITCH, INPUT_PULLUP);
  
  state = OG_STATE_INITIAL;
  
  if(!FILESYS.begin()) {
    DEBUG_PRINTLN(F("failed to mount file system!"));
  }
}

void OpenGarage::options_setup() {
  int i;
  if(!FILESYS.exists(config_fname)) { // if config file does not exist
  	DEBUG_PRINTLN(F("create config file"));
    options_save(); // save default option values
    return;
  }
  DEBUG_PRINTLN(F("load options"));
  options_load();
  DEBUG_PRINTLN(F("done"));
  
  if(options[OPTION_FWV].ival != OG_FWV)  {
    // if firmware version has changed
    // re-save options, thus preserving
    // shared options with previous firmwares
    options[OPTION_FWV].ival = OG_FWV;
    options_save();
    return;
  }
}

void OpenGarage::options_reset() {
  DEBUG_PRINT(F("reset to factory default..."));
  if(!FILESYS.remove(config_fname)) {
    DEBUG_PRINTLN(F("failed to remove config file"));
    return;
  }else{DEBUG_PRINTLN(F("Removed config file"));}
  DEBUG_PRINTLN(F("ok"));
}

void OpenGarage::log_reset() {
  if(!FILESYS.remove(log_fname)) {
    DEBUG_PRINTLN(F("failed to remove log file"));
    return;
  }else{DEBUG_PRINTLN(F("Removed log file"));}
  DEBUG_PRINTLN(F("ok"));  
}

int OpenGarage::find_option(String name) {
  for(byte i=0;i<NUM_OPTIONS;i++) {
    if(name == options[i].name) {
      return i;
    }
  }
  return -1;
}

void OpenGarage::options_load() {
  File file = FILESYS.open(config_fname, "r");
  DEBUG_PRINT(F("loading config file..."));
  if(!file) {
    DEBUG_PRINTLN(F("failed"));
    return;
  }
  byte nopts = 0;
  while(file.available()) {
    String name = file.readStringUntil(':');
    String sval = file.readStringUntil('\n');
    sval.trim();
    DEBUG_PRINT(name);
    DEBUG_PRINT(":");
    DEBUG_PRINT(sval);
    DEBUG_PRINT(F(" | "));
    nopts++;
    if(nopts>NUM_OPTIONS+1) break;
    int idx = find_option(name);
    if(idx<0) continue;
    if(options[idx].max) {  // this is an integer option
      options[idx].ival = sval.toInt();
    } else {  // this is a string option
      options[idx].sval = sval;
    }
  }
  file.close();
}

void OpenGarage::options_save() {
  File file = FILESYS.open(config_fname, "w");
  DEBUG_PRINTLN(F("saving config file..."));  
  if(!file) {
    DEBUG_PRINTLN(F("failed"));
    return;
  }
  OptionStruct *o = options;
  for(byte i=0;i<NUM_OPTIONS;i++,o++) {
    file.print(o->name + ":");
    if(o->max)
      file.println(o->ival);
    else
      file.println(o->sval);
  }
  DEBUG_PRINTLN(F("ok"));  
  file.close();
  set_dirty_bit(DIRTY_BIT_JO, 1);
}


void OpenGarage::init_sensors() {
  switch(options[OPTION_TSN].ival) {
  case OG_TSN_AM2320:
    am2320 = new AM2320();
    am2320->begin();
    break;
  case OG_TSN_DHT11:
    dht = new DHTesp();
    dht->setup(PIN_TH, DHTesp::DHT11);
    break;
  case OG_TSN_DHT22:
    dht = new DHTesp();
    dht->setup(PIN_TH, DHTesp::DHT22);    
    break;

  case OG_TSN_DS18B20:
    oneWire = new OneWire(PIN_TH);
    ds18b20 = new DallasTemperature(oneWire);
    ds18b20->begin();
    break;
  }
}

void OpenGarage::read_TH_sensor(float& C, float& H) {
	float v;
  switch(options[OPTION_TSN].ival) {
  case OG_TSN_AM2320:
    if(am2320) {
      if(am2320->measure()) {
      	v = am2320->getTemperature();
      	if(!isnan(v)) C=v;
        v = am2320->getHumidity();
        if(!isnan(v)) H=v;
      }
    }
    break;
  case OG_TSN_DHT11:
  case OG_TSN_DHT22:
    if(dht) {
      TempAndHumidity th = dht->getTempAndHumidity();
      v = th.temperature;
      if(!isnan(v)) C=v;
      v = th.humidity;
      if(!isnan(v)) H=v;
    }
    break;

  case OG_TSN_DS18B20:
    if(ds18b20) {
      ds18b20->requestTemperatures();
      v=ds18b20->getTempCByIndex(0);
      if(!isnan(v)) C=v;
    }
    break;
  }
}

bool OpenGarage::get_cloud_access_en() {
  if(options[OPTION_AUTH].sval.length()==32) {
    return true;
  }
  return false;
}

void OpenGarage::write_log(const LogStruct& data) {
  File file;
  uint curr = 0;
  DEBUG_PRINTLN(F("saving log data..."));  
  if(!FILESYS.exists(log_fname)) {  // create log file
    file = FILESYS.open(log_fname, "w");
    if(!file) {
      DEBUG_PRINTLN(F("failed"));
      return;
    }
    // fill log file
    uint next = curr+1;
    file.write((const byte*)&next, sizeof(next));
    file.write((const byte*)&data, sizeof(LogStruct));
    LogStruct l;
    l.tstamp = 0;
    for(;next<MAX_LOG_SIZE;next++) {  // pre-fill the log file to maximum size
      file.write((const byte*)&l, sizeof(LogStruct));
    }
  } else {
    file = FILESYS.open(log_fname, "r+");
    if(!file) {
      DEBUG_PRINTLN(F("failed"));
      return;
    }
    file.readBytes((char*)&curr, sizeof(curr));
    uint next = (curr+1) % options[OPTION_LSZ].ival;
    file.seek(0, SeekSet);
    file.write((const byte*)&next, sizeof(next));

    file.seek(sizeof(curr)+curr*sizeof(LogStruct), SeekSet);
    file.write((const byte*)&data, sizeof(LogStruct));
  }
  DEBUG_PRINTLN(F("ok"));      
  file.close();
  set_dirty_bit(DIRTY_BIT_JL, 1);
}

bool OpenGarage::read_log_start() {
  if(log_file) log_file.close();
  log_file = FILESYS.open(log_fname, "r");
  if(!log_file) return false;
  uint curr;
  if(log_file.readBytes((char*)&curr, sizeof(curr)) != sizeof(curr)) return false;
  if(curr>=MAX_LOG_SIZE) return false;
  return true;
}

bool OpenGarage::read_log_next(LogStruct& data) {
  if(!log_file) return false;
  if(log_file.readBytes((char*)&data, sizeof(LogStruct)) != sizeof(LogStruct)) return false;
  return true;  
}

bool OpenGarage::read_log_end() {
  if(!log_file) return false;
  log_file.close();
  return true;
}

void OpenGarage::play_note(uint freq) {
  if(freq>0) {
    tone(PIN_BUZZER, freq);
  } else {
    noTone(PIN_BUZZER);
  }
}

void OpenGarage::config_ip() {
  if(options[OPTION_USI].ival) {
    IPAddress dvip, gwip, subn, dns1;
    if(dvip.fromString(options[OPTION_DVIP].sval) &&
       gwip.fromString(options[OPTION_GWIP].sval) &&
       subn.fromString(options[OPTION_SUBN].sval) &&
       dns1.fromString(options[OPTION_DNS1].sval)) {
      WiFi.config(dvip, gwip, subn, dns1, gwip);
    }
  }
}
#include "pitches.h"

void OpenGarage::play_startup_tune() {
  static uint melody[] = {NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5};
  static byte duration[] = {4, 8, 8, 8};
  
  for (byte i = 0; i < sizeof(melody)/sizeof(uint); i++) {
    uint noteTime = 1000/duration[i];
    tone(PIN_BUZZER, melody[i], noteTime);
    uint delayTime = noteTime * 1.2f;
    delay(delayTime);
    noTone(PIN_BUZZER);
  }
}

#include "wled.h"
#include "wled_ethernet.h"

/*
 * Serializes and parses the cfg.json and wsec.json settings files, stored in internal FS.
 * The structure of the JSON is not to be considered an official API and may change without notice.
 */

//simple macro for ArduinoJSON's or syntax
#define CJSON(a,b) a = b | a

void getStringFromJson(char* dest, const char* src, size_t len) {
  if (src != nullptr) strlcpy(dest, src, len);
}

bool deserializeConfig(JsonObject doc, bool fromFS) {
  bool needsSave = false;
  //int rev_major = doc["rev"][0]; // 1
  //int rev_minor = doc["rev"][1]; // 0

  //long vid = doc[F("vid")]; // 2010020

#ifdef WLED_USE_ETHERNET
  JsonObject ethernet = doc[F("eth")];
  CJSON(ethernetType, ethernet["type"]);
  // NOTE: Ethernet configuration takes priority over other use of pins
  WLED::instance().initEthernet();
#endif

  JsonObject id = doc["id"];
  getStringFromJson(cmDNS, id[F("mdns")], 33);
  getStringFromJson(serverDescription, id[F("name")], 33);
#ifndef WLED_DISABLE_ALEXA
  getStringFromJson(alexaInvocationName, id[F("inv")], 33);
#endif
  CJSON(simplifiedUI, id[F("sui")]);

  JsonObject nw = doc["nw"];
#ifndef WLED_DISABLE_ESPNOW
  CJSON(enableESPNow, nw[F("espnow")]);
  getStringFromJson(linked_remote, nw[F("linked_remote")], 13);
  linked_remote[12] = '\0';
#endif

  size_t n = 0;
  JsonArray nw_ins = nw["ins"];
  if (!nw_ins.isNull()) {
    // as password are stored separately in wsec.json when reading configuration vector resize happens there, but for dynamic config we need to resize if necessary
    if (nw_ins.size() > 1 && nw_ins.size() > multiWiFi.size()) multiWiFi.resize(nw_ins.size()); // resize constructs objects while resizing
    for (JsonObject wifi : nw_ins) {
      JsonArray ip = wifi["ip"];
      JsonArray gw = wifi["gw"];
      JsonArray sn = wifi["sn"];
      char ssid[33] = "";
      char pass[65] = "";
      IPAddress nIP = (uint32_t)0U, nGW = (uint32_t)0U, nSN = (uint32_t)0x00FFFFFF; // little endian
      getStringFromJson(ssid, wifi[F("ssid")], 33);
      getStringFromJson(pass, wifi["psk"], 65); // password is not normally present but if it is, use it
      for (size_t i = 0; i < 4; i++) {
        CJSON(nIP[i], ip[i]);
        CJSON(nGW[i], gw[i]);
        CJSON(nSN[i], sn[i]);
      }
      if (strlen(ssid) > 0) strlcpy(multiWiFi[n].clientSSID, ssid, 33); // this will keep old SSID intact if not present in JSON
      if (strlen(pass) > 0) strlcpy(multiWiFi[n].clientPass, pass, 65); // this will keep old password intact if not present in JSON
      multiWiFi[n].staticIP = nIP;
      multiWiFi[n].staticGW = nGW;
      multiWiFi[n].staticSN = nSN;
      if (++n >= WLED_MAX_WIFI_COUNT) break;
    }
  }

  JsonArray dns = nw[F("dns")];
  if (!dns.isNull()) {
    for (size_t i = 0; i < 4; i++) {
      CJSON(dnsAddress[i], dns[i]);
    }
  }

  JsonObject ap = doc["ap"];
  getStringFromJson(apSSID, ap[F("ssid")], 33);
  getStringFromJson(apPass, ap["psk"] , 65); //normally not present due to security
  //int ap_pskl = ap[F("pskl")];
  CJSON(apChannel, ap[F("chan")]);
  if (apChannel > 13 || apChannel < 1) apChannel = 1;
  CJSON(apHide, ap[F("hide")]);
  if (apHide > 1) apHide = 1;
  CJSON(apBehavior, ap[F("behav")]);
  /*
  JsonArray ap_ip = ap["ip"];
  for (unsigned i = 0; i < 4; i++) {
    apIP[i] = ap_ip;
  }
  */

  JsonObject wifi = doc[F("wifi")];
  noWifiSleep = !(wifi[F("sleep")] | !noWifiSleep); // inverted
  //noWifiSleep = !noWifiSleep;
  CJSON(force802_3g, wifi[F("phy")]); //force phy mode g?
#ifdef ARDUINO_ARCH_ESP32
  CJSON(txPower, wifi[F("txpwr")]);
  txPower = min(max((int)txPower, (int)WIFI_POWER_2dBm), (int)WIFI_POWER_19_5dBm);
#endif

  JsonObject hw = doc[F("hw")];

  // initialize LED pins and lengths prior to other HW (except for ethernet)
  JsonObject hw_led = hw["led"];

  uint16_t total = hw_led[F("total")] | strip.getLengthTotal();
  uint16_t ablMilliampsMax = hw_led[F("maxpwr")] | BusManager::ablMilliampsMax();
  BusManager::setMilliampsMax(ablMilliampsMax);
  Bus::setGlobalAWMode(hw_led[F("rgbwm")] | AW_GLOBAL_DISABLED);
  CJSON(strip.correctWB, hw_led["cct"]);
  CJSON(strip.cctFromRgb, hw_led[F("cr")]);
  CJSON(cctICused, hw_led[F("ic")]);
  CJSON(strip.cctBlending, hw_led[F("cb")]);
  Bus::setCCTBlend(strip.cctBlending);
  strip.setTargetFps(hw_led["fps"]); //NOP if 0, default 42 FPS
  CJSON(useGlobalLedBuffer, hw_led[F("ld")]);
  #if defined(ARDUINO_ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32C3)
  CJSON(useParallelI2S, hw_led[F("prl")]);
  #endif

  #ifndef WLED_DISABLE_2D
  // 2D Matrix Settings
  JsonObject matrix = hw_led[F("matrix")];
  if (!matrix.isNull()) {
    strip.isMatrix = true;
    CJSON(strip.panels, matrix[F("mpc")]);
    strip.panel.clear();
    JsonArray panels = matrix[F("panels")];
    int s = 0;
    if (!panels.isNull()) {
      strip.panel.reserve(max(1U,min((size_t)strip.panels,(size_t)WLED_MAX_PANELS)));  // pre-allocate memory for panels
      for (JsonObject pnl : panels) {
        WS2812FX::Panel p;
        CJSON(p.bottomStart, pnl["b"]);
        CJSON(p.rightStart,  pnl["r"]);
        CJSON(p.vertical,    pnl["v"]);
        CJSON(p.serpentine,  pnl["s"]);
        CJSON(p.xOffset,     pnl["x"]);
        CJSON(p.yOffset,     pnl["y"]);
        CJSON(p.height,      pnl["h"]);
        CJSON(p.width,       pnl["w"]);
        strip.panel.push_back(p);
        if (++s >= WLED_MAX_PANELS || s >= strip.panels) break; // max panels reached
      }
    } else {
      // fallback
      WS2812FX::Panel p;
      strip.panels = 1;
      p.height = p.width = 8;
      p.xOffset = p.yOffset = 0;
      p.options = 0;
      strip.panel.push_back(p);
    }
    // cannot call strip.setUpMatrix() here due to already locked JSON buffer
  }
  #endif

  JsonArray ins = hw_led["ins"];

  if (fromFS || !ins.isNull()) {
    DEBUG_PRINTF_P(PSTR("Heap before buses: %d\n"), ESP.getFreeHeap());
    int s = 0;  // bus iterator
    if (fromFS) BusManager::removeAll(); // can't safely manipulate busses directly in network callback

    for (JsonObject elm : ins) {
      if (s >= WLED_MAX_BUSSES+WLED_MIN_VIRTUAL_BUSSES) break;
      uint8_t pins[5] = {255, 255, 255, 255, 255};
      JsonArray pinArr = elm["pin"];
      if (pinArr.size() == 0) continue;
      //pins[0] = pinArr[0];
      unsigned i = 0;
      for (int p : pinArr) {
        pins[i++] = p;
        if (i>4) break;
      }
      uint16_t length = elm["len"] | 1;
      uint8_t colorOrder = (int)elm[F("order")]; // contains white channel swap option in upper nibble
      uint8_t skipFirst = elm[F("skip")];
      uint16_t start = elm["start"] | 0;
      if (length==0 || start + length > MAX_LEDS) continue; // zero length or we reached max. number of LEDs, just stop
      uint8_t ledType = elm["type"] | TYPE_WS2812_RGB;
      bool reversed = elm["rev"];
      bool refresh = elm["ref"] | false;
      uint16_t freqkHz = elm[F("freq")] | 0;  // will be in kHz for DotStar and Hz for PWM
      uint8_t AWmode = elm[F("rgbwm")] | RGBW_MODE_MANUAL_ONLY;
      uint8_t maPerLed = elm[F("ledma")] | LED_MILLIAMPS_DEFAULT;
      uint16_t maMax = elm[F("maxpwr")] | (ablMilliampsMax * length) / total; // rough (incorrect?) per strip ABL calculation when no config exists
      // To disable brightness limiter we either set output max current to 0 or single LED current to 0 (we choose output max current)
      if (Bus::isPWM(ledType) || Bus::isOnOff(ledType) || Bus::isVirtual(ledType)) { // analog and virtual
        maPerLed = 0;
        maMax = 0;
      }
      ledType |= refresh << 7; // hack bit 7 to indicate strip requires off refresh

      busConfigs.push_back(std::move(BusConfig(ledType, pins, start, length, colorOrder, reversed, skipFirst, AWmode, freqkHz, useGlobalLedBuffer, maPerLed, maMax)));
      doInitBusses = true;  // finalization done in beginStrip()
      s++;
    }
  }
  if (hw_led["rev"]) BusManager::getBus(0)->setReversed(true); //set 0.11 global reversed setting for first bus

  // read color order map configuration
  JsonArray hw_com = hw[F("com")];
  if (!hw_com.isNull()) {
    BusManager::getColorOrderMap().reserve(std::min(hw_com.size(), (size_t)WLED_MAX_COLOR_ORDER_MAPPINGS));
    for (JsonObject entry : hw_com) {
      uint16_t start = entry["start"] | 0;
      uint16_t len = entry["len"] | 0;
      uint8_t colorOrder = (int)entry[F("order")];
      if (!BusManager::getColorOrderMap().add(start, len, colorOrder)) break;
    }
  }

  // read multiple button configuration
  JsonObject btn_obj = hw["btn"];
  CJSON(touchThreshold, btn_obj[F("tt")]);
  bool pull = btn_obj[F("pull")] | (!disablePullUp); // if true, pullup is enabled
  disablePullUp = !pull;
  JsonArray hw_btn_ins = btn_obj["ins"];
  if (!hw_btn_ins.isNull()) {
    // deallocate existing button pins
    for (unsigned b = 0; b < WLED_MAX_BUTTONS; b++) PinManager::deallocatePin(btnPin[b], PinOwner::Button); // does nothing if trying to deallocate a pin with PinOwner != Button
    unsigned s = 0;
    for (JsonObject btn : hw_btn_ins) {
      CJSON(buttonType[s], btn["type"]);
      int8_t pin = btn["pin"][0] | -1;
      if (pin > -1 && PinManager::allocatePin(pin, false, PinOwner::Button)) {
        btnPin[s] = pin;
      #ifdef ARDUINO_ARCH_ESP32
        // ESP32 only: check that analog button pin is a valid ADC gpio
        if ((buttonType[s] == BTN_TYPE_ANALOG) || (buttonType[s] == BTN_TYPE_ANALOG_INVERTED)) {
          if (digitalPinToAnalogChannel(btnPin[s]) < 0) {
            // not an ADC analog pin
            DEBUG_PRINTF_P(PSTR("PIN ALLOC error: GPIO%d for analog button #%d is not an analog pin!\n"), btnPin[s], s);
            btnPin[s] = -1;
            PinManager::deallocatePin(pin,PinOwner::Button);
          } else {
            analogReadResolution(12); // see #4040
          }
        }
        else if ((buttonType[s] == BTN_TYPE_TOUCH || buttonType[s] == BTN_TYPE_TOUCH_SWITCH))
        {
          if (digitalPinToTouchChannel(btnPin[s]) < 0) {
            // not a touch pin
            DEBUG_PRINTF_P(PSTR("PIN ALLOC error: GPIO%d for touch button #%d is not a touch pin!\n"), btnPin[s], s);
            btnPin[s] = -1;
            PinManager::deallocatePin(pin,PinOwner::Button);
          }          
          //if touch pin, enable the touch interrupt on ESP32 S2 & S3
          #ifdef SOC_TOUCH_VERSION_2    // ESP32 S2 and S3 have a function to check touch state but need to attach an interrupt to do so
          else
          {
            touchAttachInterrupt(btnPin[s], touchButtonISR, touchThreshold << 4); // threshold on Touch V2 is much higher (1500 is a value given by Espressif example, I measured changes of over 5000)
          }
          #endif
        }
        else
      #endif
        {
          if (disablePullUp) {
            pinMode(btnPin[s], INPUT);
          } else {
            #ifdef ESP32
            pinMode(btnPin[s], buttonType[s]==BTN_TYPE_PUSH_ACT_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
            #else
            pinMode(btnPin[s], INPUT_PULLUP);
            #endif
          }
        }
      } else {
        btnPin[s] = -1;
      }
      JsonArray hw_btn_ins_0_macros = btn["macros"];
      CJSON(macroButton[s], hw_btn_ins_0_macros[0]);
      CJSON(macroLongPress[s],hw_btn_ins_0_macros[1]);
      CJSON(macroDoublePress[s], hw_btn_ins_0_macros[2]);
      if (++s >= WLED_MAX_BUTTONS) break; // max buttons reached
    }
    // clear remaining buttons
    for (; s<WLED_MAX_BUTTONS; s++) {
      btnPin[s]           = -1;
      buttonType[s]       = BTN_TYPE_NONE;
      macroButton[s]      = 0;
      macroLongPress[s]   = 0;
      macroDoublePress[s] = 0;
    }
  } else {
    // new install/missing configuration (button 0 has defaults)
    if (fromFS) {
      // relies upon only being called once with fromFS == true, which is currently true.
      for (size_t s = 0; s < WLED_MAX_BUTTONS; s++) {
        if (buttonType[s] == BTN_TYPE_NONE || btnPin[s] < 0 || !PinManager::allocatePin(btnPin[s], false, PinOwner::Button)) {
          btnPin[s]     = -1;
          buttonType[s] = BTN_TYPE_NONE;
        }
        if (btnPin[s] >= 0) {
          if (disablePullUp) {
            pinMode(btnPin[s], INPUT);
          } else {
            #ifdef ESP32
            pinMode(btnPin[s], buttonType[s]==BTN_TYPE_PUSH_ACT_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
            #else
            pinMode(btnPin[s], INPUT_PULLUP);
            #endif
          }
        }
        macroButton[s]      = 0;
        macroLongPress[s]   = 0;
        macroDoublePress[s] = 0;
      }
    }
  }

  CJSON(buttonPublishMqtt,btn_obj["mqtt"]);

  #ifndef WLED_DISABLE_INFRARED
  int hw_ir_pin = hw["ir"]["pin"] | -2; // 4
  if (hw_ir_pin > -2) {
    PinManager::deallocatePin(irPin, PinOwner::IR);
    if (PinManager::allocatePin(hw_ir_pin, false, PinOwner::IR)) {
      irPin = hw_ir_pin;
    } else {
      irPin = -1;
    }
  }
  CJSON(irEnabled, hw["ir"]["type"]);
  #endif
  CJSON(irApplyToAllSelected, hw["ir"]["sel"]);

  JsonObject relay = hw[F("relay")];

  rlyOpenDrain  = relay[F("odrain")] | rlyOpenDrain;
  int hw_relay_pin = relay["pin"] | -2;
  if (hw_relay_pin > -2) {
    PinManager::deallocatePin(rlyPin, PinOwner::Relay);
    if (PinManager::allocatePin(hw_relay_pin,true, PinOwner::Relay)) {
      rlyPin = hw_relay_pin;
      pinMode(rlyPin, rlyOpenDrain ? OUTPUT_OPEN_DRAIN : OUTPUT);
    } else {
      rlyPin = -1;
    }
  }
  if (relay.containsKey("rev")) {
    rlyMde = !relay["rev"];
  }

  CJSON(serialBaud, hw[F("baud")]);
  if (serialBaud < 96 || serialBaud > 15000) serialBaud = 1152;
  updateBaudRate(serialBaud *100);

  JsonArray hw_if_i2c = hw[F("if")][F("i2c-pin")];
  CJSON(i2c_sda, hw_if_i2c[0]);
  CJSON(i2c_scl, hw_if_i2c[1]);
  PinManagerPinType i2c[2] = { { i2c_sda, true }, { i2c_scl, true } };
  if (i2c_scl >= 0 && i2c_sda >= 0 && PinManager::allocateMultiplePins(i2c, 2, PinOwner::HW_I2C)) {
    #ifdef ESP32
    if (!Wire.setPins(i2c_sda, i2c_scl)) { i2c_scl = i2c_sda = -1; } // this will fail if Wire is initialised (Wire.begin() called prior)
    else Wire.begin();
    #else
    Wire.begin(i2c_sda, i2c_scl);
    #endif
  } else {
    i2c_sda = -1;
    i2c_scl = -1;
  }
  JsonArray hw_if_spi = hw[F("if")][F("spi-pin")];
  CJSON(spi_mosi, hw_if_spi[0]);
  CJSON(spi_sclk, hw_if_spi[1]);
  CJSON(spi_miso, hw_if_spi[2]);
  PinManagerPinType spi[3] = { { spi_mosi, true }, { spi_miso, true }, { spi_sclk, true } };
  if (spi_mosi >= 0 && spi_sclk >= 0 && PinManager::allocateMultiplePins(spi, 3, PinOwner::HW_SPI)) {
    #ifdef ESP32
    SPI.begin(spi_sclk, spi_miso, spi_mosi);  // SPI global uses VSPI on ESP32 and FSPI on C3, S3
    #else
    SPI.begin();
    #endif
  } else {
    spi_mosi = -1;
    spi_miso = -1;
    spi_sclk = -1;
  }

  //int hw_status_pin = hw[F("status")]["pin"]; // -1

  JsonObject light = doc[F("light")];
  CJSON(briMultiplier, light[F("scale-bri")]);
  CJSON(strip.paletteBlend, light[F("pal-mode")]);
  CJSON(strip.autoSegments, light[F("aseg")]);

  CJSON(gammaCorrectVal, light["gc"]["val"]); // default 2.8
  float light_gc_bri = light["gc"]["bri"];
  float light_gc_col = light["gc"]["col"];
  if (light_gc_bri > 1.0f) gammaCorrectBri = true;
  else                     gammaCorrectBri = false;
  if (light_gc_col > 1.0f) gammaCorrectCol = true;
  else                     gammaCorrectCol = false;
  if (gammaCorrectVal > 1.0f && gammaCorrectVal <= 3) {
    if (gammaCorrectVal != 2.8f) NeoGammaWLEDMethod::calcGammaTable(gammaCorrectVal);
  } else {
    gammaCorrectVal = 1.0f; // no gamma correction
    gammaCorrectBri = false;
    gammaCorrectCol = false;
  }

  JsonObject light_tr = light["tr"];
  CJSON(fadeTransition, light_tr["mode"]);
  CJSON(modeBlending, light_tr["fx"]);
  int tdd = light_tr["dur"] | -1;
  if (tdd >= 0) transitionDelay = transitionDelayDefault = tdd * 100;
  strip.setTransition(fadeTransition ? transitionDelayDefault : 0);
  CJSON(strip.paletteFade, light_tr["pal"]);
  CJSON(randomPaletteChangeTime, light_tr[F("rpc")]);
  CJSON(useHarmonicRandomPalette, light_tr[F("hrp")]);

  JsonObject light_nl = light["nl"];
  CJSON(nightlightMode, light_nl["mode"]);
  byte prev = nightlightDelayMinsDefault;
  CJSON(nightlightDelayMinsDefault, light_nl["dur"]);
  if (nightlightDelayMinsDefault != prev) nightlightDelayMins = nightlightDelayMinsDefault;

  CJSON(nightlightTargetBri, light_nl[F("tbri")]);
  CJSON(macroNl, light_nl["macro"]);

  JsonObject def = doc["def"];
  CJSON(bootPreset, def["ps"]);
  CJSON(turnOnAtBoot, def["on"]); // true
  CJSON(briS, def["bri"]); // 128

  JsonObject interfaces = doc["if"];

  JsonObject if_sync = interfaces["sync"];
  CJSON(udpPort, if_sync[F("port0")]); // 21324
  CJSON(udpPort2, if_sync[F("port1")]); // 65506

#ifndef WLED_DISABLE_ESPNOW
  CJSON(useESPNowSync, if_sync[F("espnow")]);
#endif

  JsonObject if_sync_recv = if_sync[F("recv")];
  CJSON(receiveNotificationBrightness, if_sync_recv["bri"]);
  CJSON(receiveNotificationColor, if_sync_recv["col"]);
  CJSON(receiveNotificationEffects, if_sync_recv["fx"]);
  CJSON(receiveNotificationPalette, if_sync_recv["pal"]);
  CJSON(receiveGroups, if_sync_recv["grp"]);
  CJSON(receiveSegmentOptions, if_sync_recv["seg"]);
  CJSON(receiveSegmentBounds, if_sync_recv["sb"]);

  JsonObject if_sync_send = if_sync[F("send")];
  CJSON(sendNotifications, if_sync_send["en"]);
  sendNotificationsRT = sendNotifications;
  CJSON(notifyDirect, if_sync_send[F("dir")]);
  CJSON(notifyButton, if_sync_send["btn"]);
  CJSON(notifyAlexa, if_sync_send["va"]);
  CJSON(notifyHue, if_sync_send["hue"]);
  CJSON(syncGroups, if_sync_send["grp"]);
  if (if_sync_send[F("twice")]) udpNumRetries = 1; // import setting from 0.13 and earlier
  CJSON(udpNumRetries, if_sync_send["ret"]);

  JsonObject if_nodes = interfaces["nodes"];
  CJSON(nodeListEnabled, if_nodes[F("list")]);
  CJSON(nodeBroadcastEnabled, if_nodes[F("bcast")]);

  JsonObject if_live = interfaces["live"];
  CJSON(receiveDirect, if_live["en"]);  // UDP/Hyperion realtime
  CJSON(useMainSegmentOnly, if_live[F("mso")]);
  CJSON(realtimeRespectLedMaps, if_live[F("rlm")]);
  CJSON(e131Port, if_live["port"]); // 5568
  if (e131Port == DDP_DEFAULT_PORT) e131Port = E131_DEFAULT_PORT; // prevent double DDP port allocation
  CJSON(e131Multicast, if_live[F("mc")]);

  JsonObject if_live_dmx = if_live["dmx"];
  CJSON(e131Universe, if_live_dmx[F("uni")]);
  CJSON(e131SkipOutOfSequence, if_live_dmx[F("seqskip")]);
  CJSON(DMXAddress, if_live_dmx[F("addr")]);
  if (!DMXAddress || DMXAddress > 510) DMXAddress = 1;
  CJSON(DMXSegmentSpacing, if_live_dmx[F("dss")]);
  if (DMXSegmentSpacing > 150) DMXSegmentSpacing = 0;
  CJSON(e131Priority, if_live_dmx[F("e131prio")]);
  if (e131Priority > 200) e131Priority = 200;
  CJSON(DMXMode, if_live_dmx["mode"]);

  tdd = if_live[F("timeout")] | -1;
  if (tdd >= 0) realtimeTimeoutMs = tdd * 100;
  CJSON(arlsForceMaxBri, if_live[F("maxbri")]);
  CJSON(arlsDisableGammaCorrection, if_live[F("no-gc")]); // false
  CJSON(arlsOffset, if_live[F("offset")]); // 0

#ifndef WLED_DISABLE_ALEXA
  CJSON(alexaEnabled, interfaces["va"][F("alexa")]); // false
  CJSON(macroAlexaOn, interfaces["va"]["macros"][0]);
  CJSON(macroAlexaOff, interfaces["va"]["macros"][1]);
  CJSON(alexaNumPresets, interfaces["va"]["p"]);
#endif

#ifndef WLED_DISABLE_MQTT
  JsonObject if_mqtt = interfaces["mqtt"];
  CJSON(mqttEnabled, if_mqtt["en"]);
  getStringFromJson(mqttServer, if_mqtt[F("broker")], MQTT_MAX_SERVER_LEN+1);
  CJSON(mqttPort, if_mqtt["port"]); // 1883
  getStringFromJson(mqttUser, if_mqtt[F("user")], 41);
  getStringFromJson(mqttPass, if_mqtt["psk"], 65); //normally not present due to security
  getStringFromJson(mqttClientID, if_mqtt[F("cid")], 41);

  getStringFromJson(mqttDeviceTopic, if_mqtt[F("topics")][F("device")], MQTT_MAX_TOPIC_LEN+1); // "wled/test"
  getStringFromJson(mqttGroupTopic, if_mqtt[F("topics")][F("group")], MQTT_MAX_TOPIC_LEN+1); // ""
  CJSON(retainMqttMsg, if_mqtt[F("rtn")]);
#endif

#ifndef WLED_DISABLE_HUESYNC
  JsonObject if_hue = interfaces["hue"];
  CJSON(huePollingEnabled, if_hue["en"]);
  CJSON(huePollLightId, if_hue["id"]);
  tdd = if_hue[F("iv")] | -1;
  if (tdd >= 2) huePollIntervalMs = tdd * 100;

  JsonObject if_hue_recv = if_hue["recv"];
  CJSON(hueApplyOnOff, if_hue_recv["on"]);
  CJSON(hueApplyBri, if_hue_recv["bri"]);
  CJSON(hueApplyColor, if_hue_recv["col"]);

  JsonArray if_hue_ip = if_hue["ip"];

  for (unsigned i = 0; i < 4; i++)
    CJSON(hueIP[i], if_hue_ip[i]);
#endif

  JsonObject if_ntp = interfaces[F("ntp")];
  CJSON(ntpEnabled, if_ntp["en"]);
  getStringFromJson(ntpServerName, if_ntp[F("host")], 33); // "1.wled.pool.ntp.org"
  CJSON(currentTimezone, if_ntp[F("tz")]);
  CJSON(utcOffsetSecs, if_ntp[F("offset")]);
  CJSON(useAMPM, if_ntp[F("ampm")]);
  CJSON(longitude, if_ntp[F("ln")]);
  CJSON(latitude, if_ntp[F("lt")]);

  JsonObject ol = doc[F("ol")];
  CJSON(overlayCurrent ,ol[F("clock")]); // 0
  CJSON(countdownMode, ol[F("cntdwn")]);

  CJSON(overlayMin, ol["min"]);
  CJSON(overlayMax, ol[F("max")]);
  CJSON(analogClock12pixel, ol[F("o12pix")]);
  CJSON(analogClock5MinuteMarks, ol[F("o5m")]);
  CJSON(analogClockSecondsTrail, ol[F("osec")]);
  CJSON(analogClockSolidBlack, ol[F("osb")]);

  //timed macro rules
  JsonObject tm = doc[F("timers")];
  JsonObject cntdwn = tm[F("cntdwn")];
  JsonArray cntdwn_goal = cntdwn[F("goal")];
  CJSON(countdownYear,  cntdwn_goal[0]);
  CJSON(countdownMonth, cntdwn_goal[1]);
  CJSON(countdownDay,   cntdwn_goal[2]);
  CJSON(countdownHour,  cntdwn_goal[3]);
  CJSON(countdownMin,   cntdwn_goal[4]);
  CJSON(countdownSec,   cntdwn_goal[5]);
  CJSON(macroCountdown, cntdwn["macro"]);
  setCountdown();

  JsonArray timers = tm["ins"];
  uint8_t it = 0;
  for (JsonObject timer : timers) {
    if (it > 9) break;
    if (it<8 && timer[F("hour")]==255) it=8;  // hour==255 -> sunrise/sunset
    CJSON(timerHours[it], timer[F("hour")]);
    CJSON(timerMinutes[it], timer["min"]);
    CJSON(timerMacro[it], timer["macro"]);

    byte dowPrev = timerWeekday[it];
    //note: act is currently only 0 or 1.
    //the reason we are not using bool is that the on-disk type in 0.11.0 was already int
    int actPrev = timerWeekday[it] & 0x01;
    CJSON(timerWeekday[it], timer[F("dow")]);
    if (timerWeekday[it] != dowPrev) { //present in JSON
      timerWeekday[it] <<= 1; //add active bit
      int act = timer["en"] | actPrev;
      if (act) timerWeekday[it]++;
    }
    if (it<8) {
      JsonObject start = timer["start"];
      byte startm = start["mon"];
      if (startm) timerMonth[it] = (startm << 4);
      CJSON(timerDay[it], start["day"]);
      JsonObject end = timer["end"];
      CJSON(timerDayEnd[it], end["day"]);
      byte endm = end["mon"];
      if (startm) timerMonth[it] += endm & 0x0F;
      if (!(timerMonth[it] & 0x0F)) timerMonth[it] += 12; //default end month to 12
    }
    it++;
  }

  JsonObject ota = doc["ota"];
  const char* pwd = ota["psk"]; //normally not present due to security

  bool pwdCorrect = !otaLock; //always allow access if ota not locked
  if (pwd != nullptr && strncmp(otaPass, pwd, 33) == 0) pwdCorrect = true;

  if (pwdCorrect) { //only accept these values from cfg.json if ota is unlocked (else from wsec.json)
    CJSON(otaLock, ota[F("lock")]);
    CJSON(wifiLock, ota[F("lock-wifi")]);
    CJSON(aOtaEnabled, ota[F("aota")]);
    getStringFromJson(otaPass, pwd, 33); //normally not present due to security
  }

  #ifdef WLED_ENABLE_DMX
  JsonObject dmx = doc["dmx"];
  CJSON(DMXChannels, dmx[F("chan")]);
  CJSON(DMXGap,dmx[F("gap")]);
  CJSON(DMXStart, dmx["start"]);
  CJSON(DMXStartLED,dmx[F("start-led")]);

  JsonArray dmx_fixmap = dmx[F("fixmap")];
  for (int i = 0; i < dmx_fixmap.size(); i++) {
    if (i > 14) break;
    CJSON(DMXFixtureMap[i],dmx_fixmap[i]);
  }

  CJSON(e131ProxyUniverse, dmx[F("e131proxy")]);
  #endif

  DEBUG_PRINTLN(F("Starting usermod config."));
  JsonObject usermods_settings = doc["um"];
  if (!usermods_settings.isNull()) {
    needsSave = !UsermodManager::readFromConfig(usermods_settings);
  }

  if (fromFS) return needsSave;
  // if from /json/cfg
  doReboot = doc[F("rb")] | doReboot;
  if (doInitBusses) return false; // no save needed, will do after bus init in wled.cpp loop
  return (doc["sv"] | true);
}


static const char s_cfg_json[] PROGMEM = "/cfg.json";

void deserializeConfigFromFS() {
  bool success = deserializeConfigSec();
  #ifdef WLED_ADD_EEPROM_SUPPORT
  if (!success) { //if file does not exist, try reading from EEPROM
    deEEPSettings();
    return;
  }
  #endif

  if (!requestJSONBufferLock(1)) return;

  DEBUG_PRINTLN(F("Reading settings from /cfg.json..."));

  success = readObjectFromFile(s_cfg_json, nullptr, pDoc);
  if (!success) { // if file does not exist, optionally try reading from EEPROM and then save defaults to FS
    releaseJSONBufferLock();
    #ifdef WLED_ADD_EEPROM_SUPPORT
    deEEPSettings();
    #endif

    // save default values to /cfg.json
    // call readFromConfig() with an empty object so that usermods can initialize to defaults prior to saving
    JsonObject empty = JsonObject();
    UsermodManager::readFromConfig(empty);
    serializeConfig();
    // init Ethernet (in case default type is set at compile time)
    #ifdef WLED_USE_ETHERNET
    WLED::instance().initEthernet();
    #endif
    return;
  }

  // NOTE: This routine deserializes *and* applies the configuration
  //       Therefore, must also initialize ethernet from this function
  JsonObject root = pDoc->as<JsonObject>();
  bool needsSave = deserializeConfig(root, true);
  releaseJSONBufferLock();

  if (needsSave) serializeConfig(); // usermods required new parameters
}

void serializeConfig() {
  serializeConfigSec();

  DEBUG_PRINTLN(F("Writing settings to /cfg.json..."));

  if (!requestJSONBufferLock(2)) return;

  JsonObject root = pDoc->to<JsonObject>();

  JsonArray rev = root.createNestedArray("rev");
  rev.add(1); //major settings revision
  rev.add(0); //minor settings revision

  root[F("vid")] = VERSION;

  JsonObject id = root.createNestedObject("id");
  id[F("mdns")] = cmDNS;
  id[F("name")] = serverDescription;
#ifndef WLED_DISABLE_ALEXA
  id[F("inv")] = alexaInvocationName;
#endif
  id[F("sui")] = simplifiedUI;

  JsonObject nw = root.createNestedObject("nw");
#ifndef WLED_DISABLE_ESPNOW
  nw[F("espnow")] = enableESPNow;
  nw[F("linked_remote")] = linked_remote;
#endif

  JsonArray nw_ins = nw.createNestedArray("ins");
  for (size_t n = 0; n < multiWiFi.size(); n++) {
    JsonObject wifi = nw_ins.createNestedObject();
    wifi[F("ssid")] = multiWiFi[n].clientSSID;
    wifi[F("pskl")] = strlen(multiWiFi[n].clientPass);
    JsonArray wifi_ip = wifi.createNestedArray("ip");
    JsonArray wifi_gw = wifi.createNestedArray("gw");
    JsonArray wifi_sn = wifi.createNestedArray("sn");
    for (size_t i = 0; i < 4; i++) {
      wifi_ip.add(multiWiFi[n].staticIP[i]);
      wifi_gw.add(multiWiFi[n].staticGW[i]);
      wifi_sn.add(multiWiFi[n].staticSN[i]);
    }
  }

  JsonArray dns = nw.createNestedArray(F("dns"));
  for (size_t i = 0; i < 4; i++) {
    dns.add(dnsAddress[i]);
  }

  JsonObject ap = root.createNestedObject("ap");
  ap[F("ssid")] = apSSID;
  ap[F("pskl")] = strlen(apPass);
  ap[F("chan")] = apChannel;
  ap[F("hide")] = apHide;
  ap[F("behav")] = apBehavior;

  JsonArray ap_ip = ap.createNestedArray("ip");
  ap_ip.add(4);
  ap_ip.add(3);
  ap_ip.add(2);
  ap_ip.add(1);

  JsonObject wifi = root.createNestedObject(F("wifi"));
  wifi[F("sleep")] = !noWifiSleep;
  wifi[F("phy")] = force802_3g;
#ifdef ARDUINO_ARCH_ESP32
  wifi[F("txpwr")] = txPower;
#endif

#ifdef WLED_USE_ETHERNET
  JsonObject ethernet = root.createNestedObject("eth");
  ethernet["type"] = ethernetType;
  if (ethernetType != WLED_ETH_NONE && ethernetType < WLED_NUM_ETH_TYPES) {
    JsonArray pins = ethernet.createNestedArray("pin");
    for (unsigned p=0; p<WLED_ETH_RSVD_PINS_COUNT; p++) pins.add(esp32_nonconfigurable_ethernet_pins[p].pin);
    if (ethernetBoards[ethernetType].eth_power>=0)     pins.add(ethernetBoards[ethernetType].eth_power);
    if (ethernetBoards[ethernetType].eth_mdc>=0)       pins.add(ethernetBoards[ethernetType].eth_mdc);
    if (ethernetBoards[ethernetType].eth_mdio>=0)      pins.add(ethernetBoards[ethernetType].eth_mdio);
    switch (ethernetBoards[ethernetType].eth_clk_mode) {
      case ETH_CLOCK_GPIO0_IN:
      case ETH_CLOCK_GPIO0_OUT:
        pins.add(0);
        break;
      case ETH_CLOCK_GPIO16_OUT:
        pins.add(16);
        break;
      case ETH_CLOCK_GPIO17_OUT:
        pins.add(17);
        break;
    }
  }
#endif

  JsonObject hw = root.createNestedObject(F("hw"));

  JsonObject hw_led = hw.createNestedObject("led");
  hw_led[F("total")] = strip.getLengthTotal(); //provided for compatibility on downgrade and per-output ABL
  hw_led[F("maxpwr")] = BusManager::ablMilliampsMax();
  hw_led[F("ledma")] = 0; // no longer used
  hw_led["cct"] = strip.correctWB;
  hw_led[F("cr")] = strip.cctFromRgb;
  hw_led[F("ic")] = cctICused;
  hw_led[F("cb")] = strip.cctBlending;
  hw_led["fps"] = strip.getTargetFps();
  hw_led[F("rgbwm")] = Bus::getGlobalAWMode(); // global auto white mode override
  hw_led[F("ld")] = useGlobalLedBuffer;
  #if defined(ARDUINO_ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32C3)
  hw_led[F("prl")] = BusManager::hasParallelOutput();
  #endif

  #ifndef WLED_DISABLE_2D
  // 2D Matrix Settings
  if (strip.isMatrix) {
    JsonObject matrix = hw_led.createNestedObject(F("matrix"));
    matrix[F("mpc")] = strip.panels;
    JsonArray panels = matrix.createNestedArray(F("panels"));
    for (size_t i = 0; i < strip.panel.size(); i++) {
      JsonObject pnl = panels.createNestedObject();
      pnl["b"] = strip.panel[i].bottomStart;
      pnl["r"] = strip.panel[i].rightStart;
      pnl["v"] = strip.panel[i].vertical;
      pnl["s"] = strip.panel[i].serpentine;
      pnl["x"] = strip.panel[i].xOffset;
      pnl["y"] = strip.panel[i].yOffset;
      pnl["h"] = strip.panel[i].height;
      pnl["w"] = strip.panel[i].width;
    }
  }
  #endif

  JsonArray hw_led_ins = hw_led.createNestedArray("ins");

  for (size_t s = 0; s < BusManager::getNumBusses(); s++) {
    DEBUG_PRINTF_P(PSTR("Cfg: Saving bus #%u\n"), s);
    Bus *bus = BusManager::getBus(s);
    if (!bus || bus->getLength()==0) break;
    DEBUG_PRINTF_P(PSTR("  (%d-%d, type:%d, CO:%d, rev:%d, skip:%d, AW:%d kHz:%d, mA:%d/%d)\n"),
      (int)bus->getStart(), (int)(bus->getStart()+bus->getLength()),
      (int)(bus->getType() & 0x7F),
      (int)bus->getColorOrder(),
      (int)bus->isReversed(),
      (int)bus->skippedLeds(),
      (int)bus->getAutoWhiteMode(),
      (int)bus->getFrequency(),
      (int)bus->getLEDCurrent(), (int)bus->getMaxCurrent()
    );
    JsonObject ins = hw_led_ins.createNestedObject();
    ins["start"] = bus->getStart();
    ins["len"] = bus->getLength();
    JsonArray ins_pin = ins.createNestedArray("pin");
    uint8_t pins[5];
    uint8_t nPins = bus->getPins(pins);
    for (int i = 0; i < nPins; i++) ins_pin.add(pins[i]);
    ins[F("order")] = bus->getColorOrder();
    ins["rev"] = bus->isReversed();
    ins[F("skip")] = bus->skippedLeds();
    ins["type"] = bus->getType() & 0x7F;
    ins["ref"] = bus->isOffRefreshRequired();
    ins[F("rgbwm")] = bus->getAutoWhiteMode();
    ins[F("freq")] = bus->getFrequency();
    ins[F("maxpwr")] = bus->getMaxCurrent();
    ins[F("ledma")] = bus->getLEDCurrent();
  }

  JsonArray hw_com = hw.createNestedArray(F("com"));
  const ColorOrderMap& com = BusManager::getColorOrderMap();
  for (size_t s = 0; s < com.count(); s++) {
    const ColorOrderMapEntry *entry = com.get(s);
    if (!entry) break;

    JsonObject co = hw_com.createNestedObject();
    co["start"] = entry->start;
    co["len"] = entry->len;
    co[F("order")] = entry->colorOrder;
  }

  // button(s)
  JsonObject hw_btn = hw.createNestedObject("btn");
  hw_btn["max"] = WLED_MAX_BUTTONS; // just information about max number of buttons (not actually used)
  hw_btn[F("pull")] = !disablePullUp;
  JsonArray hw_btn_ins = hw_btn.createNestedArray("ins");

  // configuration for all buttons
  for (int i = 0; i < WLED_MAX_BUTTONS; i++) {
    JsonObject hw_btn_ins_0 = hw_btn_ins.createNestedObject();
    hw_btn_ins_0["type"] = buttonType[i];
    JsonArray hw_btn_ins_0_pin = hw_btn_ins_0.createNestedArray("pin");
    hw_btn_ins_0_pin.add(btnPin[i]);
    JsonArray hw_btn_ins_0_macros = hw_btn_ins_0.createNestedArray("macros");
    hw_btn_ins_0_macros.add(macroButton[i]);
    hw_btn_ins_0_macros.add(macroLongPress[i]);
    hw_btn_ins_0_macros.add(macroDoublePress[i]);
  }

  hw_btn[F("tt")] = touchThreshold;
  hw_btn["mqtt"] = buttonPublishMqtt;

  JsonObject hw_ir = hw.createNestedObject("ir");
  #ifndef WLED_DISABLE_INFRARED
  hw_ir["pin"] = irPin;
  hw_ir["type"] = irEnabled;  // the byte 'irEnabled' does contain the IR-Remote Type ( 0=disabled )
  #endif
  hw_ir["sel"] = irApplyToAllSelected;

  JsonObject hw_relay = hw.createNestedObject(F("relay"));
  hw_relay["pin"] = rlyPin;
  hw_relay["rev"] = !rlyMde;
  hw_relay[F("odrain")] = rlyOpenDrain;

  hw[F("baud")] = serialBaud;

  JsonObject hw_if = hw.createNestedObject(F("if"));
  JsonArray hw_if_i2c = hw_if.createNestedArray("i2c-pin");
  hw_if_i2c.add(i2c_sda);
  hw_if_i2c.add(i2c_scl);
  JsonArray hw_if_spi = hw_if.createNestedArray("spi-pin");
  hw_if_spi.add(spi_mosi);
  hw_if_spi.add(spi_sclk);
  hw_if_spi.add(spi_miso);

  //JsonObject hw_status = hw.createNestedObject("status");
  //hw_status["pin"] = -1;

  JsonObject light = root.createNestedObject(F("light"));
  light[F("scale-bri")] = briMultiplier;
  light[F("pal-mode")] = strip.paletteBlend;
  light[F("aseg")] = strip.autoSegments;

  JsonObject light_gc = light.createNestedObject("gc");
  light_gc["bri"] = (gammaCorrectBri) ? gammaCorrectVal : 1.0f;  // keep compatibility
  light_gc["col"] = (gammaCorrectCol) ? gammaCorrectVal : 1.0f;  // keep compatibility
  light_gc["val"] = gammaCorrectVal;

  JsonObject light_tr = light.createNestedObject("tr");
  light_tr["mode"] = fadeTransition;
  light_tr["fx"] = modeBlending;
  light_tr["dur"] = transitionDelayDefault / 100;
  light_tr["pal"] = strip.paletteFade;
  light_tr[F("rpc")] = randomPaletteChangeTime;
  light_tr[F("hrp")] = useHarmonicRandomPalette;

  JsonObject light_nl = light.createNestedObject("nl");
  light_nl["mode"] = nightlightMode;
  light_nl["dur"] = nightlightDelayMinsDefault;
  light_nl[F("tbri")] = nightlightTargetBri;
  light_nl["macro"] = macroNl;

  JsonObject def = root.createNestedObject("def");
  def["ps"] = bootPreset;
  def["on"] = turnOnAtBoot;
  def["bri"] = briS;

  JsonObject interfaces = root.createNestedObject("if");

  JsonObject if_sync = interfaces.createNestedObject("sync");
  if_sync[F("port0")] = udpPort;
  if_sync[F("port1")] = udpPort2;

#ifndef WLED_DISABLE_ESPNOW
  if_sync[F("espnow")] = useESPNowSync;
#endif

  JsonObject if_sync_recv = if_sync.createNestedObject(F("recv"));
  if_sync_recv["bri"] = receiveNotificationBrightness;
  if_sync_recv["col"] = receiveNotificationColor;
  if_sync_recv["fx"]  = receiveNotificationEffects;
  if_sync_recv["pal"] = receiveNotificationPalette;
  if_sync_recv["grp"] = receiveGroups;
  if_sync_recv["seg"] = receiveSegmentOptions;
  if_sync_recv["sb"]  = receiveSegmentBounds;

  JsonObject if_sync_send = if_sync.createNestedObject(F("send"));
  if_sync_send["en"] = sendNotifications;
  if_sync_send[F("dir")] = notifyDirect;
  if_sync_send["btn"] = notifyButton;
  if_sync_send["va"] = notifyAlexa;
  if_sync_send["hue"] = notifyHue;
  if_sync_send["grp"] = syncGroups;
  if_sync_send["ret"] = udpNumRetries;

  JsonObject if_nodes = interfaces.createNestedObject("nodes");
  if_nodes[F("list")] = nodeListEnabled;
  if_nodes[F("bcast")] = nodeBroadcastEnabled;

  JsonObject if_live = interfaces.createNestedObject("live");
  if_live["en"] = receiveDirect; // UDP/Hyperion realtime
  if_live[F("mso")] = useMainSegmentOnly;
  if_live[F("rlm")] = realtimeRespectLedMaps;
  if_live["port"] = e131Port;
  if_live[F("mc")] = e131Multicast;

  JsonObject if_live_dmx = if_live.createNestedObject("dmx");
  if_live_dmx[F("uni")] = e131Universe;
  if_live_dmx[F("seqskip")] = e131SkipOutOfSequence;
  if_live_dmx[F("e131prio")] = e131Priority;
  if_live_dmx[F("addr")] = DMXAddress;
  if_live_dmx[F("dss")] = DMXSegmentSpacing;
  if_live_dmx["mode"] = DMXMode;

  if_live[F("timeout")] = realtimeTimeoutMs / 100;
  if_live[F("maxbri")] = arlsForceMaxBri;
  if_live[F("no-gc")] = arlsDisableGammaCorrection;
  if_live[F("offset")] = arlsOffset;

#ifndef WLED_DISABLE_ALEXA
  JsonObject if_va = interfaces.createNestedObject("va");
  if_va[F("alexa")] = alexaEnabled;

  JsonArray if_va_macros = if_va.createNestedArray("macros");
  if_va_macros.add(macroAlexaOn);
  if_va_macros.add(macroAlexaOff);

  if_va["p"] = alexaNumPresets;
#endif

#ifndef WLED_DISABLE_MQTT
  JsonObject if_mqtt = interfaces.createNestedObject("mqtt");
  if_mqtt["en"] = mqttEnabled;
  if_mqtt[F("broker")] = mqttServer;
  if_mqtt["port"] = mqttPort;
  if_mqtt[F("user")] = mqttUser;
  if_mqtt[F("pskl")] = strlen(mqttPass);
  if_mqtt[F("cid")] = mqttClientID;
  if_mqtt[F("rtn")] = retainMqttMsg;

  JsonObject if_mqtt_topics = if_mqtt.createNestedObject(F("topics"));
  if_mqtt_topics[F("device")] = mqttDeviceTopic;
  if_mqtt_topics[F("group")] = mqttGroupTopic;
#endif

#ifndef WLED_DISABLE_HUESYNC
  JsonObject if_hue = interfaces.createNestedObject("hue");
  if_hue["en"] = huePollingEnabled;
  if_hue["id"] = huePollLightId;
  if_hue[F("iv")] = huePollIntervalMs / 100;

  JsonObject if_hue_recv = if_hue.createNestedObject(F("recv"));
  if_hue_recv["on"] = hueApplyOnOff;
  if_hue_recv["bri"] = hueApplyBri;
  if_hue_recv["col"] = hueApplyColor;

  JsonArray if_hue_ip = if_hue.createNestedArray("ip");
  for (unsigned i = 0; i < 4; i++) {
    if_hue_ip.add(hueIP[i]);
  }
#endif

  JsonObject if_ntp = interfaces.createNestedObject("ntp");
  if_ntp["en"] = ntpEnabled;
  if_ntp[F("host")] = ntpServerName;
  if_ntp[F("tz")] = currentTimezone;
  if_ntp[F("offset")] = utcOffsetSecs;
  if_ntp[F("ampm")] = useAMPM;
  if_ntp[F("ln")] = longitude;
  if_ntp[F("lt")] = latitude;

  JsonObject ol = root.createNestedObject("ol");
  ol[F("clock")] = overlayCurrent;
  ol[F("cntdwn")] = countdownMode;

  ol["min"] = overlayMin;
  ol[F("max")] = overlayMax;
  ol[F("o12pix")] = analogClock12pixel;
  ol[F("o5m")] = analogClock5MinuteMarks;
  ol[F("osec")] = analogClockSecondsTrail;
  ol[F("osb")] = analogClockSolidBlack;

  JsonObject timers = root.createNestedObject(F("timers"));

  JsonObject cntdwn = timers.createNestedObject(F("cntdwn"));
  JsonArray goal = cntdwn.createNestedArray(F("goal"));
  goal.add(countdownYear); goal.add(countdownMonth); goal.add(countdownDay);
  goal.add(countdownHour); goal.add(countdownMin); goal.add(countdownSec);
  cntdwn["macro"] = macroCountdown;

  JsonArray timers_ins = timers.createNestedArray("ins");

  for (unsigned i = 0; i < 10; i++) {
    if (timerMacro[i] == 0 && timerHours[i] == 0 && timerMinutes[i] == 0) continue; // sunrise/sunset get saved always (timerHours=255)
    JsonObject timers_ins0 = timers_ins.createNestedObject();
    timers_ins0["en"] = (timerWeekday[i] & 0x01);
    timers_ins0[F("hour")] = timerHours[i];
    timers_ins0["min"] = timerMinutes[i];
    timers_ins0["macro"] = timerMacro[i];
    timers_ins0[F("dow")] = timerWeekday[i] >> 1;
    if (i<8) {
      JsonObject start = timers_ins0.createNestedObject("start");
      start["mon"] = (timerMonth[i] >> 4) & 0xF;
      start["day"] = timerDay[i];
      JsonObject end = timers_ins0.createNestedObject("end");
      end["mon"] = timerMonth[i] & 0xF;
      end["day"] = timerDayEnd[i];
    }
  }

  JsonObject ota = root.createNestedObject("ota");
  ota[F("lock")] = otaLock;
  ota[F("lock-wifi")] = wifiLock;
  ota[F("pskl")] = strlen(otaPass);
  ota[F("aota")] = aOtaEnabled;

  #ifdef WLED_ENABLE_DMX
  JsonObject dmx = root.createNestedObject("dmx");
  dmx[F("chan")] = DMXChannels;
  dmx[F("gap")] = DMXGap;
  dmx["start"] = DMXStart;
  dmx[F("start-led")] = DMXStartLED;

  JsonArray dmx_fixmap = dmx.createNestedArray(F("fixmap"));
  for (unsigned i = 0; i < 15; i++) {
    dmx_fixmap.add(DMXFixtureMap[i]);
  }

  dmx[F("e131proxy")] = e131ProxyUniverse;
  #endif

  JsonObject usermods_settings = root.createNestedObject("um");
  UsermodManager::addToConfig(usermods_settings);

  File f = WLED_FS.open(FPSTR(s_cfg_json), "w");
  if (f) serializeJson(root, f);
  f.close();
  releaseJSONBufferLock();

  doSerializeConfig = false;
}


static const char s_wsec_json[] PROGMEM = "/wsec.json";

//settings in /wsec.json, not accessible via webserver, for passwords and tokens
bool deserializeConfigSec() {
  DEBUG_PRINTLN(F("Reading settings from /wsec.json..."));

  if (!requestJSONBufferLock(3)) return false;

  bool success = readObjectFromFile(s_wsec_json, nullptr, pDoc);
  if (!success) {
    releaseJSONBufferLock();
    return false;
  }

  JsonObject root = pDoc->as<JsonObject>();

  size_t n = 0;
  JsonArray nw_ins = root["nw"]["ins"];
  if (!nw_ins.isNull()) {
    if (nw_ins.size() > 1 && nw_ins.size() > multiWiFi.size()) multiWiFi.resize(nw_ins.size()); // resize constructs objects while resizing
    for (JsonObject wifi : nw_ins) {
      char pw[65] = "";
      getStringFromJson(pw, wifi["psk"], 65);
      strlcpy(multiWiFi[n].clientPass, pw, 65);
      if (++n >= WLED_MAX_WIFI_COUNT) break;
    }
  }

  JsonObject ap = root["ap"];
  getStringFromJson(apPass, ap["psk"] , 65);

  [[maybe_unused]] JsonObject interfaces = root["if"];

#ifndef WLED_DISABLE_MQTT
  JsonObject if_mqtt = interfaces["mqtt"];
  getStringFromJson(mqttPass, if_mqtt["psk"], 65);
#endif

#ifndef WLED_DISABLE_HUESYNC
  getStringFromJson(hueApiKey, interfaces["hue"][F("key")], 47);
#endif

  getStringFromJson(settingsPIN, root["pin"], 5);
  correctPIN = !strlen(settingsPIN);

  JsonObject ota = root["ota"];
  getStringFromJson(otaPass, ota[F("pwd")], 33);
  CJSON(otaLock, ota[F("lock")]);
  CJSON(wifiLock, ota[F("lock-wifi")]);
  CJSON(aOtaEnabled, ota[F("aota")]);

  releaseJSONBufferLock();
  return true;
}

void serializeConfigSec() {
  DEBUG_PRINTLN(F("Writing settings to /wsec.json..."));

  if (!requestJSONBufferLock(4)) return;

  JsonObject root = pDoc->to<JsonObject>();

  JsonObject nw = root.createNestedObject("nw");

  JsonArray nw_ins = nw.createNestedArray("ins");
  for (size_t i = 0; i < multiWiFi.size(); i++) {
    JsonObject wifi = nw_ins.createNestedObject();
    wifi[F("psk")] = multiWiFi[i].clientPass;
  }

  JsonObject ap = root.createNestedObject("ap");
  ap["psk"] = apPass;

  [[maybe_unused]] JsonObject interfaces = root.createNestedObject("if");
#ifndef WLED_DISABLE_MQTT
  JsonObject if_mqtt = interfaces.createNestedObject("mqtt");
  if_mqtt["psk"] = mqttPass;
#endif
#ifndef WLED_DISABLE_HUESYNC
  JsonObject if_hue = interfaces.createNestedObject("hue");
  if_hue[F("key")] = hueApiKey;
#endif

  root["pin"] = settingsPIN;

  JsonObject ota = root.createNestedObject("ota");
  ota[F("pwd")] = otaPass;
  ota[F("lock")] = otaLock;
  ota[F("lock-wifi")] = wifiLock;
  ota[F("aota")] = aOtaEnabled;

  File f = WLED_FS.open(FPSTR(s_wsec_json), "w");
  if (f) serializeJson(root, f);
  f.close();
  releaseJSONBufferLock();
}

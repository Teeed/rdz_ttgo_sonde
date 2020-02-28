#include <U8x8lib.h>
#include <U8g2lib.h>

#include "Sonde.h"
#include "RS41.h"
#include "RS92.h"
#include "DFM.h"
#include "SX1278FSK.h"
#include "Display.h"

extern SX1278FSK sx1278;

RXTask rxtask = { -1, -1, -1, 0xFFFF, 0 };

const char *evstring[]={"NONE", "KEY1S", "KEY1D", "KEY1M", "KEY1L", "KEY2S", "KEY2D", "KEY2M", "KEY2L",
                               "VIEWTO", "RXTO", "NORXTO", "(max)"};

const char *RXstr[]={"RX_OK", "RX_TIMEOUT", "RX_ERROR", "RX_UNKNOWN"};

int getKeyPressEvent(); /* in RX_FSK.ino */

/* Task model:
 * There is a background task for all SX1278 interaction.
 *  - On startup and on each mode/frequency change (requested by setting requestNextSonde
 *    to an sonde index >=0) it calls Sonde::setup(), which will call the new decoder's
 *    setup function.  Setup will update the value currentSonde.
 *  - Periodically it calls Sonde::receive(), which calls the current decoder's receive()
 *    function. It should return control to the SX1278 main loop at least once per second.
 *    It will also set the internal variable receiveResult.  The decoder's receive function
 *    must make sure that there are no FIFI overflows in the SX1278.
 *  - the Arduino main loop will call the waitRXcomplete function, which should return as
 *    soon as there is some new data to display, or no later than after 1s, returning the
 *    value of receiveResult (or timeout, if receiveResult was not set within 1s). It
 *    should also return immediately if there is some keyboard input.
 */   
int initlevels[40];

Sonde::Sonde() : myIP("0.0.0.0") {
	for (int i = 0; i < 39; i++) {
		initlevels[i] = gpio_get_level((gpio_num_t)i);
  	}
	for (int i = 0; i < 39*3; i++) {
		initlevels[i%39] += gpio_get_level((gpio_num_t)(i%39));
  	}
	sondeList = (SondeInfo *)malloc((MAXSONDE+1)*sizeof(SondeInfo));
	memset(sondeList, 0, (MAXSONDE+1)*sizeof(SondeInfo));
	config.touch_thresh = 70;
	config.led_pout = 9;	
	// Try autodetecting board type
  	// Seems like on startup, GPIO4 is 1 on v1 boards, 0 on v2.1 boards?
	config.gps_rxd = -1;
	config.gps_txd = -1;
	config.oled_rst = 16;
	config.disptype = 0;
	if(initlevels[16]==0) {
		config.oled_sda = 4;
		config.oled_scl = 15;
		config.button_pin = 0;
		config.button2_pin = T4 + 128;     // T4 == GPIO13
	} else {
		config.oled_sda = 21;
		config.oled_scl = 22;
		if(initlevels[17]==0) { // T-Beam
			if(initlevels[12]==0) {  // T-Beam v1.0
				config.button_pin = 38;
				config.button2_pin = -1; //T4 + 128;  // T4 = GPIO13
				config.gps_rxd = 34;
				// for now, lets assume TFT display / SPI
				// CS=0, RST=14, RS=2, SDA=4, CLK=13
				config.disptype = 1;
				config.oled_sda = 4;
				config.oled_scl = 13;
				config.oled_rst = 14;
				config.tft_rs = 2;
				config.tft_cs = 0;
			} else {
				config.button_pin = 39;
				config.button2_pin = T4 + 128;  // T4 == GPIO13
				config.gps_rxd = 12;
				// Check if we possibly have a large display
				if(initlevels[21]==0) {
					config.disptype = 1;
					config.oled_sda = 4;
					config.oled_scl = 21;
					config.oled_rst = 22;
					config.tft_rs = 2;
					config.tft_cs = 0;
				}
			}
		} else {
			config.button_pin = 2 + 128;     // GPIO2 / T2
			config.button2_pin = 14 + 128;   // GPIO14 / T6
		}
	}
	//
	config.noisefloor = -125;
	strcpy(config.call,"NOCALL");
	strcpy(config.passcode, "---");
	config.maxsonde=15;
	config.debug=0;
	config.wifi=1;
	config.wifiap=1;
	config.display=1;
	config.startfreq=400;
	config.channelbw=10;
	config.spectrum=10;
	config.timer=0;
	config.marker=0;
	config.showafc=0;
	config.freqofs=0;
	config.rs41.agcbw=12500;
	config.rs41.rxbw=6300;
	config.rs92.rxbw=12500;
	config.rs92.alt2d=480;
	config.dfm.agcbw=20800;
	config.dfm.rxbw=10400;
	config.udpfeed.active = 1;
	config.udpfeed.type = 0;
	strcpy(config.udpfeed.host, "192.168.42.20");
	strcpy(config.udpfeed.symbol, "/O");
	config.udpfeed.port = 9002;
	config.udpfeed.highrate = 1;
	config.udpfeed.idformat = ID_DFMGRAW;
	config.tcpfeed.active = 0;
	config.tcpfeed.type = 1;
	strcpy(config.tcpfeed.host, "radiosondy.info");
	strcpy(config.tcpfeed.symbol, "/O");
	config.tcpfeed.port = 12345;
	config.tcpfeed.highrate = 10;
	config.tcpfeed.idformat = ID_DFMDXL;
}

void Sonde::setConfig(const char *cfg) {
	while(*cfg==' '||*cfg=='\t') cfg++;
	if(*cfg=='#') return;
	char *s = strchr(cfg,'=');
	if(!s) return;
	char *val = s+1;
	*s=0; s--;
	while(s>cfg && (*s==' '||*s=='\t')) { *s=0; s--; }
	Serial.printf("configuration option '%s'=%s \n", cfg, val);
	if(strcmp(cfg,"noisefloor")==0) {
		config.noisefloor = atoi(val);
		if(config.noisefloor==0) config.noisefloor=-130;
	} else if(strcmp(cfg,"call")==0) {
		strncpy(config.call, val, 9);
	} else if(strcmp(cfg,"passcode")==0) {
		strncpy(config.passcode, val, 9);
	} else if(strcmp(cfg,"button_pin")==0) {
		config.button_pin = atoi(val);
	} else if(strcmp(cfg,"button2_pin")==0) {
		config.button2_pin = atoi(val);
	} else if(strcmp(cfg,"touch_thresh")==0) {
		config.touch_thresh = atoi(val);
	} else if(strcmp(cfg,"led_pout")==0) {
		config.led_pout = atoi(val);
	} else if(strcmp(cfg,"disptype")==0) {
		config.disptype = atoi(val);
	} else if(strcmp(cfg,"oled_sda")==0) {
		config.oled_sda = atoi(val);
	} else if(strcmp(cfg,"oled_scl")==0) {
		config.oled_scl = atoi(val);
	} else if(strcmp(cfg,"oled_rst")==0) {
		config.oled_rst = atoi(val);
	} else if(strcmp(cfg,"tft_rs")==0) {
		config.tft_rs = atoi(val);
	} else if(strcmp(cfg,"tft_cs")==0) {
		config.tft_cs = atoi(val);
	} else if(strcmp(cfg,"gps_rxd")==0) {
		config.gps_rxd = atoi(val);
	} else if(strcmp(cfg,"gps_txd")==0) {
		config.gps_txd = atoi(val);
	} else if(strcmp(cfg,"maxsonde")==0) {
		config.maxsonde = atoi(val);
		if(config.maxsonde>MAXSONDE) config.maxsonde=MAXSONDE;
	} else if(strcmp(cfg,"debug")==0) {
		config.debug = atoi(val);
	} else if(strcmp(cfg,"wifi")==0) {
		config.wifi = atoi(val);			
	} else if(strcmp(cfg,"wifiap")==0) {
		config.wifiap = atoi(val);
	} else if(strcmp(cfg,"display")==0) {
		config.display = atoi(val);
		disp.setLayout(config.display);
	} else if(strcmp(cfg,"startfreq")==0) {
		config.startfreq = atoi(val);
	} else if(strcmp(cfg,"channelbw")==0) {
		config.channelbw = atoi(val);	
	} else if(strcmp(cfg,"spectrum")==0) {
		config.spectrum = atoi(val);
	} else if(strcmp(cfg,"timer")==0) {
		config.timer = atoi(val);
	} else if(strcmp(cfg,"marker")==0) {
		config.marker = atoi(val);					
	} else if(strcmp(cfg,"showafc")==0) {
		config.showafc = atoi(val);
	} else if(strcmp(cfg,"freqofs")==0) {
		config.freqofs = atoi(val);
	} else if(strcmp(cfg,"rs41.agcbw")==0) {
		config.rs41.agcbw = atoi(val);
	} else if(strcmp(cfg,"rs41.rxbw")==0) {
		config.rs41.rxbw = atoi(val);
	} else if(strcmp(cfg,"dfm.agcbw")==0) {
		config.dfm.agcbw = atoi(val);
	} else if(strcmp(cfg,"dfm.rxbw")==0) {
		config.dfm.rxbw = atoi(val);
	} else if(strcmp(cfg,"rs92.alt2d")==0) {
		config.rs92.alt2d= atoi(val);
	} else if(strcmp(cfg,"rs92.rxbw")==0) {
		config.rs92.rxbw = atoi(val);
	} else if(strcmp(cfg,"axudp.active")==0) {
		config.udpfeed.active = atoi(val)>0;
	} else if(strcmp(cfg,"axudp.host")==0) {
		strncpy(config.udpfeed.host, val, 63);
	} else if(strcmp(cfg,"axudp.port")==0) {
		config.udpfeed.port = atoi(val);
	} else if(strcmp(cfg,"axudp.symbol")==0) {
		strncpy(config.udpfeed.symbol, val, 3);
	} else if(strcmp(cfg,"axudp.highrate")==0) {
		config.udpfeed.highrate = atoi(val);
	} else if(strcmp(cfg,"axudp.idformat")==0) {
		config.udpfeed.idformat = atoi(val);
	} else if(strcmp(cfg,"tcp.active")==0) {
		config.tcpfeed.active = atoi(val)>0;
	} else if(strcmp(cfg,"tcp.host")==0) {
		strncpy(config.tcpfeed.host, val, 63);
	} else if(strcmp(cfg,"tcp.port")==0) {
		config.tcpfeed.port = atoi(val);
	} else if(strcmp(cfg,"tcp.symbol")==0) {
		strncpy(config.tcpfeed.symbol, val, 3);
	} else if(strcmp(cfg,"tcp.highrate")==0) {
		config.tcpfeed.highrate = atoi(val);
	} else if(strcmp(cfg,"tcp.idformat")==0) {
		config.tcpfeed.idformat = atoi(val);
	} else {
		Serial.printf("Invalid config option '%s'=%s \n", cfg, val);
	}
}

void Sonde::clearIP() {
	disp.clearIP();
}

void Sonde::setIP(const char *ip, bool AP) {
	disp.setIP(ip, AP);
	strncpy(myIP, ip, sizeof(myIP));
}

void Sonde::clearSonde() {
	nSonde = 0;
}
void Sonde::addSonde(float frequency, SondeType type, int active, char *launchsite)  {
	if(nSonde>=config.maxsonde) {
		Serial.println("Cannot add another sonde, MAXSONDE reached");
		return;
	}
	Serial.printf("Adding %f - %d - %d - %s\n", frequency, type, active, launchsite);
	sondeList[nSonde].type = type;
	sondeList[nSonde].freq = frequency;
	sondeList[nSonde].active = active;
	strncpy(sondeList[nSonde].launchsite, launchsite, 17);	
	memcpy(sondeList[nSonde].rxStat, "\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3\x3", 18); // unknown/undefined
	nSonde++;
}

// called by updateState (only)
void Sonde::nextConfig() {
	currentSonde++;
	if(currentSonde>=nSonde) {
		currentSonde=0;
	}
	// Skip non-active entries (but don't loop forever if there are no active ones)
	for(int i=0; i<config.maxsonde; i++) {
		if(!sondeList[currentSonde].active) {
			currentSonde++;
			if(currentSonde>=nSonde) currentSonde=0;
		}
	}
}
void Sonde::nextRxSonde() {
	rxtask.currentSonde++;
	if(rxtask.currentSonde>=nSonde) {
		rxtask.currentSonde=0;
	}
	for(int i=0; i<config.maxsonde; i++) {
		if(!sondeList[rxtask.currentSonde].active) {
			rxtask.currentSonde++;
			if(rxtask.currentSonde>=nSonde) rxtask.currentSonde=0;
		}
	}
	Serial.printf("nextRxSonde: %d\n", rxtask.currentSonde);
}
SondeInfo *Sonde::si() {
	return &sondeList[currentSonde];
}

void Sonde::setup() {
	if(rxtask.currentSonde<0 || rxtask.currentSonde>=config.maxsonde) {
		Serial.print("Invalid rxtask.currentSonde: ");
		Serial.println(rxtask.currentSonde);
		rxtask.currentSonde = 0;
	}

	// update receiver config
	Serial.print("\nSonde::setup() on sonde index ");
	Serial.println(rxtask.currentSonde);
	switch(sondeList[rxtask.currentSonde].type) {
	case STYPE_RS41:
		rs41.setup(sondeList[rxtask.currentSonde].freq * 1000000);
		break;
	case STYPE_DFM06:
	case STYPE_DFM09:
		dfm.setup( sondeList[rxtask.currentSonde].freq * 1000000, sondeList[rxtask.currentSonde].type==STYPE_DFM06?0:1 );
		break;
	case STYPE_RS92:
		rs92.setup( sondeList[rxtask.currentSonde].freq * 1000000);
	}
	// debug
	float afcbw = sx1278.getAFCBandwidth();
	float rxbw = sx1278.getRxBandwidth();
	Serial.printf("AFC BW: %f  RX BW: %f\n", afcbw, rxbw);
}

void Sonde::receive() {
	uint16_t res = 0;
	SondeInfo *si = &sondeList[rxtask.currentSonde];
	switch(si->type) {
	case STYPE_RS41:
		res = rs41.receive();
		break;
	case STYPE_RS92:
		res = rs92.receive();
		break;
	case STYPE_DFM06:
	case STYPE_DFM09:
		res = dfm.receive();
		break;
	}

	// state information for RX_TIMER / NORX_TIMER events
        if(res==0) {  // RX OK
                if(si->lastState != 1) {
                        si->rxStart = millis();
                        si->lastState = 1;
                }
        } else { // RX not ok
                if(si->lastState != 0) {
                        si->norxStart = millis();
                        si->lastState = 0;
                }
        }
	Serial.printf("debug: res was %d, now lastState is %d\n", res, si->lastState);


	// we should handle timer events here, because after returning from receive,
	// we'll directly enter setup
	rxtask.receiveSonde = rxtask.currentSonde; // pass info about decoded sonde to main loop

	int event = getKeyPressEvent();
	if (!event) event = timeoutEvent(si);
	int action = (event==EVT_NONE) ? ACT_NONE : disp.layout->actions[event];
	Serial.printf("event %x: action is %x\n", event, action);
	// If action is to move to a different sonde index, we do update things here, set activate
	// to force the sx1278 task to call sonde.setup(), and pass information about sonde to
	// main loop (display update...)
	if(action == ACT_NEXTSONDE || action==ACT_PREVSONDE) {
		// handled here...
		nextRxSonde();
		action = ACT_SONDE(rxtask.currentSonde);
		if(rxtask.activate==-1) {
			// race condition here. maybe better use mutex. TODO
			rxtask.activate = action;
		}
	}
	res = (action<<8) | (res&0xff);
	Serial.printf("receive Result is %04x\n", res);
	// let waitRXcomplete resume...
	rxtask.receiveResult = res;
}

// return (action<<8) | (rxresult)
uint16_t Sonde::waitRXcomplete() {
	uint16_t res=0;
        uint32_t t0 = millis();
rxloop:
        while( rxtask.receiveResult==0xFFFF && millis()-t0 < 2000) { delay(50); }
	if( rxtask.receiveResult == RX_UPDATERSSI ) {
		Serial.println("RSSI update");
		rxtask.receiveResult = 0xFFFF;
		disp.updateDisplayRSSI();
		goto rxloop;
	}

	if( rxtask.receiveResult==0xFFFF) {
		res = RX_TIMEOUT;
	} else {
		res = rxtask.receiveResult;
	}
        rxtask.receiveResult = 0xFFFF;
	/// TODO: THis has caused an exception when swithcing back to spectrumm...
        Serial.printf("waitRXcomplete returning %04x (%s)\n", res, (res&0xff)<4?RXstr[res&0xff]:"");
	// currently used only by RS92
	switch(sondeList[rxtask.receiveSonde].type) {
	case STYPE_RS41:
		rs41.waitRXcomplete();
		break;
	case STYPE_RS92:
		rs92.waitRXcomplete();
		break;
	case STYPE_DFM06:
	case STYPE_DFM09:
		dfm.waitRXcomplete();
		break;
	}
	memmove(sonde.si()->rxStat+1, sonde.si()->rxStat, 17);
        sonde.si()->rxStat[0] = res;
	return res;
}

uint8_t Sonde::timeoutEvent(SondeInfo *si) {
	uint32_t now = millis();
#if 1
	Serial.printf("Timeout check: %d - %d vs %d; %d - %d vs %d; %d - %d vs %d\n",
		now, si->viewStart, disp.layout->timeouts[0],
		now, si->rxStart, disp.layout->timeouts[1],
		now, si->norxStart, disp.layout->timeouts[2]);
#endif
	Serial.printf("lastState is %d\n", si->lastState);
	if(disp.layout->timeouts[0]>=0 && now - si->viewStart >= disp.layout->timeouts[0]) {
		Serial.println("View timer expired");
		return EVT_VIEWTO;
	}
	if(si->lastState==1 && disp.layout->timeouts[1]>=0 && now - si->rxStart >= disp.layout->timeouts[1]) {
		Serial.println("RX timer expired");
		return EVT_RXTO;
	}
	if(si->lastState==0 && disp.layout->timeouts[2]>=0 && now - si->norxStart >= disp.layout->timeouts[2]) {
		Serial.println("NORX timer expired");
		return EVT_NORXTO;
	}
	return 0;
}

uint8_t Sonde::updateState(uint8_t event) {
	Serial.printf("Sonde::updateState for event %d\n", event);
	// No change
	if(event==ACT_NONE) return 0xFF;

	// In all cases (new display mode, new sonde) we reset the mode change timers
	sonde.sondeList[sonde.currentSonde].viewStart = millis();
        sonde.sondeList[sonde.currentSonde].lastState = -1;

	// Moving to a different display mode
	if (event==ACT_DISPLAY_SPECTRUM || event==ACT_DISPLAY_WIFI) {
		// main loop will call setMode() and disable sx1278 background task
		return event;
	}
	int n = event;
	if(event==ACT_DISPLAY_DEFAULT) {
		n = config.display;
	}
	if(n>=0&&n<5) {
		Serial.printf("Setting display mode %d\n", n);
		disp.setLayout(n);
		sonde.clearDisplay();
		return 0xFF;
	}		

	// Moving to a different value for currentSonde
	// TODO: THis should be done in sx1278 task, not in main loop!!!!!
	if(event==ACT_NEXTSONDE) {
		sonde.nextConfig();
		Serial.printf("advancing to next sonde %d\n", sonde.currentSonde);
		return event;
	}
	if (event==ACT_PREVSONDE) {
		// TODO
		Serial.printf("previous not supported, advancing to next sonde\n");
		sonde.nextConfig();
		return ACT_NEXTSONDE;
	}
	if(event&0x80) {
		sonde.currentSonde = (event&0x7F);
		return ACT_NEXTSONDE;
	}
	return 0xFF;
}

void Sonde::updateDisplayPos() {
	disp.updateDisplayPos();
}

void Sonde::updateDisplayPos2() {
	disp.updateDisplayPos2();
}

void Sonde::updateDisplayID() {
	disp.updateDisplayID();
}
	
void Sonde::updateDisplayRSSI() {
	disp.updateDisplayRSSI();
}

void Sonde::updateStat() {
	disp.updateStat();
}

void Sonde::updateDisplayRXConfig() {
	disp.updateDisplayRXConfig();
}

void Sonde::updateDisplayIP() {
	disp.updateDisplayIP();
}

void Sonde::updateDisplayScanner() {
	disp.setLayout(0);
	disp.updateDisplay();
	disp.setLayout(config.display);
}

void Sonde::updateDisplay()
{
	disp.updateDisplay();
}

void Sonde::clearDisplay() {
	disp.rdis->clear();
}

Sonde sonde = Sonde();

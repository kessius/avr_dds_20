//*****************************************************************************
//
// File Name	: 'main.c'
// Title		: AVR DDS2 signal generator
// Author		: Scienceprog.com - Copyright (C) 2008
// Created		: 2008-03-09
// Revised		: 2008-03-09
// Version		: 2.0
// Target MCU	: Atmel AVR series ATmega16
//
// This code is distributed under the GNU Public License
//		which can be found at http://www.gnu.org/licenses/gpl.txt
//
//*****************************************************************************
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <inttypes.h>
#include "lcd_lib.h"

// define R2R port
#define R2RPORT PORTA
#define R2RDDR  DDRA

// define button port and dedicated pins (PD0-4)
#define BPORT   PORTD
#define BPIN    PIND
#define BDDR    DDRD
#define DOWN    0
#define LEFT    1
#define START   2
#define RIGHT   3
#define UP      4
#define OPT     6
#define BPORT2  PORTB
#define BDDR2   DDRB
#define BTN_INT 2

// define Highs Speed (HS) signal output (PD5)
#define HSDDR   DDRD
#define HSPORT  PORTD
#define HSPIN   PIND
#define HS      5

// define eeprom addresses
#define EE_CONFIG     0
#define EE_INIT       E2END

#define CPU_FREQ            16000000ul
#define OUT_TICKS           10
#define OUT_SYNC_TICKS      15
#define SWEEP_OUT_TICKS     9
#define ACC_FRAC_BITS       24
#define SWEEP_ACC_FRAC_BITS 16
#define SIGNAL_BUFFER_SIZE  256

#define MIN_FREQ      0.0        // minimum DDS frequency
#define MAX_FREQ      250000.0   // maximum DDS frequency
#define MIN_FREQ_STEP 0.001      // minimum DDS frequency step
#define MAX_FREQ_STEP 10000.0    // maximum DDS frequency step
#define MIN_FREQ_INC  0.0        // minimum sweep frequency increment
#define MAX_FREQ_INC  100.0      // maximum sweep frequency increment
#define MIN_FREQ_CAL  0.09
#define MAX_FREQ_CAL  1.01
#define STEP_FREQ_CAL 0.000001
#define MIN_PULSE     0.001      // minimum pulse duration, ms
#define MAX_PULSE     1000.0     // maximum pulse duration, ms

void timer2Init(void);
void timer2Start(void);
void timer2Stop(void);
void timer1Start(uint8_t);
void timer1StartPwm(uint16_t);
void timer1Stop(void);
inline void static signalOut(const uint8_t *, uint8_t, uint8_t, uint8_t, uint8_t);
inline void static signalWithSyncOut(const uint8_t *, uint8_t, uint8_t, uint8_t, uint8_t);
inline void static randomSignalOut(const uint8_t *);
inline void static sweepOut(const uint8_t *, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);

// button processing
typedef void (ButtonHandlerFn_t)(void);
void processButton(void);
void buttonNop(void);
void menu_onUp(void);
void menu_onDown(void);
void menu_onOpt(void);
void optMenu_onUp(void);
void optMenu_onDown(void);
void optMenu_onOpt(void);
void signal_onLeft(void);
void signal_onRight(void);
void signal_onStart(void);
void freqStep_onLeft(void);
void freqStep_onRight(void);
void freqMode_onLeft(void);
void freqMode_onRight(void);
void noise_onStart(void);
void pulse_onStart(void);
void pulse_onLeft(void);
void pulse_onRight(void);
void hs_onLeft(void);
void hs_onRight(void);
void hs_onStart(void);
void pwm_onUp(void);
void pwm_onDown(void);
void pwm_onLeft(void);
void pwm_onRight(void);
void pwm_onStart(void);
void pwmHs_onUp(void);
void pwmHs_onDown(void);
void pwmHs_onLeft(void);
void pwmHs_onRight(void);
void pwmHs_onStart(void);
void sweep_onUp(void);
void sweep_onDown(void);
void sweep_onLeft(void);
void sweep_onRight(void);
void sweep_onStart(void);
void offLevel_onLeft(void);
void offLevel_onRight(void);
void syncOut_onLeft(void);
void syncOut_onRight(void);
void syncOut_onOpt(void);
void trigger_onLeft(void);
void trigger_onRight(void);
void calFreq_onLeft(void);
void calFreq_onRight(void);
void calFreq_onStart(void);

// menu processing
typedef void (MenuItemEnterHandlerFn_t)(void);
void signal_updateDisplay(void);
void noise_updateDisplay(void);
void pulse_updateDisplay(void);
void freqStep_updateDisplay(void);
void freqMode_updateDisplay(void);
void hs_updateDisplay(void);
void pwm_updateDisplay(void);
void pwmHs_updateDisplay(void);
void sweep_updateDisplay(void);
void offLevel_updateDisplay(void);
void syncOut_updateDisplay(void);
void trigger_updateDisplay(void);
void calFreq_updateDisplay(void);

// adjust LCDsendChar() function for strema
static int LCDsendstream(char c, FILE *stream);
// set output stream to LCD
static FILE lcd_str = FDEV_SETUP_STREAM(LCDsendstream, NULL, _FDEV_SETUP_WRITE);

struct ButtonHandlers {
	ButtonHandlerFn_t * onUp;
	ButtonHandlerFn_t * onDown;
	ButtonHandlerFn_t * onLeft;
	ButtonHandlerFn_t * onRight;
	ButtonHandlerFn_t * onStart;
	ButtonHandlerFn_t * onOpt;
};

struct MenuEntry {
	const char * title;
	const void * data;
	MenuItemEnterHandlerFn_t * updateDisplay;
	struct ButtonHandlers buttonHandlers;
};

enum SyncOut {
	SyncOut_Off,
	SyncOut_Single,
	SyncOut_Multiple,
	SyncOut_Trigger,
	SyncOut_End
};

enum FreqMode {
	FreqMode_Exact,
	FreqMode_Jitter
};

struct Config {
	uint8_t       menuEntry;     // active or last active main menu entry
	double        freq;          // frequency value, Hz
	double        freqCal;       // frequence calibration coefficient
	double        freqEnd;       // end frequency for sweep, Hz
	double        freqInc;       // frequency increment for sweep, Hz
	uint8_t       hsFreq;        // high speed frequency [1..8 MHz]
	double        freqStep;      // frequency step value, Hz
	enum FreqMode freqMode;
	uint16_t      pwmFreq;       // PWM freq [61..62500 Hz]
	uint8_t       pwmDuty;
	uint8_t       offLevel;      // output value when then generator if off
	double        pulse;         // pulse duration, ms
	enum SyncOut  syncOut;
	double        triggerDelay;  // deleay after trigger detection, ms
};

struct Config config = {
	.menuEntry    = 0,
	.freq         = 1000.0,
	.freqCal      = 1.0000,
	.freqEnd      = 20000.0,
	.freqInc      = 0.1,
	.hsFreq       = 1,           // default 1MHz HS signal freq
	.freqStep     = 100.0,
	.pwmFreq      = 62500,
	.pwmDuty      = 127,
	.offLevel     = 0x80,        // middle of the scale
	.pulse        = 1.0,
	.syncOut      = SyncOut_Off,
	.triggerDelay = 0.0,
};

volatile bool running; // generator on/off

//define signals
const uint8_t SINE_WAVE[] PROGMEM = {
	0x80,0x83,0x86,0x89,0x8c,0x8f,0x92,0x95,0x98,0x9c,0x9f,0xa2,0xa5,0xa8,0xab,0xae,
	0xb0,0xb3,0xb6,0xb9,0xbc,0xbf,0xc1,0xc4,0xc7,0xc9,0xcc,0xce,0xd1,0xd3,0xd5,0xd8,
	0xda,0xdc,0xde,0xe0,0xe2,0xe4,0xe6,0xe8,0xea,0xec,0xed,0xef,0xf0,0xf2,0xf3,0xf5,
	0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfc,0xfd,0xfe,0xfe,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xfe,0xfe,0xfd,0xfc,0xfc,0xfb,0xfa,0xf9,0xf8,0xf7,
	0xf6,0xf5,0xf3,0xf2,0xf0,0xef,0xed,0xec,0xea,0xe8,0xe6,0xe4,0xe2,0xe0,0xde,0xdc,
	0xda,0xd8,0xd5,0xd3,0xd1,0xce,0xcc,0xc9,0xc7,0xc4,0xc1,0xbf,0xbc,0xb9,0xb6,0xb3,
	0xb0,0xae,0xab,0xa8,0xa5,0xa2,0x9f,0x9c,0x98,0x95,0x92,0x8f,0x8c,0x89,0x86,0x83,
	0x80,0x7c,0x79,0x76,0x73,0x70,0x6d,0x6a,0x67,0x63,0x60,0x5d,0x5a,0x57,0x54,0x51,
	0x4f,0x4c,0x49,0x46,0x43,0x40,0x3e,0x3b,0x38,0x36,0x33,0x31,0x2e,0x2c,0x2a,0x27,
	0x25,0x23,0x21,0x1f,0x1d,0x1b,0x19,0x17,0x15,0x13,0x12,0x10,0x0f,0x0d,0x0c,0x0a,
	0x09,0x08,0x07,0x06,0x05,0x04,0x03,0x03,0x02,0x01,0x01,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x02,0x03,0x03,0x04,0x05,0x06,0x07,0x08,
	0x09,0x0a,0x0c,0x0d,0x0f,0x10,0x12,0x13,0x15,0x17,0x19,0x1b,0x1d,0x1f,0x21,0x23,
	0x25,0x27,0x2a,0x2c,0x2e,0x31,0x33,0x36,0x38,0x3b,0x3e,0x40,0x43,0x46,0x49,0x4c,
	0x4f,0x51,0x54,0x57,0x5a,0x5d,0x60,0x63,0x67,0x6a,0x6d,0x70,0x73,0x76,0x79,0x7c
};

const uint8_t SQUARE_WAVE[] PROGMEM = {
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
};

const uint8_t SAWTOOTH_WAVE[] PROGMEM = {
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
	0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
	0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
	0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
	0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,
	0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,
	0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,
	0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f,
	0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f,
	0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f,
	0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf,
	0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,
	0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf,
	0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,
	0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef,
	0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff
};

const uint8_t REV_SAWTOOTH_WAVE[] PROGMEM = {
	0xff,0xfe,0xfd,0xfc,0xfb,0xfa,0xf9,0xf8,0xf7,0xf6,0xf5,0xf4,0xf3,0xf2,0xf1,0xf0,
	0xef,0xee,0xed,0xec,0xeb,0xea,0xe9,0xe8,0xe7,0xe6,0xe5,0xe4,0xe3,0xe2,0xe1,0xe0,
	0xdf,0xde,0xdd,0xdc,0xdb,0xda,0xd9,0xd8,0xd7,0xd6,0xd5,0xd4,0xd3,0xd2,0xd1,0xd0,
	0xcf,0xce,0xcd,0xcc,0xcb,0xca,0xc9,0xc8,0xc7,0xc6,0xc5,0xc4,0xc3,0xc2,0xc1,0xc0,
	0xbf,0xbe,0xbd,0xbc,0xbb,0xba,0xb9,0xb8,0xb7,0xb6,0xb5,0xb4,0xb3,0xb2,0xb1,0xb0,
	0xaf,0xae,0xad,0xac,0xab,0xaa,0xa9,0xa8,0xa7,0xa6,0xa5,0xa4,0xa3,0xa2,0xa1,0xa0,
	0x9f,0x9e,0x9d,0x9c,0x9b,0x9a,0x99,0x98,0x97,0x96,0x95,0x94,0x93,0x92,0x91,0x90,
	0x8f,0x8e,0x8d,0x8c,0x8b,0x8a,0x89,0x88,0x87,0x86,0x85,0x84,0x83,0x82,0x81,0x80,
	0x7f,0x7e,0x7d,0x7c,0x7b,0x7a,0x79,0x78,0x77,0x76,0x75,0x74,0x73,0x72,0x71,0x70,
	0x6f,0x6e,0x6d,0x6c,0x6b,0x6a,0x69,0x68,0x67,0x66,0x65,0x64,0x63,0x62,0x61,0x60,
	0x5f,0x5e,0x5d,0x5c,0x5b,0x5a,0x59,0x58,0x57,0x56,0x55,0x54,0x53,0x52,0x51,0x50,
	0x4f,0x4e,0x4d,0x4c,0x4b,0x4a,0x49,0x48,0x47,0x46,0x45,0x44,0x43,0x42,0x41,0x40,
	0x3f,0x3e,0x3d,0x3c,0x3b,0x3a,0x39,0x38,0x37,0x36,0x35,0x34,0x33,0x32,0x31,0x30,
	0x2f,0x2e,0x2d,0x2c,0x2b,0x2a,0x29,0x28,0x27,0x26,0x25,0x24,0x23,0x22,0x21,0x20,
	0x1f,0x1e,0x1d,0x1c,0x1b,0x1a,0x19,0x18,0x17,0x16,0x15,0x14,0x13,0x12,0x11,0x10,
	0x0f,0x0e,0x0d,0x0c,0x0b,0x0a,0x09,0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01,0x00,
};

const uint8_t TRIANGLE_WAVE[] PROGMEM = {
	0x00,0x02,0x04,0x06,0x08,0x0a,0x0c,0x0e,0x10,0x12,0x14,0x16,0x18,0x1a,0x1c,0x1e,
	0x20,0x22,0x24,0x26,0x28,0x2a,0x2c,0x2e,0x30,0x32,0x34,0x36,0x38,0x3a,0x3c,0x3e,
	0x40,0x42,0x44,0x46,0x48,0x4a,0x4c,0x4e,0x50,0x52,0x54,0x56,0x58,0x5a,0x5c,0x5e,
	0x60,0x62,0x64,0x66,0x68,0x6a,0x6c,0x6e,0x70,0x72,0x74,0x76,0x78,0x7a,0x7c,0x7e,
	0x80,0x82,0x84,0x86,0x88,0x8a,0x8c,0x8e,0x90,0x92,0x94,0x96,0x98,0x9a,0x9c,0x9e,
	0xa0,0xa2,0xa4,0xa6,0xa8,0xaa,0xac,0xae,0xb0,0xb2,0xb4,0xb6,0xb8,0xba,0xbc,0xbe,
	0xc0,0xc2,0xc4,0xc6,0xc8,0xca,0xcc,0xce,0xd0,0xd2,0xd4,0xd6,0xd8,0xda,0xdc,0xde,
	0xe0,0xe2,0xe4,0xe6,0xe8,0xea,0xec,0xee,0xf0,0xf2,0xf4,0xf6,0xf8,0xfa,0xfc,0xfe,
	0xff,0xfd,0xfb,0xf9,0xf7,0xf5,0xf3,0xf1,0xef,0xef,0xeb,0xe9,0xe7,0xe5,0xe3,0xe1,
	0xdf,0xdd,0xdb,0xd9,0xd7,0xd5,0xd3,0xd1,0xcf,0xcf,0xcb,0xc9,0xc7,0xc5,0xc3,0xc1,
	0xbf,0xbd,0xbb,0xb9,0xb7,0xb5,0xb3,0xb1,0xaf,0xaf,0xab,0xa9,0xa7,0xa5,0xa3,0xa1,
	0x9f,0x9d,0x9b,0x99,0x97,0x95,0x93,0x91,0x8f,0x8f,0x8b,0x89,0x87,0x85,0x83,0x81,
	0x7f,0x7d,0x7b,0x79,0x77,0x75,0x73,0x71,0x6f,0x6f,0x6b,0x69,0x67,0x65,0x63,0x61,
	0x5f,0x5d,0x5b,0x59,0x57,0x55,0x53,0x51,0x4f,0x4f,0x4b,0x49,0x47,0x45,0x43,0x41,
	0x3f,0x3d,0x3b,0x39,0x37,0x35,0x33,0x31,0x2f,0x2f,0x2b,0x29,0x27,0x25,0x23,0x21,
	0x1f,0x1d,0x1b,0x19,0x17,0x15,0x13,0x11,0x0f,0x0f,0x0b,0x09,0x07,0x05,0x03,0x01
};

const uint8_t ECG_WAVE[] PROGMEM = {
	73,74,75,75,74,73,73,73,73,72,71,69,68,67,67,67,
	68,68,67,65,62,61,59,57,56,55,55,54,54,54,55,55,
	55,55,55,55,54,53,51,50,49,49,52,61,77,101,132,
	169,207,238,255,254,234,198,154,109,68,37,17,5,
	0,1,6,13,20,28,36,45,52,57,61,64,65,66,67,68,68,
	69,70,71,71,71,71,71,71,71,71,72,72,72,73,73,74,
	75,75,76,77,78,79,80,81,82,83,84,86,88,91,93,96,
	98,100,102,104,107,109,112,115,118,121,123,125,
	126,127,127,127,127,127,126,125,124,121,119,116,
	113,109,105,102,98,95,92,89,87,84,81,79,77,76,75,
	74,73,72,70,69,68,67,67,67,68,68,68,69,69,69,69,
	69,69,69,70,71,72,73,73,74,74,75,75,75,75,75,75,
	74,74,73,73,73,73,72,72,72,71,71,71,71,71,71,71,
	70,70,70,69,69,69,69,69,70,70,70,69,68,68,67,67,
	67,67,66,66,66,65,65,65,65,65,65,65,65,64,64,63,
	63,64,64,65,65,65,65,65,65,65,64,64,64,64,64,64,
	64,64,65,65,65,66,67,68,69,71,72,73
};

const uint8_t SINE_WAVE_FROM_ZERO[] PROGMEM = { //sine 256 values, start from 0
	0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x02,0x03,0x03,0x04,0x05,0x06,0x07,0x08,
	0x09,0x0a,0x0c,0x0d,0x0f,0x10,0x12,0x13,0x15,0x17,0x19,0x1b,0x1d,0x1f,0x21,0x23,
	0x25,0x27,0x2a,0x2c,0x2e,0x31,0x33,0x36,0x38,0x3b,0x3e,0x40,0x43,0x46,0x49,0x4c,
	0x4f,0x51,0x54,0x57,0x5a,0x5d,0x60,0x63,0x67,0x6a,0x6d,0x70,0x73,0x76,0x79,0x7c,
	0x80,0x83,0x86,0x89,0x8c,0x8f,0x92,0x95,0x98,0x9c,0x9f,0xa2,0xa5,0xa8,0xab,0xae,
	0xb0,0xb3,0xb6,0xb9,0xbc,0xbf,0xc1,0xc4,0xc7,0xc9,0xcc,0xce,0xd1,0xd3,0xd5,0xd8,
	0xda,0xdc,0xde,0xe0,0xe2,0xe4,0xe6,0xe8,0xea,0xec,0xed,0xef,0xf0,0xf2,0xf3,0xf5,
	0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfc,0xfd,0xfe,0xfe,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xfe,0xfe,0xfd,0xfc,0xfc,0xfb,0xfa,0xf9,0xf8,0xf7,
	0xf6,0xf5,0xf3,0xf2,0xf0,0xef,0xed,0xec,0xea,0xe8,0xe6,0xe4,0xe2,0xe0,0xde,0xdc,
	0xda,0xd8,0xd5,0xd3,0xd1,0xce,0xcc,0xc9,0xc7,0xc4,0xc1,0xbf,0xbc,0xb9,0xb6,0xb3,
	0xb0,0xae,0xab,0xa8,0xa5,0xa2,0x9f,0x9c,0x98,0x95,0x92,0x8f,0x8c,0x89,0x86,0x83,
	0x80,0x7c,0x79,0x76,0x73,0x70,0x6d,0x6a,0x67,0x63,0x60,0x5d,0x5a,0x57,0x54,0x51,
	0x4f,0x4c,0x49,0x46,0x43,0x40,0x3e,0x3b,0x38,0x36,0x33,0x31,0x2e,0x2c,0x2a,0x27,
	0x25,0x23,0x21,0x1f,0x1d,0x1b,0x19,0x17,0x15,0x13,0x12,0x10,0x0f,0x0d,0x0c,0x0a,
	0x09,0x08,0x07,0x06,0x05,0x04,0x03,0x03,0x02,0x01,0x01
};

const uint8_t NOISE_SIGNAL[] PROGMEM = {
	0x0a,0x0e,0x2d,0x73,0xc4,0x40,0xaa,0x8f,0xdd,0xf3,0x6b,0x97,0xb9,0x8d,0x77,0x57,
	0xe3,0x52,0x93,0x3f,0x25,0x07,0x99,0x5f,0x8b,0x37,0x30,0x7b,0x3a,0x89,0xc6,0xae,
	0x4e,0x58,0xe4,0x4b,0x48,0x05,0xd6,0xf2,0x5c,0x44,0xef,0xf8,0x69,0xf6,0x92,0x56,
	0x1d,0x96,0xab,0x2f,0x88,0x35,0xf5,0x36,0x83,0xfc,0x8e,0x60,0xe0,0xda,0xa8,0x5b,
	0xdf,0x7e,0x4d,0x3b,0x38,0x91,0x2b,0xfa,0x21,0xc2,0x23,0x0d,0x2e,0xce,0x3c,0xb6,
	0x03,0x32,0xed,0x86,0xe6,0x0f,0x55,0x6a,0x34,0xb8,0x70,0x45,0x49,0x9b,0x76,0xbc,
	0x18,0x5a,0x41,0x46,0x28,0xfd,0x2c,0xb0,0xea,0xb2,0xde,0x65,0xbb,0x10,0x59,0xf1,
	0x9d,0xb7,0x29,0xd4,0xeb,0x42,0x85,0x6f,0x39,0xd5,0x26,0x90,0x7f,0xa7,0xe8,0xd9,
	0x98,0xc1,0x8c,0x11,0x62,0xad,0x81,0x66,0x0c,0x5d,0x19,0x01,0x1e,0xc8,0x87,0xe1,
	0x2a,0xd2,0x24,0xd1,0x43,0xe7,0x4f,0x68,0xc0,0xaf,0x5e,0x9e,0x84,0xe2,0x50,0xcb,
	0x1a,0xc3,0xb4,0x74,0x04,0xac,0x64,0xa0,0x13,0xd3,0x31,0x00,0x9c,0xfe,0x4a,0xb3,
	0x78,0x15,0x3e,0xee,0x94,0x7c,0x1c,0x72,0xa1,0x20,0x9f,0x95,0xcf,0x3d,0x82,0xb5,
	0xbd,0x54,0xa6,0x47,0x6e,0x75,0xc7,0x1b,0xd7,0x09,0x16,0xf0,0x12,0x02,0xb1,0x06,
	0x4c,0xcd,0xa9,0xa2,0x6c,0xa5,0x61,0xca,0x7d,0x1f,0x22,0x17,0x14,0xc5,0xd8,0x6d,
	0x8a,0xf7,0x51,0xa3,0xfb,0xf4,0x63,0xbf,0x79,0xc9,0x27,0xec,0x7a,0x9a,0xbe,0x80,
	0xff,0xe5,0xba,0xcc,0x0b,0xdb,0xdc,0xf9,0x67,0xe9,0xa4,0x08,0xd0,0x71,0x33,0x53
};

const uint8_t * const SIGNALS[] PROGMEM = {
	SINE_WAVE,
	SQUARE_WAVE,
	TRIANGLE_WAVE,
	SAWTOOTH_WAVE,
	REV_SAWTOOTH_WAVE,
	ECG_WAVE
};

const char SINE_TITLE[]      PROGMEM = "      Sine      ";
const char SQUARE_TITLE[]    PROGMEM = "     Square     ";
const char TRIANGLE_TITLE[]  PROGMEM = "    Triangle    ";
const char SAW_TITLE[]       PROGMEM = "    SawTooth    ";
const char REV_SAW_TITLE[]   PROGMEM = "  Rev SawTooth  ";
const char ECG_TITLE[]       PROGMEM = "      ECG       ";
const char FREQ_STEP_TITLE[] PROGMEM = "   Freq Step    ";
const char FREQ_MODE_TITLE[] PROGMEM = "   Freq Mode    ";
const char NOISE_TITLE[]     PROGMEM = "     Noise      ";
const char PULSE_TITLE[]     PROGMEM = "     Pulse      ";
const char HS_TITLE[]        PROGMEM = "   High Speed   ";
const char PWM_TITLE[]       PROGMEM = "      PWM       ";
const char PWM_HS_TITLE[]    PROGMEM = " PWM (HS)       ";
const char SWEEP_TITLE[]     PROGMEM = "     Sweep      ";
const char SWEEP_END_TITLE[] PROGMEM = "     Sweep   End";
const char SWEEP_INC_TITLE[] PROGMEM = "     Sweep  Step";
const char OFF_LEVEL_TITLE[] PROGMEM = "   Off Level    ";
const char SYNC_OUT_TITLE[]  PROGMEM = "  Sync Output   ";
const char TRIGGER_TITLE[]   PROGMEM = " Trigger Delay  ";
const char CAL_FREQ_TITLE[]  PROGMEM = " Calibrate Freq ";

const struct MenuEntry MENU[] PROGMEM = {
	{
		SINE_TITLE,
		SINE_WAVE,
		signal_updateDisplay,
		{
			menu_onUp,
			menu_onDown,
			signal_onLeft,
			signal_onRight,
			signal_onStart,
			menu_onOpt,
		}
	},
	{
		SQUARE_TITLE,
		SQUARE_WAVE,
		signal_updateDisplay,
		{
			menu_onUp,
			menu_onDown,
			signal_onLeft,
			signal_onRight,
			signal_onStart,
			menu_onOpt,
		}
	},
	{
		TRIANGLE_TITLE,
		TRIANGLE_WAVE,
		signal_updateDisplay,
		{
			menu_onUp,
			menu_onDown,
			signal_onLeft,
			signal_onRight,
			signal_onStart,
			menu_onOpt,
		}
	},
	{
		SAW_TITLE,
		SAWTOOTH_WAVE,
		signal_updateDisplay,
		{
			menu_onUp,
			menu_onDown,
			signal_onLeft,
			signal_onRight,
			signal_onStart,
			menu_onOpt,
		}
	},
	{
		REV_SAW_TITLE,
		REV_SAWTOOTH_WAVE,
		signal_updateDisplay,
		{
			menu_onUp,
			menu_onDown,
			signal_onLeft,
			signal_onRight,
			signal_onStart,
			menu_onOpt,
		}
	},
	{
		ECG_TITLE,
		ECG_WAVE,
		signal_updateDisplay,
		{
			menu_onUp,
			menu_onDown,
			signal_onLeft,
			signal_onRight,
			signal_onStart,
			menu_onOpt,
		}
	},
	{
		NOISE_TITLE,
		NULL,
		noise_updateDisplay,
		{
			menu_onUp,
			menu_onDown,
			buttonNop,
			buttonNop,
			noise_onStart,
			menu_onOpt,
		}
	},
	{
		PULSE_TITLE,
		NULL,
		pulse_updateDisplay,
		{
			menu_onUp,
			menu_onDown,
			pulse_onLeft,
			pulse_onRight,
			pulse_onStart,
			menu_onOpt,
		}
	},
	{
		HS_TITLE,
		NULL,
		hs_updateDisplay,
		{
			menu_onUp,
			menu_onDown,
			hs_onLeft,
			hs_onRight,
			hs_onStart,
			menu_onOpt,
		}
	},
	{
		PWM_TITLE,
		NULL,
		pwm_updateDisplay,
		{
			pwm_onUp,
			pwm_onDown,
			signal_onLeft,
			signal_onRight,
			pwm_onStart,
			menu_onOpt,
		}
	},
	{
		PWM_HS_TITLE,
		NULL,
		pwmHs_updateDisplay,
		{
			pwmHs_onUp,
			pwmHs_onDown,
			pwmHs_onLeft,
			pwmHs_onRight,
			pwmHs_onStart,
			menu_onOpt,
		}
	},
	{
		SWEEP_TITLE,
		NULL,
		sweep_updateDisplay,
		{
			sweep_onUp,
			sweep_onDown,
			sweep_onLeft,
			sweep_onRight,
			sweep_onStart,
			menu_onOpt,
		}
	},
};
static const uint8_t MENU_SIZE = (sizeof(MENU)/sizeof(MENU[0]));

const struct MenuEntry OPT_MENU[] PROGMEM = {
	{
		FREQ_STEP_TITLE,
		NULL,
		freqStep_updateDisplay,
		{
			optMenu_onUp,
			optMenu_onDown,
			freqStep_onLeft,
			freqStep_onRight,
			optMenu_onOpt,
			optMenu_onOpt,
		}
	},
	{
		FREQ_MODE_TITLE,
		NULL,
		freqMode_updateDisplay,
		{
			optMenu_onUp,
			optMenu_onDown,
			freqMode_onLeft,
			freqMode_onRight,
			optMenu_onOpt,
			optMenu_onOpt,
		}
	},
	{
		OFF_LEVEL_TITLE,
		NULL,
		offLevel_updateDisplay,
		{
			optMenu_onUp,
			optMenu_onDown,
			offLevel_onLeft,
			offLevel_onRight,
			optMenu_onOpt,
			optMenu_onOpt,
		}
	},
	{
		SYNC_OUT_TITLE,
		NULL,
		syncOut_updateDisplay,
		{
			optMenu_onUp,
			optMenu_onDown,
			syncOut_onLeft,
			syncOut_onRight,
			syncOut_onOpt,
			syncOut_onOpt,
		}
	},
	{
		TRIGGER_TITLE,
		NULL,
		trigger_updateDisplay,
		{
			optMenu_onUp,
			optMenu_onDown,
			trigger_onLeft,
			trigger_onRight,
			optMenu_onOpt,
			optMenu_onOpt,
		}
	},
	{
		CAL_FREQ_TITLE,
		NULL,
		calFreq_updateDisplay,
		{
			optMenu_onUp,
			optMenu_onDown,
			calFreq_onLeft,
			calFreq_onRight,
			calFreq_onStart,
			optMenu_onOpt,
		}
	},
};
static const uint8_t OPT_MENU_SIZE = (sizeof(OPT_MENU)/sizeof(OPT_MENU[0]));

//various LCD strings
const char MNON[]   PROGMEM = "ON ";
const char MNOFF[]  PROGMEM = "OFF";
const char MNDIS[]  PROGMEM = "DIS";
const char MNTRIG[] PROGMEM = "TRG";
const char RND[]    PROGMEM = "    Random";

enum Button {
	Button_None,
	Button_Up,
	Button_Right,
	Button_Down,
	Button_Left,
	Button_Start,
	Button_Opt,
};

struct ButtonState {
	uint16_t now;
	uint16_t pressedTime;
	uint16_t autoTime;
	bool     autoRepeat;
	volatile enum Button pressed;
	volatile bool processed;
};

struct ButtonState buttonState;
static const uint16_t BUTTON_UNBOUNCE    = 20;
static const uint16_t BUTTON_AUTO_START  = 100;
static const uint16_t BUTTON_AUTO_REPEAT = 8;

uint8_t optMenuEntryNum = (uint8_t)-1;   // active opt-menu entry or -1 if not in the opt-menu
struct MenuEntry menuEntry;              // copy of active menu entry
struct ButtonHandlers * buttonHandlers;
uint8_t submenuLevel = 0;                // used by the seep only

uint8_t signalBuffer[SIGNAL_BUFFER_SIZE]
	__attribute__ ((aligned(SIGNAL_BUFFER_SIZE)))
	__attribute__ ((section (".noinit")));

// adjust LCD stream fuinction to use with printf()
static int LCDsendstream(char c , FILE *stream) {
	LCDsendChar(c);
	return 0;
}

inline uint32_t delayMsToCount(double ms) {
	return (double)(CPU_FREQ / 1000) * ms / 6;
}

inline void delayCount(uint32_t count) {
	asm volatile(
		// takes 6 cycles
		"1:" "\n\t"
		"subi %[c0], 1		; 1 c" 		"\n\t"
		"sbci %[c1], 0 		; 1 c" 		"\n\t"
		"sbci %[c2], 0 		; 1 c" 		"\n\t"
		"sbci %[c3], 0 		; 1 c" 		"\n\t"
		"brne 1b 		; 2 c" 		"\n\t"
		:
		: [c3] "d"((uint8_t)(count >> 24)),
		  [c2] "d"((uint8_t)(count >> 16)),
		  [c1] "d"((uint8_t)(count >> 8)),
		  [c0] "d"((uint8_t)count)
	);
}

// initialize Timer2 (used for button reading)
void timer2Init(void) {
	TCNT2 = 0x00;
} 

void timer2Start(void) {
	TCCR2 |= (1 << CS22) | (1 << CS21); // prescaller 256 => ~244 interrupts/s
	TIMSK |= (1 << TOV2);               // enable overflow interrupts
}

void timer2Stop(void) {
	TCCR0 &= ~((1 << CS22) | (1 << CS21)); // stop
	TIMSK &= ~(1 << TOV2);                 // disable overflow interrupts
}

// External interrupts service routines
// used to stop DDS in the inline ASM by setting
// CPHA bit in SPCR register
ISR(INT0_vect) {
	SPCR |= (1 << CPHA);
}
ISR(INT1_vect) {
	SPCR |= (1 << CPHA);
}
ISR(INT2_vect) {
	SPCR |= (1 << CPHA);
}

// called every 4.1 ms, takes ~4 us
void checkButtons(void) {
	++buttonState.now;

	enum Button newButton;
	if(bit_is_clear(BPIN, UP))
		newButton = Button_Up;
	else if(bit_is_clear(BPIN, RIGHT))
		newButton = Button_Right;
	else if(bit_is_clear(BPIN, DOWN))
		newButton = Button_Down;
	else if(bit_is_clear(BPIN, LEFT))
		newButton = Button_Left;
	else if(bit_is_clear(BPIN, START))
		newButton = Button_Start;
	else if(bit_is_clear(BPIN, OPT))
		newButton = Button_Opt;
	else
		newButton = Button_None;

	if(buttonState.pressed != newButton) {
		bool ignore = false;
		if(newButton == Button_None) {
			if((buttonState.now - buttonState.pressedTime) < BUTTON_UNBOUNCE)
				ignore = true;
		}
		
		if(!ignore) {
			buttonState.pressedTime = buttonState.now;
			buttonState.pressed     = newButton;
			buttonState.processed   = false;
			buttonState.autoTime    = buttonState.now;
			buttonState.autoRepeat  = false;
		}
	}
	else if(buttonState.pressed != Button_None) {
		if(        (!buttonState.autoRepeat && ((buttonState.now - buttonState.pressedTime) >= BUTTON_AUTO_START))
			|| ( buttonState.autoRepeat && ((buttonState.now - buttonState.pressedTime) >= BUTTON_AUTO_REPEAT)))
		{
			buttonState.processed   = false;
			buttonState.autoTime    = buttonState.now;
			buttonState.autoRepeat  = true;
		}
	}
}

void saveSettings(void) {
	eeprom_update_block(&config, EE_CONFIG, sizeof(config));
}

void loadSettings(void) {
	if(eeprom_read_byte((uint8_t*)EE_INIT) != 'T') {
		// save the initial hard-coded values
		saveSettings();
		eeprom_write_byte((uint8_t*)EE_INIT, 'T');   // marks once that eeprom init is done
	}

	eeprom_read_block(&config, EE_CONFIG, sizeof(config));
}

inline bool isHsOutputEnabled(void)
{
	return (config.syncOut != SyncOut_Trigger);
}

inline void setHsDirection(void)
{
	if(isHsOutputEnabled()) {
		HSDDR |=  _BV(HS); // configure HS as output
	}
	else {
		HSDDR &= ~_BV(HS); // configure HS as input
	}
}

inline void syncPulse(void)
{
	HSPORT |=  (1 << HS);
	HSPORT &= ~(1 << HS);
}

void buttonNop(void) {
}

void onNewMenuEntry(void) {
	memcpy_P(&menuEntry, &MENU[config.menuEntry], sizeof(menuEntry));
	buttonHandlers = &menuEntry.buttonHandlers;

	LCDclr();
	CopyStringtoLCD(menuEntry.title, 0, 0);
	menuEntry.updateDisplay();
}

void menu_onUp(void) {
	if(!running) {
		if(config.menuEntry == 0) config.menuEntry = MENU_SIZE - 1;
		else                      --config.menuEntry;
		onNewMenuEntry();
	}
}

void menu_onDown(void) {
	if(!running) {
		++config.menuEntry;
		if(config.menuEntry == MENU_SIZE) config.menuEntry = 0;
		onNewMenuEntry();
	}
}

void onNewOptMenuEntry(void) {
	memcpy_P(&menuEntry, &OPT_MENU[optMenuEntryNum], sizeof(menuEntry));
	buttonHandlers = &menuEntry.buttonHandlers;

	LCDclr();
	CopyStringtoLCD(menuEntry.title, 0, 0);
	menuEntry.updateDisplay();
}

void menu_onOpt(void) {
	if(!running) {
		optMenuEntryNum = 0;
		onNewOptMenuEntry();
	}
}

void optMenu_onUp(void) {
	if(!running) {
		if(optMenuEntryNum == 0) optMenuEntryNum = OPT_MENU_SIZE - 1;
		else                     --optMenuEntryNum;
		onNewOptMenuEntry();
	}
}

void optMenu_onDown(void) {
	if(!running) {
		++optMenuEntryNum;
		if(optMenuEntryNum == OPT_MENU_SIZE) optMenuEntryNum = 0;
		onNewOptMenuEntry();
	}
}

void optMenu_onOpt(void) {
	optMenuEntryNum = (uint8_t)-1;
	onNewMenuEntry();
}

void displaySignalStatus(void) {
	if(running)
		CopyStringtoLCD((config.syncOut == SyncOut_Trigger) ? MNTRIG : MNON, 13, 1);
	else
		CopyStringtoLCD(MNOFF, 13, 1);
}

void showFreq(double freq) {
	LCDGotoXY(0, 1);
	printf("%10.3fHz", freq);
}

void signal_updateDisplay(void) {
	showFreq(config.freq);
	displaySignalStatus();
}

void signal_onLeft(void) {
	config.freq -= config.freqStep;
	if(config.freq < MIN_FREQ)
		config.freq = MIN_FREQ;
	signal_updateDisplay();
}

void signal_onRight(void) {
	config.freq += config.freqStep;
	if(config.freq > MAX_FREQ)
		config.freq = MAX_FREQ;
	signal_updateDisplay();
}

void disableMenu(void) {
	while(buttonState.pressed != Button_None);       // wait until button release, otherwise the release interrupt will stop the generation
	GICR |= (1 << INT0) | (1 << INT1) | (1 << INT2); // set external interrupts to enable stop or modify

	timer2Stop();  // menu inactive
}

void enableMenu(void) {
	GICR &= ~((1 << INT0) | (1 << INT1) | (1 << INT2)); // stop external interrupts
	timer2Start();                                      // menu active
}

void signal_start(void) {
	saveSettings();

	running = true;

	menuEntry.updateDisplay();
	disableMenu();
}

uint32_t freqToAcc(double freq, uint8_t ticks) {
	double resolution = (double)CPU_FREQ / ticks / ((uint32_t)1 << ACC_FRAC_BITS) / SIGNAL_BUFFER_SIZE;
	return freq/(resolution / config.freqCal);
}

double accToFreq(uint32_t acc, uint8_t ticks) {
	double resolution = (double)CPU_FREQ / ticks / ((uint32_t)1 << ACC_FRAC_BITS) / SIGNAL_BUFFER_SIZE;
	return (double)acc * (resolution / config.freqCal);
}

void signal_recheckButtons(void) {
	enableMenu();
	while(buttonState.pressed != Button_None) {
		processButton();
	}
	disableMenu();
}

inline bool waitTrigger(void) {
	if(config.syncOut != SyncOut_Trigger) return true;

	HSDDR &= ~_BV(HS); // configure HS as input
	SPCR &= ~(1 << CPHA);

	uint32_t count = delayMsToCount(config.triggerDelay);
	if(count == 0) {
		while(true) {
			if(bit_is_set(HSPIN, HS)) return true;
			if(bit_is_set(SPCR, CPHA)) return false;
		}
	}
	else {
		while(true) {
			if(bit_is_set(HSPIN, HS)) break;
			if(bit_is_set(SPCR, CPHA)) return false;
		}
		delayCount(count);
		return true;
	}
}

void signal_continue(bool tryToCorrect) {
	uint32_t ticks = (config.syncOut == SyncOut_Multiple) ? OUT_SYNC_TICKS : OUT_TICKS;
	uint32_t acc = freqToAcc(config.freq, ticks);
	if(acc == 0) acc = 1;

	if(tryToCorrect && config.freqMode == FreqMode_Jitter) {
		// try to minimize jitter
		uint64_t k = ((uint64_t)UINT32_MAX + 1) / acc;
		acc = ((uint64_t)UINT32_MAX + 1) / k;
		double freq = accToFreq(acc, ticks);
		showFreq(freq);
	}

	SPCR &= ~(1 << CPHA); // clear CPHA bit in SPCR register to allow DDS

	switch(config.syncOut) {
		case SyncOut_Single:
			syncPulse();
			// continue
		case SyncOut_Off:
			signalOut(signalBuffer,
				(uint8_t)(acc >> 24),
				(uint8_t)(acc >> 16),
				(uint8_t)(acc >> 8),
				(uint8_t)acc);
			break;
		case SyncOut_Multiple:
			signalWithSyncOut(signalBuffer,
				(uint8_t)(acc >> 24),
				(uint8_t)(acc >> 16),
				(uint8_t)(acc >> 8),
				(uint8_t)acc);
			break;
		case SyncOut_Trigger:
			if(waitTrigger()) {
				signalOut(signalBuffer,
					(uint8_t)(acc >> 24),
					(uint8_t)(acc >> 16),
					(uint8_t)(acc >> 8),
					(uint8_t)acc);
			}
			break;
		case SyncOut_End: break;
	}

	R2RPORT = config.offLevel;

	// generation is interrupted - check buttons
	signal_recheckButtons();
}

void signal_run(void) {
	memcpy_P(signalBuffer, (const uint8_t *)menuEntry.data, sizeof(signalBuffer));
	while(running) {
		signal_continue(true);
	}
}

void signal_stop(void) {
	enableMenu();
	running = false;
	R2RPORT = config.offLevel;
	menuEntry.updateDisplay();
	while(buttonState.pressed != Button_None); // wait until button release, otherwise the generation will be started again
}

void signal_onStart(void) {
	if(!running) {
		signal_start();
		signal_run();
		signal_stop();
	}
	else {
		running = false;
	}
}

void noise_updateDisplay(void) {
	LCDGotoXY(0, 1);
	CopyStringtoLCD(RND, 0, 1);
	displaySignalStatus();
}

void noise_onStart(void) {
	signal_start();
	SPCR &= ~(1<<CPHA); // clear CPHA bit in SPCR register to allow DDS

	memcpy_P(signalBuffer, NOISE_SIGNAL, sizeof(signalBuffer));

	if(config.syncOut == SyncOut_Single || config.syncOut == SyncOut_Multiple) 
		syncPulse();

	if(waitTrigger()) {
		randomSignalOut(signalBuffer);
	}

	signal_stop();
}

void pulse_updateDisplay(void) {
	LCDGotoXY(0, 1);
	if(config.pulse == -INFINITY)
		printf("until rel ");
	else if(config.pulse == 0.0)
		printf("min       ");
	else if(config.pulse == INFINITY)
		printf("until stop");
	else
		printf("%8.3fms", config.pulse);

	displaySignalStatus();
}

void pulse_onLeft(void) {
	if(!running) {
		if(config.pulse == -INFINITY) {
		}
		else if(config.pulse == 0.0) {
			config.pulse = -INFINITY;
		}
		else if(config.pulse == INFINITY) {
			config.pulse = MAX_PULSE;
		}
		else {
			config.pulse -= config.freqStep / 100;
			if(config.pulse < MIN_PULSE)
				config.pulse = 0.0;
		}
		pulse_updateDisplay();
	}
}

void pulse_onRight(void) {
	if(!running) {
		if(config.pulse == -INFINITY) {
			config.pulse = 0.0;
		}
		else if(config.pulse == 0.0) {
			config.pulse = MIN_PULSE;
		}
		else if(config.pulse == INFINITY) {
		}
		else {
			config.pulse += config.freqStep / 100;
			if(config.pulse > MAX_PULSE)
				config.pulse = INFINITY;
		}
		pulse_updateDisplay();
	}
}

void pulse_onStart(void) {
	if(!running) {
		running = true;
		pulse_updateDisplay();
		bool hsOut = isHsOutputEnabled();
		if(waitTrigger()) {
			if(config.pulse == -INFINITY) {
				if(hsOut) HSPORT |=  (1 << HS);
				R2RPORT = 0xFF;
				while(buttonState.pressed != Button_None) {
					processButton();
				}
				if(hsOut) HSPORT &= ~(1 << HS);
				R2RPORT = config.offLevel;
			}
			else if(config.pulse == INFINITY) {
				if(hsOut) HSPORT |=  (1 << HS);
				R2RPORT = 0xFF;
				while(running) {
					processButton();
				}
				if(hsOut) HSPORT &= ~(1 << HS);
				R2RPORT = config.offLevel;
			}
			else if(config.pulse == 0.0) {
				R2RPORT = 0xFF;
				if(hsOut) {
					HSPORT |=  (1 << HS);
					HSPORT &= ~(1 << HS);
				}
				R2RPORT = config.offLevel;
			}
			else {
				uint32_t count = delayMsToCount(config.pulse);
				R2RPORT = 0xFF;
				if(hsOut) HSPORT |=  (1 << HS);
				delayCount(count);
				if(hsOut) HSPORT &= ~(1 << HS);
				R2RPORT = config.offLevel;
			}
		}
		running = false;
		pulse_updateDisplay();
	}
	else {
		running = false;
	}
}

void freqStep_updateDisplay(void) {
	LCDGotoXY(0, 1);
	printf("%10.3fHz", config.freqStep);
}

void freqStep_onLeft(void) {
	config.freqStep /= 10;
	if(config.freqStep < MIN_FREQ_STEP)
		config.freqStep = MIN_FREQ_STEP;
	freqStep_updateDisplay();
}

void freqStep_onRight(void) {
	config.freqStep *= 10;
	if(config.freqStep > MAX_FREQ_STEP)
		config.freqStep = MAX_FREQ_STEP;
	freqStep_updateDisplay();
}

void freqMode_updateDisplay(void) {
	LCDGotoXY(0, 1);
	switch(config.freqMode) {
		case FreqMode_Exact:    printf("Exact      "); break; 
		case FreqMode_Jitter:   printf("Min. jitter"); break;
	}
}

void freqMode_onLeft(void) {
	if(config.freqMode == FreqMode_Jitter) config.freqMode = FreqMode_Exact;
	freqMode_updateDisplay();
}

void freqMode_onRight(void) {
	if(config.freqMode == FreqMode_Exact) config.freqMode = FreqMode_Jitter;
	freqMode_updateDisplay();
}

void displayHsOutputStatus(void) {
	if(isHsOutputEnabled())
		CopyStringtoLCD(running ? MNON : MNOFF, 13, 1);
	else
		CopyStringtoLCD(MNDIS, 13, 1);
}

void hs_updateDisplay(void) {
	LCDGotoXY(0, 1);
	printf(" %5uMHz", config.hsFreq);
	displayHsOutputStatus();
}

void hs_restart(void) {
	if(running)  {
		timer1Start(config.hsFreq);
	}
}

void hs_onLeft(void) {
	if(config.hsFreq != 1)
		config.hsFreq /= 2;
	hs_updateDisplay();
	hs_restart();
}

void hs_onRight(void) {
	if(config.hsFreq != 8)
		config.hsFreq *= 2;
	hs_updateDisplay();
	hs_restart();
}

void hs_onStart(void) {
	if(running) {
		running = false;
	}
	else if(isHsOutputEnabled()) {
		saveSettings();
		running = true;
		menuEntry.updateDisplay();

		hs_restart();
		while(running) {
			processButton();
		}

		timer1Stop();
		HSPORT &= ~(1 << HS);   // set HS pin to LOW
		menuEntry.updateDisplay();
	}
}

void pwm_displayDuty(void) {
	LCDGotoXY(10, 0);
	printf("%5.1f%%", ((double)config.pwmDuty+1) / 256 * 100);
}

void pwm_updateDisplay(void) {
	signal_updateDisplay();
	pwm_displayDuty();
}

void pwn_prepareBuffer(void) {
	for(uint8_t i = 0; ; ++i) {
		signalBuffer[i] = (i <= config.pwmDuty) ? 255 : 0;
		if(i == 255) break;
	}
}

void pwm_run(void) {
	while(running) {
		pwn_prepareBuffer();
		signal_continue(true);
	}
}

void pwm_onStart(void) {
	if(!running) {
		signal_start();
		pwm_run();
		signal_stop();
	}
	else {
		running = false;
	}
}

void pwm_onUp(void) {
	if(!running) {
		menu_onUp();
	}
	else {
		if(config.pwmDuty < 255) ++config.pwmDuty;
		pwm_updateDisplay();
	}
}

void pwm_onDown(void) {
	if(!running) {
		menu_onDown();
	}
	else {
		if(config.pwmDuty > 0) --config.pwmDuty;
		pwm_updateDisplay();
	}
}

void pwmHs_updateDisplay(void) {
	double freq;
	switch(config.pwmFreq) {
		case 61:    freq = 61.04;     break;
		case 244:   freq = 244.14;    break;
		case 976:   freq = 976.56;    break;
		case 7813:  freq = 7812.50;   break;
		default:    freq = 62500.00;  break;
	}

	pwm_displayDuty();
	LCDGotoXY(0, 1);
	printf("%8.2fHz", freq);
	displayHsOutputStatus();
}

void pwmHs_onStart(void) {
	if(running) {
		running = false;
	}
	else if(isHsOutputEnabled()) {
		saveSettings();
		running = true;
		menuEntry.updateDisplay();

		while(running) {
			OCR1A = config.pwmDuty;
			timer1StartPwm(config.pwmFreq);
			processButton();
		}

		timer1Stop();
		HSPORT &= ~(1 << HS);   // set HS pin to LOW
		menuEntry.updateDisplay();
	}
}

void pwmHs_onUp(void) {
	if(!running) {
		menu_onUp();
	}
	else {
		if(config.pwmDuty < 255) ++config.pwmDuty;
		OCR1A = config.pwmDuty;
		pwmHs_updateDisplay();
	}
}

void pwmHs_onDown(void) {
	if(!running) {
		menu_onDown();
	}
	else {
		if(config.pwmDuty > 0) --config.pwmDuty;
		OCR1A = config.pwmDuty;
		pwmHs_updateDisplay();
	}
}

void pwmHs_onLeft(void) {
	switch(config.pwmFreq) {
		case 61:    ;                       break;
		case 244:   config.pwmFreq = 61;    break;
		case 976:   config.pwmFreq = 244;   break;
		case 7813:  config.pwmFreq = 976;   break;
		case 62500: config.pwmFreq = 7813;  break;
	}
	pwmHs_updateDisplay();
}

void pwmHs_onRight(void) {
	switch(config.pwmFreq) {
		case 61:    config.pwmFreq = 244;   break;
		case 244:   config.pwmFreq = 976;   break;
		case 976:   config.pwmFreq = 7813;  break;
		case 7813:  config.pwmFreq = 62500; break;
		case 62500: ;                       break;
	}
	pwmHs_updateDisplay();
}

void sweep_updateDisplay(void) {
	switch(submenuLevel) {
		case 0:
			CopyStringtoLCD(SWEEP_TITLE, 0, 0);
			LCDGotoXY(0, 1);
			printf("%10.3fHz", config.freq);
			break;

		case 1:
			CopyStringtoLCD(SWEEP_END_TITLE, 0, 0);
			LCDGotoXY(0, 1);
			printf("%10.3fHz", config.freqEnd);
			break;

		case 2:
			CopyStringtoLCD(SWEEP_INC_TITLE, 0, 0);
			LCDGotoXY(0, 1);
			printf("%10.3fHz", config.freqInc);
			break;

	}
	displaySignalStatus();
}

void sweep_onUp(void) {
	submenuLevel = 0;
	menu_onUp();
}

void sweep_onDown(void) {
	submenuLevel = 0;
	menu_onDown();
}

void sweep_onLeft(void) {
	switch(submenuLevel) {
		case 0:
			config.freq -= config.freqStep;
			if(config.freq < MIN_FREQ)
				config.freq = MIN_FREQ;
			break;

		case 1:
			config.freqEnd -= config.freqStep;
			if(config.freqEnd < config.freq)
				config.freqEnd = config.freq;
			break;

		case 2:
			config.freqInc -= config.freqStep;
			if(config.freqInc < MIN_FREQ_INC)
				config.freqInc = MIN_FREQ_INC;
			break;
	}
	sweep_updateDisplay();
}

void sweep_onRight(void) {
	switch(submenuLevel) {
		case 0:
			config.freq += config.freqStep;
			if(config.freq > MAX_FREQ)
				config.freq = MAX_FREQ;
			break;

		case 1:
			config.freqEnd += config.freqStep;
			if(config.freqEnd > MAX_FREQ)
				config.freq = MAX_FREQ;
			break;

		case 2:
			config.freqInc += config.freqStep;
			if(config.freqInc > MAX_FREQ_INC)
				config.freqInc = MAX_FREQ_INC;
			break;
	}
	sweep_updateDisplay();
}

uint32_t sweepFreqToAcc(double freq) {
	double resolution = (double)CPU_FREQ / SWEEP_OUT_TICKS / ((uint32_t)1 << SWEEP_ACC_FRAC_BITS) / SIGNAL_BUFFER_SIZE;
	return freq/(resolution / config.freqCal);
}

void sweep_continue(void) {
	double freq = config.freq - config.freqInc; // one step back for the first 1/4 of wave
	if(freq < 0.0) freq = 0.0;

	uint32_t acc = sweepFreqToAcc(config.freq);
	if(acc == 0) acc = 1;

	uint32_t inc = sweepFreqToAcc(config.freqInc);
	if(inc == 0) inc = 1;

	uint32_t end = sweepFreqToAcc(config.freqEnd);
	if(end < acc) end = acc;

	uint8_t startIndex = sizeof(signalBuffer) / 2; // here should be the maximum
	while((startIndex < sizeof(signalBuffer)-1) && (signalBuffer[startIndex] > config.offLevel)) ++startIndex;

	SPCR &= ~(1<<CPHA); // clear CPHA bit in SPCR register to allow DDS

	if(config.syncOut == SyncOut_Single || config.syncOut == SyncOut_Multiple) 
		syncPulse();

	if(waitTrigger()) {
		sweepOut(signalBuffer,
			startIndex,
			(uint8_t)(acc >> 16),
			(uint8_t)(acc >> 8),
			(uint8_t)acc,
			(uint8_t)(inc >> 16),
			(uint8_t)(inc >> 8),
			(uint8_t)inc,
			(uint8_t)(end >> 16),
			(uint8_t)(end >> 8),
			(uint8_t)end);
	}
	R2RPORT = config.offLevel;

	// generation is interrupted - check buttons
	enableMenu();
	while(buttonState.pressed != Button_None) {
		processButton();
	}
	disableMenu();
}

void sweep_onStart(void) {
	if(!running) {
		switch(submenuLevel) {
			case 0: { // set end frequency
					submenuLevel = 1;
					sweep_updateDisplay();
				}
				break;

			case 1: { // set step
					submenuLevel = 2;
					sweep_updateDisplay();
				}
				break;

			case 2: { // run
					saveSettings();
					running = true;
					menuEntry.updateDisplay();
					disableMenu();

					memcpy_P(signalBuffer, SINE_WAVE_FROM_ZERO, sizeof(signalBuffer));
					while(running) {
						sweep_continue();
					}

					signal_stop();

					// reset menu
					submenuLevel = 0;
					onNewMenuEntry();
				}
				break;
		}
	}
	else {
		running = false;
	}
}

void offLevel_updateDisplay(void) {
	LCDGotoXY(0, 1);
	printf("%3u", config.offLevel);
}

void offLevel_onLeft(void) {
	if(config.offLevel > 0) --config.offLevel;
	R2RPORT = config.offLevel;
	offLevel_updateDisplay();
}

void offLevel_onRight(void) {
	if(config.offLevel < 255) ++config.offLevel;
	R2RPORT = config.offLevel;
	offLevel_updateDisplay();
}

void syncOut_updateDisplay(void) {
	LCDGotoXY(0, 1);
	switch(config.syncOut) {
		case SyncOut_Off:      printf("Off     "); break; 
		case SyncOut_Single:   printf("Single  "); break;
		case SyncOut_Multiple: printf("Multiple"); break;
		case SyncOut_Trigger:  printf("Trigger "); break;
		case SyncOut_End:                        ; break; 
	}
}

void syncOut_onLeft(void) {
	if(config.syncOut != SyncOut_Off)
		config.syncOut = (enum SyncOut)((uint8_t)config.syncOut - 1);
	syncOut_updateDisplay();
}

void syncOut_onRight(void) {
	config.syncOut = (enum SyncOut)((uint8_t)config.syncOut + 1);
	if(config.syncOut == SyncOut_End) config.syncOut = (enum SyncOut)((uint8_t)SyncOut_End - 1);
	syncOut_updateDisplay();
}

void syncOut_onOpt(void) {
	setHsDirection(); // commit selection
	optMenu_onOpt();
}

void trigger_updateDisplay(void) {
	LCDGotoXY(0, 1);
	if(config.syncOut == SyncOut_Trigger)
		printf("%8.3fms", config.triggerDelay);
	else
		printf("Off       ");
}

void trigger_onLeft(void) {
	config.syncOut = SyncOut_Trigger;
	config.triggerDelay -= config.freqStep / 100;
	if(config.triggerDelay < MIN_PULSE)
		config.triggerDelay = 0.0;
	trigger_updateDisplay();
}

void trigger_onRight(void) {
	config.syncOut = SyncOut_Trigger;
	config.triggerDelay += config.freqStep / 100;
	if(config.triggerDelay > MAX_PULSE)
		config.triggerDelay = MAX_PULSE;
	trigger_updateDisplay();
}

void calFreq_updateDisplay(void) {
	LCDGotoXY(0, 1);
	printf("%8.6f", config.freqCal);
	displaySignalStatus();
}

void calFreq_onStart(void) {
	if(!running) {
		running = true;
		calFreq_updateDisplay();
		disableMenu();

		memcpy_P(signalBuffer, SINE_WAVE_FROM_ZERO, sizeof(signalBuffer));
		while(running) {
			signal_continue(false);
		}

		enableMenu();
		running = false;
		R2RPORT = config.offLevel;
		calFreq_updateDisplay();
		while(buttonState.pressed != Button_None); // wait until button release, otherwise the generation will be started again
	}
	else {
		running = false;
	}
}

void calFreq_onLeft(void) {
	config.freqCal -= STEP_FREQ_CAL;
	if(config.freqCal < MIN_FREQ_CAL)
		config.freqCal = MIN_FREQ_CAL;
        calFreq_updateDisplay();
}

void calFreq_onRight(void) {
	config.freqCal += STEP_FREQ_CAL;
	if(config.freqCal > MAX_FREQ_CAL)
		config.freqCal = MAX_FREQ_CAL;
        calFreq_updateDisplay();
}

//timer overflow interrupt service tourine
//checks all button status and if button is pressed
//value is updated
ISR(TIMER2_OVF_vect)
{
	checkButtons();
}

/*DDS signal generation function
Original idea is taken from
http://www.myplace.nu/avr/minidds/index.htm
small modification is made - added additional command which
checks if CPHA bit is set in SPCR register if yes - exit function
*/
inline void static signalOut(const uint8_t *signal, uint8_t ad3, uint8_t ad2, uint8_t ad1, uint8_t ad0)
{
	asm volatile(
		"eor r17, r17 			; r17<-0"			"\n\t"
		"eor r18, r18 			; r18<-0"			"\n\t"
		"eor r19, r19 			; r19<-0"			"\n\t"
		"1:"								"\n\t"
		"add r17, %[ad0]		; 1 cycle"			"\n\t"
		"adc r18, %[ad1]		; 1 cycle"			"\n\t"
		"adc r19, %[ad2]		; 1 cycle"			"\n\t"	
		"adc %A[sig], %[ad3]		; 1 cycle"			"\n\t"
		"ld __tmp_reg__, Z 		; 2 cycles" 			"\n\t"
		"out %[out], __tmp_reg__	; 1 cycle"			"\n\t"
		"sbis %[cond], 2		; 1 cycle if no skip" 		"\n\t"
		"rjmp 1b			; 2 cycles. Total 10 cycles"	"\n\t"
		:
		: [ad0] "r"(ad0), [ad1] "r"(ad1), [ad2] "r"(ad2), [ad3] "r"(ad3), // phase increment
		  [sig] "z"(signal),                                              // signal source
		  [out] "I"(_SFR_IO_ADDR(R2RPORT)),                               // output port
		  [cond] "I"(_SFR_IO_ADDR(SPCR))                                  // exit condition
		: "r17", "r18", "r19" 
	);
}

inline void static signalWithSyncOut(const uint8_t *signal, uint8_t ad3, uint8_t ad2, uint8_t ad1, uint8_t ad0)
{
	asm volatile(
		"eor r17, r17 			; r17<-0"			"\n\t"
		"eor r18, r18 			; r18<-0"			"\n\t"
		"eor r19, r19 			; r19<-0"			"\n\t"
		"1:"								"\n\t"
		"add r17, %[ad0]		; 1 cycle"			"\n\t"
		"adc r18, %[ad1]		; 1 cycle"			"\n\t"	
		"adc r19, %[ad2]		; 1 cycle"			"\n\t"	
		"adc %A[sig], %[ad3]		; 1 cycle"			"\n\t"
		"ld __tmp_reg__, Z 		; 2 cycles" 			"\n\t"
		"out %[out], __tmp_reg__	; 1 cycle"			"\n\t"

		"sbrc r30, 7			; 5 cycles together"		"\n\t"
		"cbi %[sync], 5			; "				"\n\t"
		"sbrs r30, 7			; "				"\n\t"
		"sbi %[sync], 5			; "				"\n\t"

		"sbis %[cond], 2		; 1 cycle if no skip" 		"\n\t"
		"rjmp 1b			; 2 cycles. Total 15 cycles"	"\n\t"
		:
		: [ad0] "r"(ad0), [ad1] "r"(ad1), [ad2] "r"(ad2), [ad3] "r"(ad3), // phase increment
		  [sig] "z"(signal),                                              // signal source
		  [out] "I"(_SFR_IO_ADDR(R2RPORT)),                               // output port
		  [sync] "I"(_SFR_IO_ADDR(HSPORT)),                               // sync port
		  [cond] "I"(_SFR_IO_ADDR(SPCR))                                  // exit condition
		: "r17", "r18", "r19" 
	);
}

inline void static randomSignalOut(const uint8_t *signal)
{
	asm volatile(
		"ldi r18, 252"							"\n\t"
		"mov r19, r18"							"\n\t"
		"1:"								"\n\t"
		"ld __tmp_reg__, Z 		; 2 c" 				"\n\t"
		"out %[out], __tmp_reg__	; 1 c"				"\n\t"
		"inc r30			; 1 c"				"\n\t"

		"dec r19			; 1 c"				"\n\t" // just to "random" position in buffer every 251 iteration
		"brne 2f			; 1/2 c"			"\n\t"
		"mov r19, r18			; 1 c"				"\n\t"
		"mov r30, __tmp_reg__		; 1 c"				"\n\t"

		"2:"								"\n\t"
		"sbis %[cond], 2		; 1 c"		 		"\n\t"
		"rjmp 1b			; 2 c. Total 10/11 cycles"	"\n\t"
		:
		: [sig] "z"(signal),                              // signal source
		  [out] "I"(_SFR_IO_ADDR(R2RPORT)),               // output port
		  [cond] "I"(_SFR_IO_ADDR(SPCR))                  // exit condition
		: "r18", "r19" 
	);
}

inline void static sweepOut(const uint8_t *signal, uint8_t startIndex,
                            uint8_t a2, uint8_t a1, uint8_t a0,
                            uint8_t i2, uint8_t i1, uint8_t i0,
                            uint8_t e2, uint8_t e1, uint8_t e0)
{
	asm volatile(
		"eor r18, r18 			; "				"\n\t" // r18 = 0
		"eor r19, r19 			; "				"\n\t" // r19 = 0

		"1:"								"\n\t"
		"add r18, %[a0]			; 1 c"				"\n\t"
		"adc r19, %[a1]			; 1 c"				"\n\t"	
		"adc %A[sig], %[a2]		; 1 c"				"\n\t"
		"breq 2f			; 1/2 c" 			"\n\t" // check begin of new period
		"ld __tmp_reg__, Z		; 2 c" 				"\n\t" // load from the buffer
		"out %[out], __tmp_reg__	; 1 c"				"\n\t" // output
		"rjmp 1b			; 2 c. Total 9 cycles"		"\n\t"

		// 5 cycles from iteration begin

		// make the skipped work
		"2:                     	; "				"\n\t"
		"ld __tmp_reg__, Z		; 2 c" 				"\n\t" // load from the buffer
		"out %[out], __tmp_reg__	; 1 c"				"\n\t" // output

		// increment the increment
		"add %[a0], %[i0]		; 1 c"				"\n\t"
		"adc %[a1], %[i1]		; 1 c"				"\n\t"
		"adc %[a2], %[i2]		; 1 c"				"\n\t"

		// end reached? any continue branch takes 10 cycles
		"cp %[e2], %[a2]		; 1 c"				"\n\t"
		"brlo 9f			; 1/2 c"			"\n\t" // a2 > e2 : exit
		"brne 3f			; 1/2 c"			"\n\t" // a2 == e2 : check e1/a1
		"cp %[e1], %[a1]		; 1 c"				"\n\t"
		"brlo 9f			; 1/2 c"			"\n\t" // a1 > e1 : exit
		"brne 4f			; 1/2 c"			"\n\t" // a1 == e1 : check e0/a0
		"cp %[e0], %[a0]		; 1 c"				"\n\t"
		"brlo 9f			; 1/2 c"			"\n\t" // a0 > e0 : exit
		"rjmp 5f			; 2 c"				"\n\t"
		"3:             	        ; "				"\n\t"
		"nop             	        ; "				"\n\t"
		"nop             	        ; "				"\n\t"
		"nop             	        ; "				"\n\t"
		"4:             	        ; "				"\n\t"
		"nop             	        ; "				"\n\t"
		"nop             	        ; "				"\n\t"
		"nop             	        ; "				"\n\t"

		// 21 cycles from iteration begin and 8 cycles will be taken at the end =>
		// add the 5 skipped steps to the acc (1 step to every 6 cycles)
		"add r18, %[a0]			; 1 c"				"\n\t"
		"adc r19, %[a1]			; 1 c"				"\n\t"	
		"adc %A[sig], %[a2]		; 1 c"				"\n\t"
		"add r18, %[a0]			; 1 c"				"\n\t"
		"adc r19, %[a1]			; 1 c"				"\n\t"	
		"adc %A[sig], %[a2]		; 1 c"				"\n\t"
		"add r18, %[a0]			; 1 c"				"\n\t"
		"adc r19, %[a1]			; 1 c"				"\n\t"	
		"adc %A[sig], %[a2]		; 1 c"				"\n\t"
		"add r18, %[a0]			; 1 c"				"\n\t"
		"adc r19, %[a1]			; 1 c"				"\n\t"	
		"adc %A[sig], %[a2]		; 1 c"				"\n\t"
		"add r18, %[a0]			; 1 c"				"\n\t"
		"adc r19, %[a1]			; 1 c"				"\n\t"	
		"adc %A[sig], %[a2]		; 1 c"				"\n\t"
		"nop             	        ; "				"\n\t"

		// avoid frequency inc on next step at begin; loop here until %A[sig] is incremented; each iteration must be 9 cycles
		"5:             	        ; "				"\n\t"
		"brne 6f			; 1/2 c"			"\n\t" // break if %A[sig] != 0
		"add r18, %[a0]			; 1 c"				"\n\t"
		"adc r19, %[a1]			; 1 c"				"\n\t"	
		"adc %A[sig], %[a2]		; 1 c"				"\n\t"
		"nop             	        ; "				"\n\t"
		"nop             	        ; "				"\n\t"
		"nop             	        ; "				"\n\t"
		"rjmp 5b			; 2 c"				"\n\t"

		// output the new value
		"ld __tmp_reg__, Z		; 2 c" 				"\n\t" // load from the buffer
		"out %[out], __tmp_reg__	; 1 c"				"\n\t" // output

		// check exit condition
		"6:             	        ; "				"\n\t"
		"sbis %[cond], 2		; 1 c"		 		"\n\t"
		"rjmp 1b			; 2 c"				"\n\t"

		// exit
		"9:             	        ; "				"\n\t"
		:
		: [a0] "r"(a0), [a1] "r"(a1), [a2] "r"(a2),            // phase increment
		  [sig] "z"(signal + startIndex),                      // signal source
		  [out] "I"(_SFR_IO_ADDR(R2RPORT)),                    // output port
		  [cond] "I"(_SFR_IO_ADDR(SPCR)),                      // exit condition
		  [i0] "r"(i0), [i1] "r"(i1), [i2] "r"(i2),            // increment of the increment
		  [e0] "r"(e0), [e1] "r"(e1), [e2] "r"(e2)             // stop value
		: "r18", "r19"
	);
}

void timer1Start(uint8_t freqMHz)
{
	switch(freqMHz) {
		case 2:  OCR1A = 3; break;
		case 4:  OCR1A = 1; break;
		case 8:  OCR1A = 0; break;
		default: OCR1A = 7; // 1 MHz
	}
	TCCR1A = (1 << COM1A0); // output compare toggles OC1A pin
	TCCR1B = 0b00001001;    // start timer without prescaler
}

void timer1StartPwm(uint16_t freqHz)
{
	uint8_t prescaler;
	switch(freqHz) {
		case 61:   prescaler = 0b101; break;
		case 244:  prescaler = 0b100; break;
		case 976:  prescaler = 0b011; break;
		case 7813: prescaler = 0b010; break;
		default:   prescaler = 0b001; break;
	}
	
	// Fast PWM 8 bit; non-inverting
	TCCR1A = (1 << WGM10) | (1 << COM1A1);
	TCCR1B = (1 << WGM12) | prescaler;
}

void timer1Stop(void)
{
	TCCR1A = 0; // release the OC1A pin
	TCCR1B = 0; // timer off
}

void init(void) {
	//stderr = &lcd_str;
	stdout = &lcd_str;

	// init LCD
	LCDinit();
	LCDclr();
	LCDcursorOFF();

	loadSettings();

	running = false;

	// init DDS output
	R2RPORT = config.offLevel;
	R2RDDR  = 0xFF; // set A port as output

	// set port pins for buttons
	BDDR   &= ~(_BV(START) | _BV(UP) | _BV(DOWN) | _BV(RIGHT) | _BV(LEFT) | _BV(OPT));
	BPORT  |=  (_BV(START) | _BV(UP) | _BV(DOWN) | _BV(RIGHT) | _BV(LEFT) | _BV(OPT));

	// set port pins for the button interupt
	BDDR2  &= ~(_BV(BTN_INT));
	BPORT2  =  (_BV(BTN_INT));

	setHsDirection();
	timer2Init();
	enableMenu();
	onNewMenuEntry();
	sei();
}

void processButton(void) {
	if(!buttonState.processed) {
		enum Button pressed = buttonState.pressed;
		buttonState.processed = true;

		switch(pressed) {
			case Button_None:  break;
			case Button_Up:    buttonHandlers->onUp();    break;
			case Button_Right: buttonHandlers->onRight(); break;
			case Button_Down:  buttonHandlers->onDown();  break;
			case Button_Left:  buttonHandlers->onLeft();  break;
			case Button_Start: buttonHandlers->onStart(); break;
			case Button_Opt:   buttonHandlers->onOpt();   break;
		}
	}
}

int main(void) {	
	init();
	while(1) {
		processButton();
	}

	return 0;
}


// Project sponsor: Peter Clark
// Email: peterclark@me.com
// Creator: Cedar Lake Instruments LLC
// Date: August, 2018
//
// Description:
// Read three thermistors and save to SD card
//
// Arduino I/O
// Thermistors 1-3: A0-A2
// LED 5
// SD card
// 10 - CS

#include <SD.h>
#include <Wire.h>
#include <RTClib.h>
#include <SPI.h>
#include "Thermistor-103J-G1J.h"

volatile bool _triggered = false;
bool _logging = false;
// Define the Real Time Clock
RTC_PCF8523 _rtc;
// SD card 
File _myFile;

//#define DEBUG
#define LINE_LEN 80
// Frequency of logging in milliseconds
#define LOG_INTERVAL 5000

// 1. Uncomment to set time
// 2. Program
// 3. Comment line out
// 4. Program again immediately
//#define FORCE_TIME_SET
//*******************  Pin definitions ************************
// LED
#define LED 3
// Start/stop logging
#define START 2

// SD card pins
#define CS 10
//***************************************************************

// Rapid blink if a hardware error occurs
// If blocking is true, halts here.
void errorBlink(int code, bool blocking = false)
{
	while (blocking)
	{
		for (int i = 0; i < code; i++)
		{
			digitalWrite(LED, LOW);
			delay(100);
			digitalWrite(LED, HIGH);
			delay(200);
		}
		delay(2000);
	}
}

void setup()
{
	pinMode(LED, OUTPUT);
	Serial.begin(9600);
	if (!_rtc.begin()) 
	{
		Serial.println("Couldn't find RTC");
		errorBlink(2);
	}	

#ifdef FORCE_TIME_SET
    Serial.println("Overriding RTC time");
    _rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
#endif
  
	if (!_rtc.initialized()) 
	{
		Serial.println("Initializing RTC\n");
		_rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
	}
	else
	{
		Serial.println("RTC initialized");
	}

	Serial.println("Initializing SD card...");
	if (!SD.begin(CS))
	{
		Serial.println("SD card initialization failed!");
		errorBlink(5);
	}

	Serial.println("Initialization done");
	pinMode(START, INPUT_PULLUP);
	attachInterrupt(digitalPinToInterrupt(START), logIsr, RISING);

	digitalWrite(LED, HIGH);
	_triggered = false;
}

void loop()
{
	// Check for logging start/stop
	if (_triggered)
	{
		_triggered = false;
		if (_logging)
		{
			_logging = false;
			stopLog();
		}
		else
		{
			_logging = true;
			startLog();
		}
	}

	float t1 = readThermistorValue(A0);
	delay(50);
	float t2 = readThermistorValue(A1);
	delay(50);
	float t3 = readThermistorValue(A2);

	// Toggle activity LED
	digitalWrite(LED, LOW);
	delay(100);
	digitalWrite(LED, HIGH);
	delay(200);

	logData(t1 * 100, t2 * 100, t3 * 100);
	// Account for main LED toggle time
	delay(LOG_INTERVAL - 200);
}

// Reads the thermistor on channel
// Returns temperature in degrees C
double readThermistorValue(int channel)
{
	double r = readResistorOhms(channel);
	
	// Index 0 = -80C, index 379 = 300C
	const int TEMP_OFFSET = -80;
	const int END = 380;

	float temp = -9999.0;
	for (int i = START + 1; i < END; i++)
	{
		float currentTemp = pgm_read_float(&(TempToOhms[i]));
		if ( currentTemp < r)
		{
			float lastTemp = pgm_read_float(&TempToOhms[i-1]);
			temp = map(r, lastTemp, currentTemp, (float)i-1.0, (float)i) + TEMP_OFFSET;
			break;
		}
	}
	return temp;
}

// Reads a resistor on channel.
// Returns ohms.
double readResistorOhms(int channel)
{
	const double VIN = 5.0;
	const double R2 = 10000.0;
	int reading = analogRead(channel);

	double Vtherm = reading / 1024.0 * VIN;
	// Rx = R2/(Vin/Vo - 1)
	double resistance = R2 / (VIN / Vtherm - 1);

#ifdef DEBUG
	Serial.print("Resistance: "); Serial.println(resistance);
#endif
	return resistance;
}

// Rewrite of Arduino map() function for floats
float map(float x, float in_min, float in_max, float out_min, float out_max)
{
	float result = (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
	return result;
}

// Set the time
void setClock(String t)
{
	char char_t[15];
	memset(char_t, 0, 15);
	t.toCharArray(char_t, 15);
	int d, h, m, s, y = 2018, mo = 7, day=1;
	if (sscanf(char_t, "%d,%d:%d:%d", &d, &h, &m, &s) != 4)
	{
		Serial.print("Could not scan input: ");
		Serial.println(t);
		return;
	}

	DateTime now(y, mo, day, h, m, s);

	_rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
	Serial.print("Time set to ");
	Serial.println(char_t);
}

// Log the three temperature values
void logData(int v1, int v2, int v3)
{
	DateTime timeNow = _rtc.now();
	char buffer[LINE_LEN];
    snprintf(buffer, LINE_LEN, "%02d:%02d:%02d,%d.%d,%d.%d,%d.%d", timeNow.hour(), timeNow.minute(), timeNow.second(), v1/100,abs(v1%100),v2/100,abs(v2%100),v3/100,abs(v3%100));
	if (_logging)
	{
		// Write to file
		_myFile.println(buffer);
		// Toggle activity LED
		digitalWrite(LED, LOW);
		delay(100);
		digitalWrite(LED, HIGH);
	}
	Serial.println(buffer);
}

// Create log file
// Filename follows the format of ddTHHMM.csv
// where dd is the day of month and HH,MM are hour and minute in 24 hour format
void startLog()
{
	// open the file. note that only one file can be open at a time,
	// so you have to close this one before opening another.
	DateTime now = _rtc.now();
	char buffer[LINE_LEN];
	snprintf(buffer, LINE_LEN, "%02dT%02d%02d.csv", now.day(), now.hour(), now.minute() );
	Serial.print("Recording started: "); Serial.println(buffer);
	_myFile = SD.open(buffer, FILE_WRITE);
}

// Close the log file
void stopLog()
{
	_myFile.close();
	Serial.println("Recording stopped");
}

// ISR for the START interrupt
void logIsr()
{
	_triggered = true;

	// Feedback for pressing LED
	if (!_logging)
	{
		digitalWrite(LED, LOW);
		delay(100);
	}
}

/**The MIT License (MIT)
Copyright (c) 2015 by Daniel Eichhorn
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
See more at http://blog.squix.ch

Adapted by Bodmer to use the faster TFT_ILI9341_ESP library:
https://github.com/Bodmer/TFT_ILI9341_ESP

*/

#ifndef SETTINGS_H
#define SETTINGS_H

#include <simpleDSTadjust.h>

// Setup
const int UPDATE_INTERVAL_SECS = 10 * 60; // Update every 10 minutes

// Pins for the TFT interface are defined in the User_Setup.h file inside the TFT_eSPI library
// These are the ones I used on a NodeMCU plus MOSI and SCK:
// #define TFT_DC D3
// #define TFT_CS D8
///#define TFT_RST -1 // Minus one means no pin allocated, connect to NodeMCU RST pin

// -----------------------------------
// Example Locales (uncomment only 1)
#define Rome
//#define Boston
//#define Sydney
//------------------------------------

#ifdef Rome
const float UTC_OFFSET = +1;
struct dstRule StartRule = {"CEST", Last, Sun, Mar, 2, 3600}; // Central European Summer Time = UTC/GMT +2 hours
struct dstRule EndRule = {"CET", Last, Sun, Oct, 2, 0};       // Central European Time = UTC/GMT +1 hour

// Uncomment for 24 Hour style clock
#define STYLE_24HR  //FIXME

#define NTP_SERVERS "0.it.pool.ntp.org", "1.it.pool.ntp.org", "2.it.pool.ntp.org"

// Wunderground Settings, EDIT TO SUIT YOUR LOCATION
const boolean IS_METRIC = true; // Temperature only? Wind speed units appear to stay in mph. To do: investigate <<<<<<<<<<<<<<<<<<<<<<<<<

const String WUNDERGRROUND_API_KEY = "<WUNDERGROUND KEY HERE>";

// For language codes see https://www.wunderground.com/weather/api/d/docs?d=language-support&_ga=1.55148395.1951311424.1484425551
const String WUNDERGRROUND_LANGUAGE = "EN"; // Language EN = English

// For a list of countries, states and cities see https://www.wunderground.com/about/faq/international_cities.asp
const String WUNDERGROUND_COUNTRY = "IT"; // UK, US etc
const String WUNDERGROUND_CITY = "Venice"; // City, "London", "FL/Boca_Raton" for Boca Raton in Florida (State/City) etc. Use underscore_for spaces)
#endif

// Windspeed conversion, use 1 pair of #defines. To do: investigate a more convenient method <<<<<<<<<<<<<<<<<<<<<
//#define WIND_SPEED_SCALING 1.0      // mph
//#define WIND_SPEED_UNITS " mph"

//#define WIND_SPEED_SCALING 0.868976 // mph to knots
//#define WIND_SPEED_UNITS " kn"

#define WIND_SPEED_SCALING 1.60934  // mph to kph
#define WIND_SPEED_UNITS " kph"


//Thingspeak Settings - not used, no need to populate this at the moment
const String THINGSPEAK_CHANNEL_ID = "<CHANNEL_ID_HERE>";
const String THINGSPEAK_API_READ_KEY = "<API_READ_KEY_HERE>";


// List, so that the downloader knows what to fetch
String wundergroundIcons [] = {"chanceflurries","chancerain","chancesleet","chancesnow","clear","cloudy","flurries","fog","hazy","mostlycloudy","mostlysunny","partlycloudy","partlysunny","rain","sleet","snow","sunny","tstorms","unknown"};

// Setup simpleDSTadjust Library rules
simpleDSTadjust dstAdjusted(StartRule, EndRule);

#endif

/***************************
 * End Settings
 **************************/

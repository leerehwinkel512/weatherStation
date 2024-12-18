#include <GxEPD2_BW.h>
#include <Fonts/FreeSerif24pt7b.h>
#include <Fonts/FreeMono24pt7b.h>
#include <Fonts/FreeMono18pt7b.h>
#include <Fonts/FreeMono12pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <TimeLib.h>

WiFiManager wm;

unsigned long lastUpdateTime = 0;
#define UPDATE_INTERVAL (15 * 60 * 1000) // update every 15min

// Time Config
struct tm timeinfo;

// Pensacola coordinates and NOAA station
const char* STATION_ID = "8729840";


struct WeatherStatus {
  float temperature;     // Temperature in Fahrenheit
  float windSpeed;       // Wind speed
  float windDirection;   // Wind direction in degrees
  float barometer;       // Barometric pressure
};

struct TideStatus {
  time_t HighTime;     // Unix timestamp of next high tide
  time_t LowTime;      // Unix timestamp of next low tide
  time_t NextTime;     // Unix timestamp of next tide
  time_t PrevTime;     // Unix timestamp of next tide  
  float factor;        // 0.0 to 1.0 indicating progress between tides
  char direction[10];  // "FLOOD" or "EBB"
};

TideStatus getTides() {

  Serial.println("=====================================");
  Serial.println("Get tide data from NOAA API");
  Serial.println();  

  // init return struct
  TideStatus result = {
    0,      // High timestamp
    0,      // Low timestamp
    0,      // Next timestamp
    0,      // Prev timestamp
    0.0,    // factor
    "EBB",  // direction (default to EBB)
  };
  
  if (WiFi.status() != WL_CONNECTED) {
    return result;
  }

  HTTPClient http;
  
  // Get current time and format dates for API request
  time_t currentTime;
  time(&currentTime);

  char currentTimeStr[20];
  strftime(currentTimeStr, sizeof(currentTimeStr), "%Y-%m-%d %H:%M", localtime(&currentTime));
  Serial.print("Current time: ");
  Serial.println(currentTimeStr);

  char yesterdayStr[20];
  time_t yesterday = currentTime - (24 * 60 * 60);
  strftime(yesterdayStr, sizeof(yesterdayStr), "%Y%m%d", localtime(&yesterday));
  Serial.print("Yesterday date: ");
  Serial.println(yesterdayStr);

  char tomorrowStr[20];
  time_t tomorrow = currentTime + (24 * 60 * 60);
  strftime(tomorrowStr, sizeof(tomorrowStr), "%Y%m%d", localtime(&tomorrow));
  Serial.print("Tomorrow date: ");
  Serial.println(tomorrowStr);

  // call API
  String url = "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?";
  url += "begin_date=";
  url += yesterdayStr;
  url += "&end_date=";
  url += tomorrowStr;
  url += "&station=";
  url += STATION_ID;
  url += "&product=predictions";
  url += "&datum=MLLW";
  url += "&interval=hilo";
  url += "&units=english";
  url += "&time_zone=lst_ldt";
  url += "&format=json";
  
  Serial.println("\nAPI Request URL:");
  Serial.println(url);
  
  Serial.println("\nStarting HTTP GET request...");
  http.begin(url);
  int httpCode = http.GET();
  
  Serial.print("HTTP Response Code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String payload = http.getString();

    Serial.println("API Response:");
    Serial.println(payload);

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      JsonArray predictions = doc["predictions"];
      if (predictions) {

        time_t prevTideTime = 0;
                
        for (JsonObject prediction : predictions) {
          const char* timeStr = prediction["t"];
          const char* type = prediction["type"];
          bool isHigh = (type[0] == 'H');
          
          // Parse the time string
          int year, month, day, hour, minute;
          if (sscanf(timeStr, "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute) == 5) {

            // Create the time struct
            struct tm tm = {0};
            tm.tm_year = year - 1900;
            tm.tm_mon = month - 1;  // tm months are 0-11
            tm.tm_mday = day;
            tm.tm_hour = hour;
            tm.tm_min = minute;
            
            // Convert to time_t
            time_t predictionTime = mktime(&tm);

            char predictionTimeStr[20];
            strftime(predictionTimeStr, sizeof(predictionTimeStr), "%Y-%m-%d %H:%M", gmtime(&predictionTime));
            Serial.print("Tide Time: ");
            Serial.print(predictionTimeStr);
            Serial.print("  Type: ");
            Serial.println(type);

            // check to see if tide time is after current time and not yet set
            if (predictionTime > currentTime && result.NextTime == 0) {
              if (isHigh) {
                result.HighTime  = predictionTime;
                result.LowTime   = prevTideTime;
                result.NextTime  = predictionTime;
                result.PrevTime  = prevTideTime;
                strcpy(result.direction, "FLOOD");
              } else {
                result.LowTime   = predictionTime;
                result.HighTime  = prevTideTime;
                result.NextTime  = predictionTime;
                result.PrevTime  = prevTideTime;
                strcpy(result.direction, "EBB");
              }
            } else {
              prevTideTime = predictionTime;
            }
          }
        }

        float totalDuration = (float)(result.NextTime - result.PrevTime);
        float timeElapsed   = (float)(currentTime - result.PrevTime);
        result.factor = timeElapsed / totalDuration;

        // check
        char nextTimeStr[20];
        strftime(nextTimeStr, sizeof(nextTimeStr), "%Y-%m-%d %H:%M", localtime(&result.NextTime));

        char prevTimeStr[20];
        strftime(prevTimeStr, sizeof(prevTimeStr), "%Y-%m-%d %H:%M", localtime(&result.PrevTime));        

        Serial.println();
        Serial.print("Previous Tide Time: ");
        Serial.println(prevTimeStr);
        Serial.print("Current Time:       ");
        Serial.println(currentTimeStr);        
        Serial.print("Next Tide Time:     ");
        Serial.println(nextTimeStr);
        Serial.print("Factor: ");
        Serial.println(result.factor);
        Serial.print("Direction: ");
        Serial.println(result.direction);
      }
    }
  }

  http.end();

  Serial.println();
  Serial.println("Done getting tide data");
  Serial.println("=====================================");

  return result;
}


WeatherStatus getWeather() {

  Serial.println("=====================================");
  Serial.println("Get weater data from NOAA API");
  Serial.println("");  

  // Initialize return struct
  WeatherStatus result = {
    0.0,    // temperature
    0.0,    // windSpeed
    0.0,    // windDirection
    0.0,    // barometer
  };
  
  if (WiFi.status() != WL_CONNECTED) {
    return result;
  }

  HTTPClient http;
  
  // Get temperature
  String tempUrl = "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?";
  tempUrl += "date=latest";
  tempUrl += "&station=";
  tempUrl += STATION_ID;
  tempUrl += "&product=air_temperature";
  tempUrl += "&units=english";
  tempUrl += "&time_zone=lst_ldt";
  tempUrl += "&format=json";
  
  Serial.println("Temperature API Request URL:");
  Serial.println(tempUrl);
  
  http.begin(tempUrl);
  int httpCode = http.GET();
  
  Serial.print("HTTP Response Code: ");
  Serial.println(httpCode);
  
  if (httpCode > 0) {
    String payload = http.getString();
    
    Serial.println("Temperature Response:");
    Serial.println(payload);
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      JsonArray data = doc["data"];
      if (data && data.size() > 0) {
        const char* value = data[0]["v"];
        result.temperature = atof(value);
      }
    }
  }
  http.end();

  // Get wind data
  delay(1000); // Small delay between requests
  
  String windUrl = "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?";
  windUrl += "date=latest";
  windUrl += "&station=";
  windUrl += STATION_ID;
  windUrl += "&product=wind";
  windUrl += "&units=english";
  windUrl += "&time_zone=lst_ldt";
  windUrl += "&format=json";
  
  Serial.println("Wind API Request URL:");
  Serial.println(windUrl);
  
  http.begin(windUrl);
  httpCode = http.GET();
  
  Serial.print("HTTP Response Code: ");
  Serial.println(httpCode);
  
  if (httpCode > 0) {
    String payload = http.getString();
    
    Serial.println("Wind Response:");
    Serial.println(payload);
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      JsonArray data = doc["data"];
      if (data && data.size() > 0) {
        const char* speed = data[0]["s"];
        const char* dir = data[0]["d"];
        result.windSpeed = atof(speed);
        result.windDirection = atoi(dir);
      }
    }
  }
  http.end();

  // Get air pressure
  delay(1000); // Small delay between requests
  
  String pressureUrl = "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?";
  pressureUrl += "date=latest";
  pressureUrl += "&station=";
  pressureUrl += STATION_ID;
  pressureUrl += "&product=air_pressure";
  pressureUrl += "&units=english";
  pressureUrl += "&time_zone=lst_ldt";
  pressureUrl += "&format=json";
  
  Serial.println("Pressure API Request URL:");
  Serial.println(pressureUrl);
  
  http.begin(pressureUrl);
  httpCode = http.GET();

  Serial.print("HTTP Response Code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String payload = http.getString();
    
    Serial.println("Pressure Response:");
    Serial.println(payload);
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      JsonArray data = doc["data"];
      if (data && data.size() > 0) {
        const char* value = data[0]["v"];
        result.barometer = atof(value);
      }
    }
  }
  http.end();

  // Print final weather report
  Serial.print("Temperature: ");
  Serial.print(result.temperature);
  Serial.println("°F");
  Serial.print("Wind Speed: ");
  Serial.print(result.windSpeed);
  Serial.println(" knots");
  Serial.print("Wind Direction: ");
  Serial.print(result.windDirection);
  Serial.println("°");
  Serial.print("Barometric Pressure: ");
  Serial.print(result.barometer);
  Serial.println(" mb");
  Serial.println();  

  Serial.println("Done getting weater data");
  Serial.println("=====================================");  
  
  return result;
}

// Initialize the display
GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(GxEPD2_420_GDEY042T81(/*CS=*/10, /*DC=*/8, /*RST=*/9, /*BUSY=*/7));

// Constants for layout
const int DISPLAY_WIDTH = 400;
const int DISPLAY_HEIGHT = 300;
const int MARGIN = 10;
const int CIRCLE_RADIUS = 60;
const int INNER_RADIUS = 30;

void renderTitle() {
  display.setFont(&FreeSerif24pt7b);
  display.setCursor(MARGIN, 40);
  display.print("Pensacola, FL");

  // add time
  time_t now;
  time(&now);

  // Format current time
  char lastUpdate[20];
  strftime(lastUpdate, sizeof(lastUpdate), "%Y-%m-%d %I:%M %p", localtime(&now));

  // Remove leading zero from the hour position (which is at index 11 in this format)
  if (lastUpdate[11] == '0') {
      for(int i = 11; lastUpdate[i]; i++) {
          lastUpdate[i] = lastUpdate[i + 1];
      }
  }  

  display.setFont(&FreeMono9pt7b);
  display.setCursor(MARGIN, 60);
  display.print(lastUpdate);
  display.setFont();
}

void renderTemp(float temperature) {
  display.setFont(&FreeMono24pt7b);
  display.setCursor(MARGIN + 40, 125);
  display.print(int(temperature));
  display.setFont();

  // Get current cursor position after printing
  int x = display.getCursorX();
  int y = display.getCursorY();
  
  // Draw small circle for degrees
  display.drawCircle(x + 5, y - 17, 3, GxEPD_BLACK);
  
  // Move cursor print F
  display.setFont(&FreeMono9pt7b);
  display.setCursor(x + 15, y - 10);
  display.print("F"); 
  display.setFont();  
}

void renderTideBar(time_t NextTime, char direction[10], float factor) {

  // Format times from timestamps
  char nextTideTime[10];
  strftime(nextTideTime, sizeof(nextTideTime), "%I:%M %p", localtime(&NextTime));

  // If hour starts with 0, replace it with a space
  if (nextTideTime[0] == '0') {
      nextTideTime[0] = ' ';
  }
  
  // Title with direction
  display.setFont(&FreeMono12pt7b);
  display.setCursor(MARGIN, 185);
  display.print("Tide: ");
  display.print(direction);
  display.setFont();
  
  // Bar
  int barWidth = DISPLAY_WIDTH - (2 * MARGIN);
  int barHeight = 20;
  display.drawRect(MARGIN, 190, barWidth, barHeight, GxEPD_BLACK);
  
  // Time
  display.setFont(&FreeMono9pt7b);
  display.setCursor(MARGIN, 225);
  display.setCursor(DISPLAY_WIDTH - 100, 225);
  display.print(nextTideTime);
  display.setFont();
  
  // fill to show progress
  if (factor > 0) {
    display.fillRect(MARGIN, 190, (barWidth * factor), barHeight, GxEPD_BLACK);
  }
}

void renderPressure(float barometer) {

  // Title with reading
  display.setFont(&FreeMono12pt7b);
  display.setCursor(MARGIN, 245);
  display.print("Baro: ");
  display.print(int(barometer));
  display.setFont(&FreeMono9pt7b);
  display.print("mb");  
  display.setFont();
  
  // Bar
  int barWidth = DISPLAY_WIDTH - (2 * MARGIN);
  int barHeight = 20;
  display.drawRect(MARGIN, 250, barWidth, barHeight, GxEPD_BLACK);
  
  // Labels
  display.setFont(&FreeMono9pt7b);
  display.setCursor(MARGIN, 285);
  display.print("Rain");
  display.setCursor(DISPLAY_WIDTH - 50, 285);
  display.print("Fair");
  display.setFont();

  // Calculate position factor (0.0 to 1.0) based on pressure range
  float factor = (barometer - 990) / (1030 - 990);
  // Constrain between 0 and 1
  factor = factor < 0 ? 0 : (factor > 1 ? 1 : factor);

  // Fill bar up to current pressure
  int markerPosition = MARGIN + (barWidth * factor);
  display.fillRect(MARGIN, 250, markerPosition - MARGIN, barHeight, GxEPD_BLACK);
}

void renderWind(float windSpeed, float windDirection) {
  
  // Circle
  int centerX = DISPLAY_WIDTH - 80;
  int centerY = 105;
  display.drawCircle(centerX, centerY, CIRCLE_RADIUS, GxEPD_BLACK);
  
  // Direction markers
  display.setFont(&FreeMono12pt7b);  
  display.setCursor(centerX - 5, centerY - CIRCLE_RADIUS - 5);
  display.print("N");
  display.setCursor(centerX - 5, centerY + CIRCLE_RADIUS + 16);
  display.print("S");
  display.setCursor(centerX + CIRCLE_RADIUS + 2, centerY + 6);
  display.print("E");
  display.setCursor(centerX - CIRCLE_RADIUS - 16, centerY + 6);
  display.print("W");
  display.setFont();   
  
  // Wind speed
  display.setFont(&FreeMono24pt7b);  
  display.setCursor(centerX - 20, centerY + 8);
  display.print(int(windSpeed));
  display.setFont(&FreeMono12pt7b);
  display.print("kt");  
  display.setFont();
  
  // Wind direction indicator
  // Subtract 90 to align with meteorological convention (0° = North)
  float radians = ((windDirection - 90) * PI) / 180.0;

  // Calculate start point offset from center by INNER_RADIUS
  int startX = centerX + (INNER_RADIUS * cos(radians));
  int startY = centerY + (INNER_RADIUS * sin(radians));

  // Calculate end point
  int endX = centerX + (CIRCLE_RADIUS * cos(radians));
  int endY = centerY + (CIRCLE_RADIUS * sin(radians));

  // Draw line from offset start point to edge
  display.drawLine(startX, startY, endX, endY, GxEPD_BLACK);
  
  // Calculate triangle points
  int triangleHeight = 15; // Length of triangle
  int triangleBase = 15;   // Width of triangle base
  float backDist = triangleHeight;  // How far back from the tip to place base points

  // Calculate the back-center point of the triangle
  int backCenterX = endX - backDist * cos(radians);
  int backCenterY = endY - backDist * sin(radians);

  // Calculate the base points
  int base1X = backCenterX + (triangleBase/2) * cos(radians + PI/2);
  int base1Y = backCenterY + (triangleBase/2) * sin(radians + PI/2);
  int base2X = backCenterX + (triangleBase/2) * cos(radians - PI/2);
  int base2Y = backCenterY + (triangleBase/2) * sin(radians - PI/2);

  // Draw filled triangle
  display.fillTriangle(endX, endY, base1X, base1Y, base2X, base2Y, GxEPD_BLACK);
}


void setupTime() {
  // CST is UTC-6, so we use a negative offset of 6 * 3600 seconds
  const long gmtOffset_sec = -6 * 3600;
  const int daylightOffset_sec = 3600; // 1 hour for daylight savings
  
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org");
  
  // Wait for NTP sync
  Serial.println("Waiting for NTP time sync...");
  time_t now = time(nullptr);
  while (now < 24 * 3600) {
    Serial.print(".");
    delay(100);
    now = time(nullptr);
  }
  Serial.println();
  
  struct tm timeinfo;
  if(getLocalTime(&timeinfo)){
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.print("NTP Time Synchronized: ");
    Serial.println(timeStringBuff);
  }
}


void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=====================================");
  Serial.println("Begin Marine Weather Station");

  // Init display with power management
  Serial.println("Init Display");
  display.init();
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();
  display.setFont(&FreeMono9pt7b);

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);  
    display.setCursor(0, 50);
    display.println("Connect to marineWeather wifi.");
  } while (display.nextPage());
  display.powerOff();
  
  // wifi connect; edit default screen to hide elements
  Serial.println("Init Wifi");    
  std::vector<const char *> wm_menu  = {"wifi", "exit"};
  wm.setMenu(wm_menu);
  wm.setDebugOutput(false);
  wm.setDebugOutput(false);
  //wm.resetSettings();
  wm.setConfigPortalTimeout(180);
  
  if(!wm.autoConnect("marineWeather")) {
    display.firstPage();
    do {
      display.println("Failed to connect and hit timeout");
    } while (display.nextPage());
    display.powerOff();

    delay(2000);
    ESP.restart();
  }

  // wifi status update
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(0, 50);
    display.println("Connected to WiFi!");
    display.print("IP Address: ");
    display.print(WiFi.localIP());
  } while (display.nextPage());
  display.powerOff(); 

  // init time
  Serial.println("Init Time");
  setupTime();

  Serial.println("Setup Complete");
  Serial.println("=====================================");
}

void loop() {
  unsigned long currentMillis = millis();

  WeatherStatus currentWeather;
  TideStatus currentTide;
  
  // Check for update
  if ((currentMillis - lastUpdateTime >= UPDATE_INTERVAL) || (lastUpdateTime == 0)) {
    lastUpdateTime = currentMillis;

    currentTide    = getTides();
    currentWeather = getWeather();
    
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      renderTitle();
      renderTemp(currentWeather.temperature);
      renderTideBar(currentTide.NextTime, currentTide.direction, currentTide.factor);
      renderPressure(currentWeather.barometer);
      renderWind(currentWeather.windSpeed, currentWeather.windDirection);
    } while (display.nextPage());
    display.powerOff();
    
  }
}
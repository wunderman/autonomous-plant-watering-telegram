#include <M5Core2.h> //basic M5 library
#include "M5_ENV.h" //library for ENV III
#include <WiFi.h> //for WiFi connectivity needed for Telegram
#include <WiFiClientSecure.h> //for connecting to nyu family
#include <UniversalTelegramBot.h> //Telegram Bot library
#include <string> // Library for Strings
#include <VL53L0X.h> // Library for ToF Sensor
#include "time.h" //

// Declaration for enabling Telegram to scan for messages
const unsigned long BOT_MTBS = 1000; // mean time between scan messages 
unsigned long bot_lasttime; // last time messages' scan has been done
const String chat_id = "286032787"; // chat id to identify the chat
#define BOT_TOKEN "6248608382:AAEb6pb4DTt00RCe0sc7Xwn8lxRntYIilYQ" //Bot Token to identify bot

//Telegram + WiFi security paramteres initialization
WiFiClientSecure secured_client; //WiFi declaration
UniversalTelegramBot bot(BOT_TOKEN, secured_client); //Bot declaration

// Setting up enterprise WiFi, provided by Hasan on Brightspace
#include "esp_wpa2.h" //wpa2 library for connections to Enterprise networks byte mac[6];
#define EAP_ANONYMOUS_IDENTITY "-" //Enter your NYU Net ID
#define EAP_IDENTITY "-" //Enter your NYU Net ID
#define EAP_PASSWORD "-" //enter the password for NYU Net ID account here in the quotations
const char* WIFI_SSID = "nyu"; // Wifi SSID -- Should be set to "nyu" | Alternatively, there's a way to coonect to mobile hotspot too 

//TRIAL FOR RTC
const char* ntpServer = "pool.ntp.org";  //Set the connect NTP server. 
const long gmtOffset_sec = 14400;
const int daylightOffset_sec = 0;

//Defining pins for our sensors
#define INPUT_PIN 36 //declaring Pins for Watering Unit
#define PUMP_PIN  26

//Sensors
SHT3X sht30; //declaring a temperature and humidity sensor in ENV III
QMP6988 qmp6988; //declaring pressure sensor in ENV III
RTC_TimeTypeDef RTCtime; //declaring RTC time type
RTC_DateTypeDef RTCDate; //declaring RTC date type
VL53L0X TOF_sensor; //ToF Sensor declaration

//Function prototypes
void get_sensorDATA(double sensorDATA[]); //gets data from sensors and store into an array
int calculate_SM(int maxADC, int minADC, int rawADC); //calculated soil moisture in % from ADC
bool checkTime(int *schedule, int size); //checks if current time is 
void MakeSchedule(int waterschedstart, int schedincrement, int schedloop, int *schedule);
void printInfo(double sensorDATA[], int soil_moisture);
void handleNewMessages(int numNewMessages);
void setupTime();

const int maxADC = 3000, minADC = 1900; //NEED TO TEST SENSOR

#ifndef WateringDetails_h
#define WateringDetials_h

//Class for watering details
class WateringDetails{
  private:
    int min_moist_percent; 
    int waterschedloop, waterschedincrement, waterschedstart;
    int watering_amount;
    int minLevel;
    int tankHeight;

  public:
    WateringDetails(int tankHeight = 200, int min_moist_percent = 40, int waterschedloop = 1, int waterschedincrement = 0, int waterschedstart = 800, int watering_amount = 5, int minLevel=30){
      this->min_moist_percent = min_moist_percent; 
      this->waterschedloop = waterschedloop;
      this->waterschedincrement = waterschedincrement;
      this->waterschedstart = waterschedstart;
      this->watering_amount = watering_amount;
      this->minLevel=minLevel;
      this->tankHeight=tankHeight;
    }

  int getMinMoist() {return min_moist_percent;} 
  int getSchedLoop() {return waterschedloop;}
  int getSchedInc() {return waterschedincrement;}
  int getSchedStart() {return waterschedstart;}
  int getWatAmt() {return watering_amount;}
  int getminLevel() {return minLevel;}
  int getTankHeight() {return tankHeight;}

  void setMinMoist(int min_moist_percent) {this->min_moist_percent = min_moist_percent;}
  void setSchedLoop(int waterschedloop) {this->waterschedloop = waterschedloop;}
  void setSchedInc(int waterschedincrement) {this->waterschedincrement = waterschedincrement;}
  void setSchedStart(int waterschedstart) {this->waterschedstart = waterschedstart;}
  void setWatAmt(int wateringamount) {this->watering_amount = wateringamount;}
  void setMinLevel(int minLevel) {this->minLevel=minLevel;}
  void setTankHeight(int tankHeight) {this->tankHeight=tankHeight;}

};


#endif

void setup() 
  {
    M5.begin(); // Turning on M5Core2
    WiFi.begin(WIFI_SSID, WPA2_AUTH_PEAP, EAP_ANONYMOUS_IDENTITY, EAP_IDENTITY, EAP_PASSWORD);

  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }
  Serial.print("\nWiFi connected. IP address: ");
  Serial.println(WiFi.localIP());

  TOF_sensor.init();
  TOF_sensor.setTimeout(1000);
  TOF_sensor.startContinuous();

// Important
  Serial.print("Retrieving time: ");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);// get UTC time via NTP
  time_t now = time(nullptr);
  while (now < 24 * 3600)
  {
    Serial.print(".");
    delay(100);
    now = time(nullptr);
  }
  Serial.println(now);
  
    Wire.begin();  // Wire init, adding the I2C bus
    qmp6988.init();
    M5.Lcd.print("Watering Plant System");

    // Printing out the network info
    Serial.print("\nWiFi connected. IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Connecting to Wifi SSID ");
    Serial.print(WIFI_SSID);

    pinMode(INPUT_PIN, INPUT);
    pinMode(PUMP_PIN, OUTPUT);
    pinMode(25, OUTPUT);
    digitalWrite(25, 0);


//TRIAL FOR RTC
    setupTime();

    String welcomeMessage ="Use the following commands to control your outputs.\n\n";
    welcomeMessage += "/getData to get sensor readings\n";
    welcomeMessage += "/changeDetails to change watering details\n " ;
    welcomeMessage += "/turnONpump or /turnOFFpump to either turn on or off the pump \n";
    welcomeMessage += "/ManualORAutomatic to switch operation modes \n";
    bot.sendMessage("286032787", welcomeMessage, "");
      
}

WateringDetails w;
bool manual = false; // setting inititial condition for manual control as false
bool lowWaterMessageShown = false;


void loop() {
  if (millis() - bot_lasttime > BOT_MTBS) {



    int *schedule;
    schedule = new int[w.getSchedLoop()];
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    String msg;

    MakeSchedule(w.getSchedStart(), w.getSchedInc(), w.getSchedLoop(), schedule);

    int soil_moisture=40;
    int rawADC;
    double sensorDATA[5]; //distance from TOF in mm


    get_sensorDATA(sensorDATA);
    soil_moisture = calculate_SM(maxADC, minADC, sensorDATA[3]);
    printInfo(sensorDATA, soil_moisture);
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    M5.Rtc.GetTime(&RTCtime);



    int state = 100; // initializing state parameter so that it's not equal to one of the cases

    if (numNewMessages!=0){
      state = 2; //state 2 is for user input
    }
    else if(sensorDATA[4] > (w.getTankHeight() - w.getminLevel()) && !lowWaterMessageShown){ //red zone. not enough water. pump off.
      digitalWrite(PUMP_PIN, false);
      bot.sendMessage(chat_id, "Not enough water. The pump will be turned off until you refill the tank.");
      lowWaterMessageShown = true;
    }
    else if (soil_moisture>w.getMinMoist() && !manual){
      digitalWrite(PUMP_PIN, false);
      M5.lcd.fillRect(80, 130, 240, 20, BLACK);
    }
    else if (soil_moisture<w.getMinMoist() && !manual && !lowWaterMessageShown){
      state =0; //state 0 is for too low moisture
    }
    else if(sensorDATA[4] <= (w.getTankHeight() - w.getminLevel() - 5) && lowWaterMessageShown){
      lowWaterMessageShown = false;
      bot.sendMessage(chat_id, "Refilled");
    }
    if (checkTime(schedule, w.getSchedLoop()) && !lowWaterMessageShown && RTCtime.Seconds<15){ //checks if current time is scheduled time
      state = 1; //state 1 is for scheduled watering
    }
   switch (state){
     case 0:{ //Moisture based automatic
        digitalWrite(PUMP_PIN, true); //turn on pump
        M5.Lcd.setCursor(80, 130);
        M5.Lcd.print("Moisture low!");
        break;
      }
     case 1:{ //RTC Case
       int temp;

        M5.Rtc.GetTime(&RTCtime);
        if (RTCtime.Seconds<55){
          temp = RTCtime.Seconds + 5;
        }
        else{
          temp = RTCtime.Seconds-55;
        }
        digitalWrite(PUMP_PIN, true); //turn on pump
        M5.Lcd.setCursor(80, 130);
        M5.Lcd.print("Water time!!");
        while (RTCtime.Seconds< temp){ //keep checking while pump is turned on
          M5.Rtc.GetTime(&RTCtime);
          get_sensorDATA(sensorDATA);
          soil_moisture = calculate_SM(maxADC, minADC, sensorDATA[3]);
          printInfo(sensorDATA, soil_moisture);
          delay(200);
          }
        digitalWrite(PUMP_PIN, false); //we know this part runs

        break; 
      }

     case 2:{

        handleNewMessages(numNewMessages);
      
     break;
     }
    }
      delay(100);

    delete[] schedule;
  }
}
 // end of the if statement with BOT_MTBS



void get_sensorDATA(double sensorDATA[]){
       sensorDATA[0] = qmp6988.calcPressure();
       if (sht30.get() == 0) {  // check if sesnor has any error, 0 means no error
        sensorDATA[1] = sht30.cTemp;   // Store the temperature obtained from shT30.
        sensorDATA[2] = sht30.humidity;  // Store the humidity obtained from the SHT30.
        }
       else {
        sensorDATA[1] = 0, sensorDATA[2] = 0;
        }
      sensorDATA[3] = analogRead(INPUT_PIN);
      sensorDATA[4] = TOF_sensor.readRangeContinuousMillimeters(); //distance from TOF in mm
}




int calculate_SM(int maxADC, int minADC, int rawADC){
  int soil_moisture = ((rawADC-maxADC)*100)/(minADC-maxADC);
  if (soil_moisture<0){
    return -1;
  }
  else if (soil_moisture>100){
    return 110;
  }
  else{
  return soil_moisture;
}
}

void MakeSchedule(int waterschedstart, int schedincrement, int schedloop, int *schedule){
  for (int i = 0; i<schedloop; i++){
    schedule[i] = waterschedstart + (i*schedincrement);
  }
}

bool checkTime(int *schedule, int size) {
    M5.Rtc.GetTime(&RTCtime);  // Gets the time in the real-time clock.
    M5.Rtc.GetDate(&RTCDate);
    for(int i=0; i<size;i++){
      if (RTCtime.Hours == schedule[i]/100 && RTCtime.Minutes == schedule[i]-((schedule[i]/100)*100)){
        return true;
      }
    }
    return false;
}

// Function to print out the 
void printInfo(double sensorDATA[], int soil_moisture){
      M5.lcd.fillRect(0, 60, 360, 70, BLACK);
      M5.Rtc.GetTime(&RTCtime);  // Gets the time in the real-time clock.
                             // 获取实时时钟内的时间
      M5.Rtc.GetDate(&RTCDate);
      char timeDATA[64];
      sprintf(timeDATA, "%d/%02d/%02d %02d:%02d:%02d", RTCDate.Year,
        RTCDate.Month, RTCDate.Date, RTCtime.Hours, RTCtime.Minutes,
        RTCtime.Seconds);
      M5.lcd.setCursor(80, 60);
      M5.Lcd.println(timeDATA);
      M5.Lcd.setCursor(80, 70);
      M5.Lcd.print("Pressure: " + String(sensorDATA[0]) + "Pa");
      M5.Lcd.setCursor(80, 80);
      M5.Lcd.print("Temperature: " + String(sensorDATA[1]) + "C");
      M5.Lcd.setCursor(80, 90);
      M5.Lcd.print("Humidity: " + String(sensorDATA[2]) + "%");
      M5.Lcd.setCursor(80, 100);
      M5.Lcd.print("ADC: " + String(sensorDATA[3]));
      M5.Lcd.setCursor(80, 110);
      M5.Lcd.print("Soil Moisture: " + String(soil_moisture) + "%");
      M5.Lcd.setCursor(80, 120);
      M5.Lcd.print("Min%: " + String(w.getMinMoist()));

}

//Function to setup RTC time
void setupTime() {
struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  RTCtime.Hours = timeinfo.tm_hour;  // Set the time
  RTCtime.Minutes = timeinfo.tm_min;
  RTCtime.Seconds = timeinfo.tm_sec;
  if (!M5.Rtc.SetTime(&RTCtime)) Serial.println("wrong time set!");
  RTCDate.Year = 1900+timeinfo.tm_year;  // Set the date
  RTCDate.Month = 1+timeinfo.tm_mon;
  RTCDate.Date = timeinfo.tm_mday;
  if (!M5.Rtc.SetDate(&RTCDate)) Serial.println("wrong date set!");
}

//Function to handle new messages sent to Telegram bot
void handleNewMessages(int numNewMessages)
{
  for (int i = 0; i < numNewMessages; i++)
  {
    String msg = bot.messages[i].text;
   
    double sensorDATA[5];
    int soil_moisture;
    if(msg == "/getData"){ //works
          get_sensorDATA(sensorDATA);
          soil_moisture = calculate_SM(maxADC, minADC, sensorDATA[3]);
          String print = "Pressure: " + String(sensorDATA[0]);
          print += "\nTemperature: ";
          print += String(sensorDATA[1]);
          print += "\nHumidity: ";
          print += String(sensorDATA[2]);
          print += "\nADC: ";
          print += String(sensorDATA[3]);
          print += "\nMoisture %: ";
          print += String(soil_moisture);
          print += "\nWater Level: ";
          if (sensorDATA[4] <= (w.getTankHeight()/4)){ //if water more than three fourth
            print += "High";
          } 
          else if (sensorDATA[4] <= (w.getTankHeight() * 3/4) && sensorDATA[4] > (w.getTankHeight() * 1/4)){ //if water more than three fourth
            print += "Medium";
          } 
          else{
            print += sensorDATA[4];
            print+= "Low";
          }
          bot.sendMessage(chat_id, print);
        }
        else if(msg == "/changeDetails"){
          bot.sendMessage(chat_id, "Please enter start time of watering in military format i.e. 8:20pm as 2020");
          while(true){
            get_sensorDATA(sensorDATA);
            soil_moisture = calculate_SM(maxADC, minADC, sensorDATA[3]);
            printInfo(sensorDATA, soil_moisture);
            numNewMessages = bot.getUpdates(bot.last_message_received + 1);
            if (numNewMessages != 0){
              w.setSchedStart( bot.messages[0].text.toInt());
              break;
            }
          }
          String print = "You entered ";
          print += String( w.getSchedStart());
          bot.sendMessage(chat_id, print);
          bot.sendMessage(chat_id, "Please enter how many times in a day you wish to get it watered");
          while(true){
            get_sensorDATA(sensorDATA);
            soil_moisture = calculate_SM(maxADC, minADC, sensorDATA[3]);
            printInfo(sensorDATA, soil_moisture);
            numNewMessages = bot.getUpdates(bot.last_message_received + 1);
            if (numNewMessages != 0){
              w.setSchedLoop(bot.messages[0].text.toInt());
              break;
            }
          }
          print = "You entered ";
          print += String( w.getSchedLoop());
          bot.sendMessage(chat_id, print);
          bot.sendMessage(chat_id, "Please enter the time in between each watering session in the format hhmm i.e. 1 hr and a half as 130");
          while(true){
            get_sensorDATA(sensorDATA);
            soil_moisture = calculate_SM(maxADC, minADC, sensorDATA[3]);
            printInfo(sensorDATA, soil_moisture);
            numNewMessages = bot.getUpdates(bot.last_message_received + 1);
            if (numNewMessages != 0){
              w.setSchedInc(bot.messages[0].text.toInt());
              break;
            }
          }
          print = "You entered ";
          print += String( w.getSchedInc());
          bot.sendMessage(chat_id, print);

          bot.sendMessage(chat_id, "Please enter water tank height in mm");
          while(true){
            get_sensorDATA(sensorDATA);
            soil_moisture = calculate_SM(maxADC, minADC, sensorDATA[3]);
            printInfo(sensorDATA, soil_moisture);
            numNewMessages = bot.getUpdates(bot.last_message_received + 1);
            if (numNewMessages != 0){
              w.setTankHeight(bot.messages[0].text.toInt());
              break;
            }
          }
          print = "You entered ";
          print += String( w.getTankHeight());
          bot.sendMessage(chat_id, print);



          bot.sendMessage(chat_id, "Done!");
        }
        else if(msg == "/turnONpump"){
          get_sensorDATA(sensorDATA);
          if (sensorDATA[4] > (w.getTankHeight() - w.getminLevel())){
            bot.sendMessage(chat_id, "Not enough water. Refill the tank.");
            digitalWrite(PUMP_PIN, false);
          }
          else{
          digitalWrite(PUMP_PIN, true);
          bot.sendMessage(chat_id, "Pump turned on!");
          manual = true;
          }
        }
        else if(msg == "/turnOFFpump"){
          manual = true;
          digitalWrite(PUMP_PIN, false);
          bot.sendMessage(chat_id, "Pump turned off!");
        }
        else if(msg == "/ManualORAutomatic"){
          bot.sendMessage(chat_id, "Please enter mode of operation for the pump (either /Automatic or /Manual");
          while(true){
            get_sensorDATA(sensorDATA);
            soil_moisture = calculate_SM(maxADC, minADC, sensorDATA[3]);
            printInfo(sensorDATA, soil_moisture);
            numNewMessages = bot.getUpdates(bot.last_message_received + 1);
            if (numNewMessages != 0){
              if (bot.messages[0].text == "/Automatic")
              {
                manual = false;
                break;
              }
              else if (bot.messages[0].text == "/Manual")
              {
                manual = true;
                break;
              }
              else 
              {
                bot.sendMessage(chat_id, "Invalid input");
              }
            }
          }
        }         
      } 
}



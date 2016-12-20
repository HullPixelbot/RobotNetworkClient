/*
 * 

Robot network client. Based on the basic ESP8266 MQTT example provided with the Arduino SDK

Uses modified PubSubClient code to connect to an Azure IOT Server - install this in 

Documents\Arduino\libraries 

- before building

The connects to an MQTT server then accepts imcoming commands which are passed over 
a serial connection to the motor contoroller. 

Each robot as a unique endpoint. Endpoint settings are selected using the processor ID of the 
esp8266 device so that the same code can be used in all robots. 

It will reconnect to the server if the connection is lost using a blocking
reconnect function. See the 'mqtt_reconnect_nonblocking' example for how to
achieve the same result without blocking the main loop.


To install the ESP8266 board, (using Arduino 1.6.4+):
- Add the following 3rd party board manager under "File -> Preferences -> Additional Boards Manager URLs":
http://arduino.esp8266.com/stable/package_esp8266com_index.json
- Open the "Tools -> Board -> Board Manager" and click install for the ESP8266"
- Select your ESP8266 in "Tools -> Board"

*/

#include <SoftwareSerial.h>
#define VERSION 1


#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#ifdef __AVR__
#include <avr/power.h>
#endif


// Update these with values suitable for your network.

const char *ssid = "SSID Here";
const char *password = "wifipasswordhere";

// Get this from your Azure settings

const char* mqtt_server = "YourIOTHubName.azure-devices.net";


const int serialReceivePin  = 12; //Pin D0 - GPI16 -> serial transmit
const int serialTransmitPin = 13; //Pin D5 - GPIO 14 -> serial receiver
SoftwareSerial robotSerial (serialReceivePin, serialTransmitPin);

const int led = 13;

#define DEBUG_ROBOT_SERIAL

int version = 1;

int distanceReading = 1;
int leftLightSensor = 2;
int rightLightSensor = 3;
int centreLightSensor = 4;

int leftMotorSpeed = 6;
int rightMotorSpeed = 7;

void sendRobotCommand(String command)
{

//#ifdef DEBUG_ROBOT_SERIAL
//	Serial.println(".**sendRobotCommand");
//	Serial.print(".  ");
//	Serial.println(command);
//#endif

	Serial.print((char)0x0d);
	Serial.print(command);
	Serial.print((char)0x0d);
}

WiFiClientSecure espClient;
PubSubClient client(espClient);

long lastMsg = 0;
char msg[50];
int value = 0;

void setup_wifi() {

	delay(10);

	// We start by connecting to a WiFi network
	Serial.println();
	Serial.print("Connecting to ");
	Serial.println(ssid);

	WiFi.begin(ssid, password);

	while (WiFi.status() != WL_CONNECTED) {
		// flash red and blue while we connect
		sendRobotCommand("PC0,0,255");
		delay(500);
		sendRobotCommand("PC255,0,0");
		delay(500);
		Serial.print(".");
	}

	Serial.println("");
	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());

	sendRobotCommand("PC0,0,0");
}

char message[50];

void callback(char* topic, byte* payload, unsigned int length) {
//	Serial.print("Message arrived [");
//	Serial.print(topic);
//	Serial.print("] ");
	int i;
	// Display the received message and build a robot command
	for (i = 0; i < length; i++) {
//		Serial.print((char)payload[i]);
		message[i] = (char)payload[i];
	}

	// Put the terminating character on the end of the message
	message[i] = 0;

	// Pass the command onto the motor processor
	sendRobotCommand(message);

	Serial.println();
}

const char * deviceName;
const char * deviceEndpoint;
const char * key;
const char * publishLocation;
const char * subscribeLocation;

// Hard wired endpoint settings which are mapped to the 
// processor id - not pretty, but working

boolean setupEndpointSettings()
{
  boolean deviceRecognised = false;
  switch(ESP.getChipId())
  {
    case 0x01:  //first robot - repeat for successive robots
      deviceName="robot1MQTTname";
      deviceEndpoint = "HullPixelbot.azure-devices.net/robot1MQTTname";
      key = "SharedAccessSignature sr=HullPixelbot.azure-devices.net%2Fdevices%robot1MQTTname&sig=key from Device Explorer tool";
      publishLocation = "devices/robot1MQTTname/messages/events/";
      subscribeLocation = "devices/robot1MQTTname/messages/devicebound/#";
      deviceRecognised = true;
      break;
  }
  
  return deviceRecognised;
}

#define RETRY_LIMIT 2

void reconnect() {

	int retryCount = 0;

	// Loop until we're reconnected
	while (!client.connected()) {

    // Set pixel to magenta
		sendRobotCommand("PC255,0,255");

		// force a reset after a few tries
		if (++retryCount == RETRY_LIMIT)
		{
			ESP.reset();
		}

		Serial.print("Attempting MQTT connection...");
		// Attempt to connect
		if (client.connect(deviceName, deviceEndpoint, key)) {
			Serial.println("connected");
			// Once connected, publish an announcement...
			client.publish(publishLocation, deviceName);
			// ... and resubscribe
			client.subscribe(subscribeLocation);
			sendRobotCommand("PC0,255,0");  // turn light green
		}
		else {
			Serial.print("failed, rc=");
			Serial.print(client.state());
			Serial.println(" try again in 2.5 seconds");
			// Wait 2.5 seconds before retrying
			for (int i = 0; i < 5; i++)
			{
				sendRobotCommand("PC255,0,0"); // light up red
				delay(250);
				sendRobotCommand("PC128,0,0"); // light up dim red
				delay(250);
			}
		}
	}
}

float versionNumber = 0.6;

void setup() {

	Serial.begin(9600);
  Serial.println(); 
  Serial.println(); 
  Serial.println("HullPixelbot");
  Serial.print("Version: ");
  Serial.println(versionNumber);
  Serial.println(); 
  Serial.print("Device ID: ");
  Serial.printf("%x",ESP.getChipId());
  Serial.println();
  Serial.println();

  if(!setupEndpointSettings() )
  {
    // device not registed in the cloud - oh dear
    sendRobotCommand("PC0,0,255"); // light up blue
    while(true)
    {
      delay(250);
    }
  }
	
	robotSerial.begin(9600);
	setup_wifi();
	client.setServer(mqtt_server, 8883);
	client.setCallback(callback);

}

void loop() {

	if (!client.connected()) {
		Serial.println("reconnecting");
		reconnect();
	}

	if (!client.loop())
		Serial.println("loop failed");
}

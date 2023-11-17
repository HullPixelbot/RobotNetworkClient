/*
HullOS Network client. Provides a network connection to an MQTT server
that can send commands to a robot running HullOS.

Connect the robot to the main serial port of the Wemos.

Works with version R1.2 of the HullOS software.

MQTT and WiFi settings are configured using the HullOS Network Client Config application

*/

#include <string>

#include <EEPROM.h>
#include <SoftwareSerial.h>
#define VERSION "1.1"

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#ifdef __AVR__
#include <avr/power.h>
#endif

const int led = 2;
bool led_lit;

#define WIFI_TRY_LIMIT 20

#define ACCESS_POINT_NAME_LENGTH 40
#define ACCESS_POINT_PASSWORD_LENGTH 40
#define NO_OF_ACCESS_POINTS 5

#define DEVICE_NAME_LENGTH 50
#define AZURE_ENDPOINT_LENGTH 200
#define AZURE_KEY_LENGTH 200
#define MQTT_USERNAME_LENGTH 200

struct WiFiDetails
{
	char name[ACCESS_POINT_NAME_LENGTH + 1];
	char password[ACCESS_POINT_PASSWORD_LENGTH + 1];
};

struct Settings
{
	char Flag1;

	WiFiDetails wifi[NO_OF_ACCESS_POINTS];

	char Device_Name[DEVICE_NAME_LENGTH + 1];
	char MQTT_Username[MQTT_USERNAME_LENGTH + 1];
	char Azure_Endpoint[AZURE_ENDPOINT_LENGTH + 1];
	char Azure_Key[AZURE_KEY_LENGTH + 1];

	// Flag 2 at the end to detect any changes in length of the config
	char Flag2;
};

Settings network_config;

#define EEPROM_SIZE 4096
#define FLAG1 0x55
#define FLAG2 0xAA

void led_on();
void led_off();

void setup_led()
{
	pinMode(led, OUTPUT);
	led_off();
}

void led_on()
{
	digitalWrite(led, 0);
	led_lit = true;
}

void led_off()
{
	digitalWrite(led, 1);
	led_lit = false;
}

void led_flip()
{
	if (led_lit)
		led_off();
	else
		led_on();
}

#define DIGIT_FLASH_INTERVAL 400
#define DIGIT_GAP 500

void flashStatusLedDigits(int flashes)
{
	for (int i = 0; i < flashes; i++)
	{
		led_on();
		delay(DIGIT_FLASH_INTERVAL);
		led_off();
		delay(DIGIT_FLASH_INTERVAL);
	}
}

void flashStatusLed(int flashes)
{
	int tenPowers = 1;

	while (flashes > tenPowers)
	{
		tenPowers = tenPowers * 10;
	}

	tenPowers = tenPowers / 10;

	while (tenPowers > 0)
	{
		int digit = (flashes / tenPowers) % 10;
		flashStatusLedDigits(digit);
		tenPowers = tenPowers / 10;
		delay(DIGIT_GAP);
	}
}

int version = 1;
bool robotConnected = false;
bool mqttLive = false;

int distanceReading = 1;
int leftLightSensor = 2;
int rightLightSensor = 3;
int centreLightSensor = 4;

int leftMotorSpeed = 6;
int rightMotorSpeed = 7;

void sendRobotCommand(String command)
{
	led_flip();

	Serial.print((char)0x0d);
	Serial.print(command);
	Serial.print((char)0x0d);
	Serial.print((char)0x0a);
}

void sendRobotChar(char ch)
{
	Serial.print(ch);
}

#define CONNECTING_TO_WIFI 1
#define NO_WIFI_ACCESS_POINTS_FOUND 2
#define NO_ACCESS_POINT_NAMES_MATCH_CONFIG 3
#define CONNECTING_TO_ACCESS_POINT 4
#define ACCESS_POINT_CONNECTED 5
#define ACCESS_POINT_CONNECT_FAILED 6
#define MQTT_CONNECTION_FAILED 7
#define MQTT_CONNECTED_OK 8

#define FLASH_INTERVAL 500

int previousStatusDisplay = 0;
bool ledLit = false;

unsigned long nextFlashTime = 0;

char accessPointName[ACCESS_POINT_NAME_LENGTH + 1];
char accessPointPassword[ACCESS_POINT_PASSWORD_LENGTH + 1];
// Leave room for the separator slash and the zero terminator
char deviceEndpoint[AZURE_ENDPOINT_LENGTH + DEVICE_NAME_LENGTH + 2];
char publishLocation[DEVICE_NAME_LENGTH + 25];
char subscribeLocation[DEVICE_NAME_LENGTH + 30];

void updateRobotStatus(int status)
{
	if (status == previousStatusDisplay)
	{
		// might have to flash the led
		if (nextFlashTime > millis())
		{
			// not time to flash yet, just return
			return;
		}

		// set the time for the next flash

		nextFlashTime = millis() + FLASH_INTERVAL;

		if (ledLit)
		{
			// Turn the light off
			sendRobotCommand("BLACK");
			ledLit = false;
			return;
		}
		else
		{
			// Turning the light back on again
			ledLit = true;
		}
	}
	else
	{
		previousStatusDisplay = status;
	}

	switch (status)
	{
	case CONNECTING_TO_WIFI:
		sendRobotCommand("BLUE");
		break;
	case NO_WIFI_ACCESS_POINTS_FOUND:
		sendRobotCommand("RED");
		break;
	case NO_ACCESS_POINT_NAMES_MATCH_CONFIG:
		// go orange
		sendRobotCommand("*PC255,165,0");
		break;
	case CONNECTING_TO_ACCESS_POINT:
		sendRobotCommand("YELLOW");
		break;
	case ACCESS_POINT_CONNECTED:
		sendRobotCommand("WHITE");
		break;
	case ACCESS_POINT_CONNECT_FAILED:
		sendRobotCommand("CYAN");
		break;
	case MQTT_CONNECTION_FAILED:
		sendRobotCommand("MAGENTA");
		break;
	case MQTT_CONNECTED_OK:
		sendRobotCommand("GREEN");
		break;
	}
}

void updateClientStatus(int status)
{
	if (status == previousStatusDisplay)
	{
		return;
	}

	previousStatusDisplay = status;

	switch (status)
	{
	case CONNECTING_TO_WIFI:
		Serial.println("Connecting to wifi");
		break;
	case NO_WIFI_ACCESS_POINTS_FOUND:
		Serial.println("No wifi access points found");
		break;
	case NO_ACCESS_POINT_NAMES_MATCH_CONFIG:
		Serial.println("No wifi access point names match config");
		break;
	case CONNECTING_TO_ACCESS_POINT:
		Serial.print("Connecting to: ");
		Serial.println(accessPointName);
		break;
	case ACCESS_POINT_CONNECTED:
		Serial.println("Connected to access point OK");
    Serial.print("MAC address:");
    Serial.println(WiFi.macAddress());
		break;
	case ACCESS_POINT_CONNECT_FAILED:
		Serial.println("Access point connection failed");
		break;
	case MQTT_CONNECTION_FAILED:
		Serial.println("MQTT connection failed");
		break;
	case MQTT_CONNECTED_OK:
		Serial.println("MQTT connected OK");
		break;
	}
}

// Updates the status display
// Either sets a colour or displays a message

void updateStatusDisplay(int status)
{
	if (robotConnected)
	{
		updateRobotStatus(status);
	}
	else
	{
		updateClientStatus(status);
	}
}

void list_access_points()
{
	Serial.println("Listing access points");
	int network_count = WiFi.scanNetworks();
	Serial.printf("%d network(s) found\n", network_count);
	for (int i = 0; i < network_count; i++)
	{
		Serial.printf("%d: %s, Ch:%d (%ddBm) %s\n",
					  i + 1,
					  WiFi.SSID(i).c_str(),
					  WiFi.channel(i),
					  WiFi.RSSI(i),
					  WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "");
	}
}

void dumpString(char *str)
{
	Serial.print("String: ");
	Serial.println(str);
	Serial.print("Chars: ");
	while (*str != 0)
	{
		Serial.print((int)*str);
		Serial.print(' ');
		str++;
	}
	Serial.println();
}

char accessPointNameDump[ACCESS_POINT_NAME_LENGTH];

// finds an access point and copies the ap name and password
int findAccessPointName()
{
	int network_count = WiFi.scanNetworks();

	// If there are no access points present - go red
	// and return

	if (network_count == 0)
	{
		return NO_WIFI_ACCESS_POINTS_FOUND;
	}

	for (int i = 0; i < NO_OF_ACCESS_POINTS; i++)
	{
		strcpy(accessPointNameDump, WiFi.SSID(i).c_str());

		// if (!robotConnected)
		//{
		//	Serial.print("Checking: ");
		//	dumpString(accessPointNameDump);
		// }

		for (int i = 0; i < network_count; i++)
		{
			// if (!robotConnected)
			//{
			//	Serial.print("  With : ");
			//	dumpString(network_config.wifi[i].name);
			// }
			int cmp = strcmp(network_config.wifi[i].name, accessPointNameDump);

			// Serial.print("Compare: ");
			// Serial.println(cmp);

			if (cmp == 0)
			{
				strcpy(accessPointName, network_config.wifi[i].name);
				strcpy(accessPointPassword, network_config.wifi[i].password);
				return CONNECTING_TO_ACCESS_POINT;
			}
		}
	}
	return NO_ACCESS_POINT_NAMES_MATCH_CONFIG;
}

 uint8_t mac[6] {0xA8, 0xD9, 0xB3, 0x0D, 0xAA, 0xCE};    

int connectToAP()
{
	WiFi.begin(accessPointName, accessPointPassword);

  // wifi_set_macaddr(0, const_cast<uint8*>(mac));   //This line changes MAC adderss of ESP8266

	int tryCount = 0;

	while (WiFi.status() != WL_CONNECTED && tryCount < WIFI_TRY_LIMIT)
	{
		tryCount++;
		updateStatusDisplay(CONNECTING_TO_ACCESS_POINT);
		delay(500);
	}

	if (WiFi.status() == WL_CONNECTED)
	{
		return ACCESS_POINT_CONNECTED;
	}
	else
	{
		// may need to abandon scan here
		WiFi.disconnect();
		return ACCESS_POINT_CONNECT_FAILED;
	}
}

Client *espClient = NULL;

PubSubClient *mqttPubSubClient = NULL;

int mqttPort;

void setupPubSubClient(bool secure)
{
	if (mqttPubSubClient != NULL)
		return;

	if (secure)
	{
		espClient = new WiFiClientSecure();
		mqttPort = 8883;
	}
	else
	{
		espClient = new WiFiClient();
		mqttPort = 1883;
	}

	mqttPubSubClient = new PubSubClient(*espClient);
}

int connectToWifi()
{
	int reply;

	for (int i = 0; i < NO_OF_ACCESS_POINTS; i++)
	{
		// Ignore empty slots
		if (network_config.wifi[i].name[0] == 0)
			continue;

		updateStatusDisplay(CONNECTING_TO_WIFI);

		delay(500);

		strcpy(accessPointName, network_config.wifi[i].name);
		strcpy(accessPointPassword, network_config.wifi[i].password);

		reply = connectToAP();

		if (reply == ACCESS_POINT_CONNECTED)
			return reply;
	}

	return ACCESS_POINT_CONNECT_FAILED;
}

bool check_for_Azure(const char *s1)
{
	return strstr(s1, "azure") != NULL;
}

char message[50];

void callback(char *topic, byte *payload, unsigned int length);

int connect_to_mqtt()
{

	setupPubSubClient(check_for_Azure(network_config.Azure_Endpoint));

	mqttPubSubClient->setServer(network_config.Azure_Endpoint, mqttPort);
	mqttPubSubClient->setCallback(callback);

	strcpy(deviceEndpoint, network_config.Azure_Endpoint);

	//	strcat(deviceEndpoint, "/");
	//	strcat(deviceEndpoint, network_config.Device_Name);

	strcpy(publishLocation, "devices/");
	strcat(publishLocation, network_config.Device_Name);
	strcat(publishLocation, "/messages/events/");

	strcpy(subscribeLocation, "devices/");
	strcat(subscribeLocation, network_config.Device_Name);
	strcat(subscribeLocation, "/messages/devicebound/#");

	//  Serial.printf("Name: %s Endpoint: %s Key: %s\n", network_config.Device_Name, network_config.MQTT_Username, network_config.Azure_Key);

	if (!robotConnected)
	{
		Serial.print("Endpoint: ");
		Serial.println(deviceEndpoint);

		Serial.print("Publish: ");
		Serial.println(publishLocation);

		Serial.print("Subscribe: ");
		Serial.println(subscribeLocation);
	}

	if (mqttPubSubClient->connect(network_config.Device_Name, network_config.MQTT_Username, network_config.Azure_Key))
	{
		mqttPubSubClient->publish(publishLocation, network_config.Device_Name);
		// ... and resubscribe
		mqttPubSubClient->subscribe(subscribeLocation);
		return MQTT_CONNECTED_OK;
	}

	return MQTT_CONNECTION_FAILED;
}

bool connect_to_network()
{
	// remove any existing connection

	WiFi.disconnect();

	// Now connect to the WiFi
	int reply = connectToWifi();

	updateStatusDisplay(reply);

	if (reply != ACCESS_POINT_CONNECTED)
		return false;

	reply = connect_to_mqtt();

	updateStatusDisplay(reply);

	if (reply != MQTT_CONNECTED_OK)
	{
		mqttLive = false;
		return false;
	}

	mqttPubSubClient->setServer(network_config.Azure_Endpoint, 8883);
	mqttPubSubClient->setCallback(callback);

	mqttLive = true;

	return true;
}

#define RESPONSE_TIMEOUT 5000

#define INPUT_BUFFER_SIZE 1000

char buffer[INPUT_BUFFER_SIZE];
char *bufferPos = buffer;
const char *bufferLimit = buffer + INPUT_BUFFER_SIZE;

void startBuffering()
{
	bufferPos = buffer;
}

bool startsWith(const char *pre, const char *str)
{
	int preLen = strlen(pre);
	int strLen = strlen(pre);

	if (preLen > strLen)
		return false;

	while (preLen > 0)
	{
		if (*pre != *str)
			return false;
		pre++;
		str++;
		preLen--;
	}
	return true;
}

void start_eeprom()
{
	EEPROM.begin(EEPROM_SIZE);
}

void load_config_from_eeprom()
{
	char *config_byte = (char *)&network_config;

	for (int i = 0; i < sizeof(network_config); i++)
	{
		config_byte[i] = EEPROM.read(i);
	}
}

void store_config_in_eeprom()
{
	char *config_byte = (char *)&network_config;

	for (int i = 0; i < sizeof(network_config); i++)
	{
		EEPROM.write(i, config_byte[i]);
	}

	EEPROM.commit();
}

bool config_is_stored()
{
	return (network_config.Flag1 == FLAG1 && network_config.Flag2 == FLAG2);
}

void reset_network_config()
{
	network_config.Flag1 = FLAG1;
	network_config.Flag2 = FLAG2;

	for (int i = 0; i < NO_OF_ACCESS_POINTS; i++)
	{
		network_config.wifi[i].name[0] = 0;
		network_config.wifi[i].password[0] = 0;
	}

	network_config.Device_Name[0] = 0;
	network_config.Azure_Endpoint[0] = 0;
	network_config.Azure_Key[0] = 0;
}

void demo_network_config()
{
	reset_network_config();

	strcpy(network_config.wifi[0].name, "Put");
	strcpy(network_config.wifi[0].password, "Your");

	strcpy(network_config.wifi[1].name, "Default");
	strcpy(network_config.wifi[1].password, "Settings");

	strcpy(network_config.wifi[2].name, "Here");
	strcpy(network_config.wifi[2].password, "For");

	strcpy(network_config.Device_Name, "Quick");
	strcpy(network_config.MQTT_Username, "Deployment");
	strcpy(network_config.Azure_Endpoint, "To");
	strcpy(network_config.Azure_Key, "A Robot");
	store_config_in_eeprom();
}

#define EMPTY_PASSWORD "********"

void send_config_to_serial()
{
	for (int i = 0; i < NO_OF_ACCESS_POINTS; i++)
	{
		Serial.println(network_config.wifi[i].name);
			Serial.println(network_config.wifi[i].password);
		if (network_config.wifi[i].password[0] != 0)
			Serial.println(EMPTY_PASSWORD);
		else
			Serial.println();
	}

	Serial.println(network_config.Device_Name);
	Serial.println(network_config.MQTT_Username);
	Serial.println(network_config.Azure_Endpoint);

	if (network_config.Azure_Key[0] != 0)
		Serial.println(EMPTY_PASSWORD);
	else
		Serial.println();
}

// If we get more characters than the buffer can hold we ignore those and
// send a fail report when we get the line ending

bool getLine(char *dest, int size, int timeoutInMilliseconds)
{
	int count = 1;

	while (timeoutInMilliseconds > 0)
	{
		while (Serial.available())
		{
			char ch = Serial.read();

			if (ch == '\n' || ch == '\r')
			{
				*dest = 0;

				if (count == size)
					return false;
				else
					return true;
			}

			*dest = ch;

			count++;

			if (count <= size)
			{
				dest++;
			}
		}
		delay(1);
		timeoutInMilliseconds--;
	}
	return false;
}

bool read_config_from_serial(int timeout)
{
	network_config.Flag1 = 0;

	char password_buffer[AZURE_KEY_LENGTH + 1];

	for (int i = 0; i < NO_OF_ACCESS_POINTS; i++)
	{
		if (!getLine(network_config.wifi[i].name, ACCESS_POINT_NAME_LENGTH, timeout))
			return false;

		if (!getLine(password_buffer, ACCESS_POINT_PASSWORD_LENGTH, timeout))
			return false;

		// If the password has been changed - updated it
		if (strcmp(EMPTY_PASSWORD, password_buffer) != 0)
			strcpy(network_config.wifi[i].password, password_buffer);
	}

	if (!getLine(network_config.Device_Name, DEVICE_NAME_LENGTH, timeout))
		return false;

	if (!getLine(network_config.MQTT_Username, MQTT_USERNAME_LENGTH, timeout))
		return false;

	if (!getLine(network_config.Azure_Endpoint, AZURE_ENDPOINT_LENGTH, timeout))
		return false;

	if (!getLine(password_buffer, AZURE_KEY_LENGTH, timeout))
		return false;

	// If the password has been changed - update it
	if (strcmp(EMPTY_PASSWORD, password_buffer) != 0)
		strcpy(network_config.Azure_Key, password_buffer);

	network_config.Flag1 = FLAG1;
	network_config.Flag2 = FLAG2;

	return true;
}

void setup_config()
{
	start_eeprom();

	load_config_from_eeprom();

	if (!config_is_stored())
	{
		reset_network_config();
		store_config_in_eeprom();
	}
}

void processCommand(char *command)
{
	if (strcmp(command, "*IV") == 0)
	{
		Serial.print("Network Client Version ");
		Serial.println(VERSION);
		return;
	}

	if (strcmp(command, "*NI") == 0)
	{
		Serial.printf("%x", ESP.getChipId());
		Serial.println();
		return;
	}

	// Network configuration read, send the configuration data
	// to the host
	if (strcmp(command, "*NR") == 0)
	{
		send_config_to_serial();
		return;
	}

	if (strcmp(command, "*ND") == 0)
	{
		demo_network_config();
		store_config_in_eeprom();
		return;
	}

	if (strcmp(command, "*NS") == 0)
	{
		if (!read_config_from_serial(RESPONSE_TIMEOUT))
		{
			Serial.println("download failed");
			return;
		}
		store_config_in_eeprom();
		return;
	}

	if (strcmp(command, "*NA") == 0)
	{
		list_access_points();
		return;
	}

	if (strcmp(command, "*NC") == 0)
	{
		connect_to_network();
		return;
	}
}

void callback(char *topic, byte *payload, unsigned int length)
{
	sendRobotChar((char)0x0d);

	for (int i = 0; i < length; i++)
	{
		sendRobotChar((char)payload[i]);
	}

	// put the terminating byte on

	sendRobotChar((char)0x0d);
	led_flip();
}

void processLine(char *command)
{
	if (robotConnected)
		mqttPubSubClient->publish(publishLocation, command);
	else
		processCommand(command);
}

void bufferChar(char ch)
{
	if (ch == '\n' || ch == '\r')
	{
		*bufferPos = 0;
		processLine(buffer);
		bufferPos = buffer;
		return;
	}

	*bufferPos = ch;

	if (bufferPos < bufferLimit)
	{
		bufferPos++;
	}
}

void processIncomingChars()
{
	while (Serial.available())
	{
		bufferChar(Serial.read());
	}
}

bool readLIne(int timeoutInMilliseconds)
{
	bufferPos = buffer;
	while (timeoutInMilliseconds)
	{
		while (Serial.available())
		{
			char ch = Serial.read();

			if (ch == '\n' || ch == '\r')
			{
				*bufferPos = 0;
				return true;
			}

			*bufferPos = ch;

			if (bufferPos < bufferLimit)
			{
				bufferPos++;
			}
		}
		delay(1);
		timeoutInMilliseconds--;
	}
	return false;
}

bool readLines(int noOfLines, int timeoutInMilliseconds)
{
	for (int i = 0; i < noOfLines; i++)
	{
		if (!readLIne(timeoutInMilliseconds))
			return false;
		// Replace the terminator with a separator
		*bufferPos = '\n';
	}
	// Put the terminator after the last line
	*bufferPos = 0;
	return true;
}

bool findRobot()
{
	led_on();
	// Flush out any received messages
	Serial.flush();

	// Give the motor controller time to boot
	delay(1000);

	led_off();

	delay(1000);

	// Stop a program if there is one
	sendRobotCommand("*RH");

	delay(200);

	// Ask the robot for version information
	sendRobotCommand("*IV");

	if (!readLIne(RESPONSE_TIMEOUT))
		return false;

	if (startsWith("HullOS", buffer))
	{
		return true;
	}

	flashStatusLed(5);

	delay(1000);

	return false;
}

void startRobot()
{
	if (findRobot())
	{
		robotConnected = true;
		connect_to_network();
		flashStatusLed(1);
	}
	else
	{
		flashStatusLed(2);
	}
}

void setup()
{

	Serial.begin(1200);

	WiFi.mode(WIFI_OFF);

	setup_led();

	setup_config();

	startRobot();

	startBuffering();
}

void loop()
{

	processIncomingChars();

	if (mqttLive)
	{
		if (!mqttPubSubClient->connected())
		{
			int reply = connect_to_mqtt();
			updateStatusDisplay(reply);
		}
		mqttPubSubClient->loop();
	}
}

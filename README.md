# RobotNetworkClient
Network client for HullPixelbot robot

This provides the network connectivity for an individual HullPixelbot. It connects to an Azure IoT hub using MQTT and receives commands from that hub. It does not drive the robot directly, instead the commands are transferred via a serial connection to the robot controller board. 

The program sets up MQTT endpoints to exchange messages with an Azure IoT Hub. Each robot uses a unique address which is hard coded into the program (see the designated points) but the program can use the device id of the host processor to perform automatic configuation within a group of robots. This makes it possible to have the same program image running in a large number of robots. 

The program was written using the Arduino development kit and is intended for the esp8266 device. The preferred platform is the WEMOS D1 mini, although other esp8266 devices may be used. 

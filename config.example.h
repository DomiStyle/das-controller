/*
 * Components
 */

bool enableInput = false; // Enable local toggling via pin
bool enableHttp = true; // Enable HTTP server
bool enableMqtt = false; // Enable MQTT client
bool enableTelnet = false; // Enable Telnet client
bool enableStatus = true; //Enable status LEDs
bool enableRgbStatus = false; // Enable RGB status

/*
 * General options
 */

const char* name = "das01"; // At least 4 characters

byte mac[] = { 0x00, 0xAA, (byte)name[0], (byte)name[1], (byte)name[2], (byte)name[4] }; // MAC address; uses parts of the name above, make sure it's unique

/*
 * Status LED
 */

unsigned long statusFalloffDelay = 500; // Time after which LED returns to off

uint8_t statusPins[] = { 2, 3, 4, 5, 6 }; // Either the 3 pins for the RGB led when using enableRgbStatus or the 5 LEDs of the front case

/*
 * Network
 */

bool useDhcp = false; // Whether to use DHCP
IPAddress ip(192, 168, 1, 111); // Only used when not using DHCP

/*
 * HTTP
 */

const char* onCommand = "on"; // Set name of on command (HTTP only)
const char* offCommand = "off"; // Set name of off command (HTTP only)
const char* toggleCommand = "toggle"; // Set name of toggle command (HTTP only)
const char* pulseCommand = "pulse"; // Set name of pulse command (HTTP only)
const char* lockCommand = "lock"; // Set name of lock command (HTTP only)
const char* unlockCommand = "unlock"; // Set name of unlock command (HTTP only)
const char* statusCommand = "status"; // Set name of status command (HTTP only)

/*
 * MQTT
 */

IPAddress mqttServer(192, 168, 1, 1); // MQTT server to connect to
unsigned short mqttPort = 1883; // Port of the MQTT server

const long mqttReconnectinterval = 30000; // Reconnect interval after connection is lost

/*
 * Telnet
 */

IPAddress telnetServer(192, 168, 1, 100); // Telnet server to poll
unsigned short telnetPort = 23; // Port of the telnet server

const char* telnetUsername = "root";
const char* telnetPassword = "1234";

const char * telnetCommand = "show -d properties /admin1/system1"; // Command to run

const long telnetInterval = 15000; // Interval at which to run the command
const long telnetTimeout = 120000; // Timeout of the telnet socket
const long telnetReconnectInterval = 30000; // Reconnect interval after connection is lost
unsigned long telnetResponseDelay = 2000; // Milliseconds to wait for a response for the command from the server

/*
 * Outputs
 */

unsigned long toggleDelay = 100; // Delay when toggling an output

Output outputs[] =
{
  {
    "drives", // Name of the output
    "/drives", // URL of the output
    "das01/drives/control", // Topic for the output
    "das01/drives/report", // Topic to report state to
    -1, // Duration of pulse

    8, // Output pin; connect pin 8 to PS_ON on the ATX power supply
    true, // Enable output (makes output virtual if set to false)
    true, // Whether the output is active low
    0, // Input pin (only if local toggling is enabled)
    false, // Whether the input is active low
    0, // The mode of the input (0 = off, 1 = copy, 2 = copy high only, 3 = pulse, 4 = toggle, 5 = hold)

    LOW, // Startup state

    true, // Allow turning this output on
    true, // Allow turning this output off
    false, // Allow toggling this output
    false, // Allow pulsing this output
    true, // Allow locking this output (no changes possible)
    true, // Allow requesting the status of this output via HTTP
    true // Enable MQTT reports when output status changes
  }
};

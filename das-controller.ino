#include <Ethernet.h>
#include <PubSubClient.h>

/*
 * Type definitions
 */

typedef struct
{
  const char* name; // Name of the output
  const char* url; // URL of the output
  const char* controlTopic; // Topic for the output
  const char* reportTopic; // Topic to report state to
  int pulseDuration; // Duration of pulse

  uint8_t outputPin; // Output pin
  bool enableOutput; // Enable output (makes output virtual if set to false)
  bool outputActiveLow; // Whether the output is active low
  uint8_t inputPin; // Input pin (only if local toggling is enabled)
  bool inputActiveLow; // Whether the input is active low
  uint8_t inputMode; // The mode of the input (0 = off, 1 = copy, 2 = copy high only, 3 = pulse, 4 = toggle, 5 = hold)

  uint8_t state; // Startup state

  bool enableOn; // Allow turning this output on
  bool enableOff; // Allow turning this output off
  bool enableToggle; // Allow toggling this output
  bool enablePulse; // Allow pulsing this output
  bool enableLock; // Allow locking this output (no changes possible)
  bool enableStatus; // Allow requesting the status of this output via HTTP
  bool enableReport; // Enable MQTT reports when output status changes

  unsigned long lastChange; // INTERNAL: Last time input was changed
  bool pulsing; // INTERNAL: Whether output is pulsing right now
  bool holding; // INTERNAL: Whether the input corresponding to the output is being held down
  bool locked; // INTERNAL: Whether the output is locked
} Output;

#include "config.h"

const char* version = "1.6";

/*
 * Enums
 */

enum Result { SUCCESS, UNCHANGED, STATUS, ROOT, MISSING, DISABLED, FAILED, LOCKED, COOLDOWN }; // Result for operation
enum Operation { ON, OFF, TOGGLE, PULSE, LOCK, UNLOCK, UNKNOWN }; // Operation to run
enum Status { OK, ERROR, WARNING, WORKING1, WORKING2, WORKING3, INITIALIZING, NONE }; // Status to display via LED

/*
 * Internal
 */

uint8_t outputCount;
uint8_t statusPinsCount;
bool enableNetwork = enableHttp || enableMqtt || enableTelnet;

EthernetClient mqttEthClient; // Ethernet client for MQTT
PubSubClient mqttClient(mqttEthClient); // MQTT client

EthernetClient telnetClient; // Telnet client

EthernetServer httpServer(80); // HTTP server

unsigned long lastMqttReconnect;

unsigned long lastTelnetReconnect;
unsigned long lastTelnetPoll;
unsigned long lastTelnetToggle = millis() - telnetTimeout;
unsigned long lastStatusChange;

unsigned long debounceDelay = 50;

bool telnetWaiting = false;
bool statusFalloff = false;

/*
 * Helpers
 */

bool startsWith(const char* str, const char* pre)
{
  return strncmp(pre, str, strlen(pre)) == 0;
}

char* join(const char* str1, const char* str2)
{
  char* result = (char*)malloc(strlen(str1) + strlen(str2) + 1);

  strcpy(result, str1);
  strcat(result, str2);

  return result;
}

int indexOf(const char* str, const char find)
{
  const char* ptr = strchr(str, find);

  if(ptr)
    return ptr - str;
  else
    return -1;
}

int indexOfBack(const char* str, const char find)
{
  const char* ptr = strrchr(str, find);

  if(ptr)
    return ptr - str;
  else
    return -1;
}

char* substr(const char* from, int index, size_t length)
{
  char* target = (char*)malloc(length+1); // Allocate enough to fit new string + null terminator
  target[length] = '\0'; // Null terminate string

  strncpy(target, from+index, length); // Copy string to target

  return target;
}

void readline(char* buffer, int bufferSize, EthernetClient client, bool ignoreWhitespaces = false)
{
  int index = 0;

  while (client.connected() && client.available()) // Loop until client disconnected or no data is left
  {
    if(index > bufferSize - 2) // Prevent overflowing array; leave room for the null terminator
    {
      return;
    }

    char c = client.read(); // Read character

    if(ignoreWhitespaces && index == 0 && (c == ' ' || c == '\r' || c == '\n'))
    {
      continue;
    }

    buffer[index] = c; // Add character to line
    buffer[index+1] = '\0'; // Set null terminator to next character
    index++; // Increase index

    if(index > 1 && c =='\n' && buffer[index-2] == '\r') // New line reached
    {
      return;
    }
  }
}

void setStatus(Status status, bool falloff = false)
{
  if(enableStatus || enableRgbStatus)
  {
    if(enableRgbStatus)
    {
      if(status == OK) // Green
      {
        analogWrite(statusPins[0], 0);
        analogWrite(statusPins[1], 255);
        analogWrite(statusPins[2], 0);
      }
      else if(status == ERROR) // Red
      {
        analogWrite(statusPins[0], 255);
        analogWrite(statusPins[1], 0);
        analogWrite(statusPins[2], 0);
      }
      else if(status == WARNING) // Yellow
      {
        analogWrite(statusPins[0], 255);
        analogWrite(statusPins[1], 255);
        analogWrite(statusPins[2], 0);
      }
      else if(status == WORKING1) // Pink
      {
        analogWrite(statusPins[0], 255);
        analogWrite(statusPins[1], 0);
        analogWrite(statusPins[2], 255);
      }
      else if(status == WORKING2) // Cyan
      {
        analogWrite(statusPins[0], 0);
        analogWrite(statusPins[1], 255);
        analogWrite(statusPins[2], 255);
      }
      else if(status == WORKING3) // Blue
      {
        analogWrite(statusPins[0], 0);
        analogWrite(statusPins[1], 0);
        analogWrite(statusPins[2], 255);
      }
      else if(status == INITIALIZING) // White
      {
        analogWrite(statusPins[0], 255);
        analogWrite(statusPins[1], 255);
        analogWrite(statusPins[2], 255);
      }
      else if(status == NONE) // Off
      {
        analogWrite(statusPins[0], 0);
        analogWrite(statusPins[1], 0);
        analogWrite(statusPins[2], 0);
      }
    }
    else
    {
      if(status == OK)
      {
        digitalWrite(statusPins[0], HIGH);
      }
      else if(status == ERROR)
      {
        digitalWrite(statusPins[4], HIGH);
      }
      else if(status == WARNING)
      {
        digitalWrite(statusPins[4], HIGH);
      }
      else if(status == WORKING1)
      {
        digitalWrite(statusPins[1], HIGH);
      }
      else if(status == WORKING2)
      {
        digitalWrite(statusPins[2], HIGH);
      }
      else if(status == WORKING3)
      {
        digitalWrite(statusPins[3], HIGH);
      }
      else if(status == INITIALIZING)
      {
        for(int i = 0; i < statusPinsCount; i++) // Loop through available status LEDs
        {
          digitalWrite(statusPins[i], HIGH);
        }
      }
      else if(status == NONE)
      {
        for(int i = 0; i < statusPinsCount; i++) // Loop through available status LEDs
        {
          digitalWrite(statusPins[i], LOW);
        }
      }
    }

    lastStatusChange = millis();

    if(falloff)
      statusFalloff = true;
    else
      statusFalloff = false;
  }
}

Operation parseCommand(const char* command)
{
  if(strcmp(command, onCommand) == 0)
    return ON;
  else if(strcmp(command, offCommand) == 0)
    return OFF;
  else if(strcmp(command, toggleCommand) == 0)
    return TOGGLE;
  else if(strcmp(command, pulseCommand) == 0)
    return PULSE;
  else if(strcmp(command, lockCommand) == 0)
    return LOCK;
  else if(strcmp(command, unlockCommand) == 0)
    return UNLOCK;
  else
  {
    Serial.println(F("Received unknown command"));
    return UNKNOWN;
  }
}

/*
 * Execute command
 */

Result execute(Output* output, Operation operation, bool internal = false)
{
  unsigned long currentMillis = millis();

  if(output->locked && operation != UNLOCK)
  {
    return LOCKED; // Do not allow any changes on locked output
  }

  if(operation == ON)
  {
    if(!output->enableOn && !internal) // Check if this output is allowed to turn on
      return DISABLED;

    if(output->state == HIGH) // Check if the state already is set
      return UNCHANGED;

    if(output->outputActiveLow)
      digitalWrite(output->outputPin, LOW); // Reverse for active low outputs
    else
      digitalWrite(output->outputPin, HIGH);

    output->state = HIGH; // Save state after setting output high
    output->pulsing = false; // Output is not pulsing
    output->lastChange = currentMillis; // Log last change

    report(output);

    return SUCCESS;
  }
  else if(operation == OFF)
  {
    if(!output->enableOff && !internal) // Check if this output is allowed to turn off
      return DISABLED;

    if(output->state == LOW) // Check if the state already is set
      return UNCHANGED;

    if(output->outputActiveLow)
      digitalWrite(output->outputPin, HIGH); // Reverse for active low outputs
    else
      digitalWrite(output->outputPin, LOW);

    output->state = LOW; // Save state after setting output low
    output->pulsing = false; // Output is not pulsing
    output->lastChange = currentMillis; // Log last change

    report(output);

    return SUCCESS;
  }
  else if(operation == TOGGLE)
  {
    if(!output->enableToggle && !internal) // Check if this output is allowed to toggle
      return DISABLED;

    if(currentMillis - output->lastChange <= toggleDelay) // Check toggle delay before toggling
      return COOLDOWN;

    if(output->outputActiveLow)
      digitalWrite(output->outputPin, output->state); // Reverse for active low outputs
    else
      digitalWrite(output->outputPin, !output->state);

    output->state = !output->state; // Save state after toggling
    output->pulsing = false; // Output is not pulsing
    output->lastChange = currentMillis; // Log last change

    report(output);

    return SUCCESS;
  }
  else if(operation == PULSE)
  {
    if(!output->enablePulse && !internal) // Check if this output is allowed to pulse
      return DISABLED;

    if(output->pulsing) // Output is already pulsing, keep pulsing
      output->lastChange = currentMillis;

    if(output->state == HIGH) // Check if state is already high
      return UNCHANGED;

    // Turn on
    if(output->outputActiveLow)
      digitalWrite(output->outputPin, LOW);
    else
      digitalWrite(output->outputPin, HIGH);

    output->state = HIGH; // Save state while pulsing
    output->pulsing = true; // Output is pulsing
    output->lastChange = currentMillis; // Log last change

    // Output will be turned off in handlePulsing

    report(output);

    return SUCCESS;
  }
  else if(operation == LOCK)
  {
    output->locked = true;

    return SUCCESS;
  }
  else if(operation == UNLOCK)
  {
    if(!output->locked)
      return UNCHANGED;

    output->locked = false;

    return SUCCESS;
  }
  else
    return MISSING;
}

/*
 * MQTT
 */

void report(Output* output)
{
  if(enableMqtt && output->enableReport)
  {
    uint8_t state[1] = { output->state };
    mqttClient.publish(output->reportTopic, state, 1);
  }
}

void callback(char* topic, uint8_t* payload, unsigned int length)
{
  setStatus(WORKING2, true);

  for(int i = 0; i < outputCount; i++) // Loop through available outputs
  {
    if(strcmp(outputs[i].controlTopic, topic) == 0)
    {
      if(payload[0] == 0x00)
      {
        execute(&outputs[i], OFF);
      }
      else if(payload[0] == 0x01)
      {
        execute(&outputs[i], ON);
      }
      else if(payload[0] == 0x02)
      {
        execute(&outputs[i], TOGGLE);
      }
      else
      {
        Serial.println(F("Got unknown payload"));
      }

      break;
    }
  }
}

/*
 * Parse HTTP command
 */

Result executeHttp(char* path, uint8_t* state)
{
  if(strcmp(path, "/") == 0) // Check if root was requested
  {
    return ROOT;
  }

  for(int i = 0; i < outputCount; i++) // Loop through available outputs
  {
    if(startsWith(path, outputs[i].url)) // Check if output matches requested url
    {
      int commandLength = strlen(path) - strlen(outputs[i].url); // Calculate length of command

      char* command = substr(path, strlen(outputs[i].url)+1, commandLength);

      if(strcmp(command, statusCommand) == 0) // Check if status was requested
      {
        if(!outputs[i].enableStatus) // Check if this output allows showing its state
        {
          free(command);
          return DISABLED;
        }

        *state = outputs[i].state; // Get state to pass along to client if status is requested

        free(command);
        return STATUS;
      }
      else
      {
        Result result = execute(&outputs[i], parseCommand(command)); // If not status request, pass along to regular process function

        free(command);
        return result;
      }
    }
  }

  return MISSING;
}

/*
 * Connect functions
 */

void connectTelnet()
{
  if(telnetClient.connect(telnetServer, telnetPort))
  {
    Serial.println(F("Telnet connected"));

    delay(1000);

    Serial.println(F("Logging in..."));

    telnetClient.write(telnetUsername);
    telnetClient.write("\r\n");

    delay(2000);

    telnetClient.write(telnetPassword);
    telnetClient.write("\r\n");

    Serial.println(F("Logged into Telnet"));

    delay(2000);

    while(telnetClient.available())
    {
      telnetClient.read();
    }
  }
  else
  {
    Serial.println(F("Telnet connection failed"));
  }
}

void connectMqtt()
{
  if(mqttClient.connect(name)) // Connect or reconnect to server
  {
    Serial.println(F("Subscribing to MQTT topic(s)"));

    for(int i = 0; i < outputCount; i++) // Loop through outputs and subscribe to all topics
    {
        mqttClient.subscribe(outputs[i].controlTopic);
    }

    Serial.println(F("Connected to MQTT server successfully"));
  }
  else
  {
    Serial.println(F("Could not connect to MQTT server"));
  }
}

/*
 * Setup functions
 */

void setupLocal()
{
  Serial.println(F("Setting up local..."));

  if(enableStatus || enableRgbStatus)
  {
    if(enableRgbStatus)
    {
      pinMode(statusPins[0], OUTPUT);
      pinMode(statusPins[1], OUTPUT);
      pinMode(statusPins[2], OUTPUT);
    }
    else
    {
      for(int i = 0; i < statusPinsCount; i++)
      {
        pinMode(statusPins[i], OUTPUT);
      }
    }

    setStatus(INITIALIZING);
  }

  // Loop through outputs and initialize pin
  for(int i = 0; i < outputCount; i++)
  {
    Serial.print(F("Initializing output \""));
    Serial.print(outputs[i].name);
    Serial.println(F("\""));

    pinMode(outputs[i].outputPin, OUTPUT);

    if(outputs[i].outputActiveLow)
      digitalWrite(outputs[i].outputPin, !outputs[i].state);
    else
      digitalWrite(outputs[i].outputPin, outputs[i].state);

    if(enableInput && outputs[i].inputMode != 0)
    {
      Serial.println(F("Enabling input"));

      pinMode(outputs[i].inputPin, INPUT);
    }
  }

  Serial.println(F("Local setup complete"));
}

void setupNetwork()
{
  Serial.println(F("Setting up network..."));

  delay(1000);

  if(useDhcp)
    Ethernet.begin(mac); // Begin networking with DHCP
  else
    Ethernet.begin(mac, ip); // Begin networking with static IP

  if (Ethernet.hardwareStatus() == EthernetNoHardware)
  {
    Serial.println(F("No ethernet shield found"));

    while (true)
    {
      delay(1); // No ethernet shield, do nothing
    }
  }

  if (Ethernet.linkStatus() == LinkOFF)
  {
    Serial.println(F("No ethernet cable"));
  }

  delay(2000);

  if(useDhcp)
  {
    Serial.print(F("Received IP: "));
    Serial.println(Ethernet.localIP()); // Print IP if we received it via DHCP
  }

  Serial.println(F("Network setup complete"));
}

void setupHttp()
{
  Serial.println(F("Setting up HTTP..."));

  httpServer.begin(); // Start socket

  Serial.println(F("HTTP setup complete"));
}

void setupMqtt()
{
  Serial.println(F("Setting up MQTT..."));

  mqttClient.setServer(mqttServer, mqttPort); // Set MQTT server
  mqttClient.setCallback(callback); // Set MQTT callback function

  connectMqtt();

  Serial.println(F("MQTT setup complete"));
}

void setupTelnet()
{
  Serial.println(F("Setting up telnet..."));

  connectTelnet();

  Serial.println(F("Telnet setup complete"));
}

/*
 * Local handler functions
 */

void handleInput(Output* output)
{
  if(millis() - output->lastChange <= debounceDelay) // Debounce input
    return;

  uint8_t state = digitalRead(output->inputPin); // Get state of input

  if(output->inputActiveLow) // Reverse input if input is active low
    state = !state;

  // The mode of the input (0 = off, 1 = copy, 2 = copy high only, 3 = pulse, 4 = toggle, 5 = toggle (off at end), 6 = hold)
  if(output->inputMode == 1 || output->inputMode == 2) // Copy input state to output state
  {
    if(output->inputMode == 2 && state == LOW) // If state is not high and output is high only copy (2), don't do anything
      return;

    if(state == output->state) // If state already matches, don't do anything
      return;

    if(state)
      execute(output, ON, true);
    else
      execute(output, OFF, true);
  }
  else if(output->inputMode == 3) // Pulse output while button is held
  {
    if(state)
      execute(output, PULSE, true);
  }
  else if(output->inputMode == 4) // Toggle output while button is held
  {
    if(state)
      execute(output, TOGGLE, true);
  }
  else if(output->inputMode == 5) // Toggle output while button is held (but always turn off at the end)
  {
    if(state)
    {
      execute(output, TOGGLE, true);
      output->holding = true;
    }
    else if(!state && output->holding)
    {
      execute(output, OFF, true);
      output->holding = false;
    }
  }
  else if(output->inputMode == 6) // Hold output as long as input is held
  {
    if(state && !output->holding)
    {
      execute(output, ON, true);
      output->holding = true;
    }
    else if(!state && output->holding)
    {
      execute(output, OFF, true);
      output->holding = false;
    }
  }
}

void handlePulsing(Output* output)
{
  if(millis() - output->lastChange >= output->pulseDuration)
  {
    execute(output, OFF, true); // Turn output off if pulse duration is exceeded
  }
}

/*
 * Loop functions
 */

void loopLocal()
{
  for(int i = 0; i < outputCount; i++) // Loop through output pins
  {
    if(enableInput && outputs[i].inputMode != 0)
      handleInput(&outputs[i]);

    if(outputs[i].pulsing)
      handlePulsing(&outputs[i]);
  }
}

void loopNetwork()
{
  if(useDhcp)
      Ethernet.maintain(); // Maintain DHCP lease if DHCP is used
}

void loopHttp()
{
  EthernetClient client = httpServer.available(); // Get client if one is available

  if(client)
  {
    Serial.println(F("New HTTP client"));
    setStatus(WORKING1, true);

    char line[255];

    Result result = FAILED;
    uint8_t state;

    while (client.connected() && client.available()) // Loop until client disconnected or no data is left
    {
      readline(line, 255, client);

      if(strncmp(line, "\r\n", 2) == 0) // Empty line, request ended
      {
        if(result == SUCCESS)
        {
          client.println(F("HTTP/1.1 200 OK"));
          client.println(F("Content-Type: application/json"));
          client.println(F("Connection: close"));
          client.println();
          client.println(F("{\"status\": \"ok\"}"));
        }
        else if(result == STATUS)
        {
          client.println(F("HTTP/1.1 200 OK"));
          client.println(F("Content-Type: application/json"));
          client.println(F("Connection: close"));
          client.println();
          client.print(F("{\"status\": \"ok\", \"state\": "));
          client.print(state);
          client.println(F("}"));
        }
        else if(result == UNCHANGED)
        {
          client.println(F("HTTP/1.1 409 Conflict"));
          client.println(F("Content-Type: application/json"));
          client.println(F("Connection: close"));
          client.println();
          client.println(F("{\"status\": \"unchanged\"}"));
        }
        else if(result == ROOT)
        {
          client.println(F("HTTP/1.1 200 OK"));
          client.println(F("Content-Type: application/json"));
          client.println(F("Connection: close"));
          client.println();
          client.print(F("{\"status\": \"ok\", \"name\": \""));
          client.print(name);
          client.print(F("\", \"version\": \""));
          client.print(version);
          client.println(F("\"}"));
        }
        else if(result == MISSING)
        {
          client.println(F("HTTP/1.1 404 Not Found"));
          client.println(F("Content-Type: application/json"));
          client.println(F("Connection: close"));
          client.println();
          client.println(F("{\"status\": \"missing\"}"));
        }
        else if(result == DISABLED)
        {
          client.println(F("HTTP/1.1 403 Forbidden"));
          client.println(F("Content-Type: application/json"));
          client.println(F("Connection: close"));
          client.println();
          client.println(F("{\"status\": \"disabled\"}"));
        }
        else if(result == COOLDOWN)
        {
          client.println(F("HTTP/1.1 429 Too Many Requests"));
          client.println(F("Content-Type: application/json"));
          client.println(F("Connection: close"));
          client.println();
          client.println(F("{\"status\": \"cooldown\"}"));
        }
        else if(result == LOCKED)
        {
          client.println(F("HTTP/1.1 403 Forbidden"));
          client.println(F("Content-Type: application/json"));
          client.println(F("Connection: close"));
          client.println();
          client.println(F("{\"status\": \"locked\"}"));
        }
        else
        {
          client.println(F("HTTP/1.1 500 Internal Server Error"));
          client.println(F("Content-Type: application/json"));
          client.println(F("Connection: close"));
          client.println();
          client.println(F("{\"status\": \"error\"}"));
        }

        break;
      }
      else if(startsWith(line, "GET ")) // Check if this line specifies the path
      {
        int start = indexOf(line, ' ') + 1; // Get index of first space
        int end = indexOfBack(line,' '); // Get index of second space

        char* request = substr(line, start, end - start); // Get the request substring

        result = executeHttp(request, &state); // Pass path along for further processing

        free(request);
      }
    }

    delay(1); // Wait for client to receive data
    client.stop(); // Disconnect from client
  }
}

void loopMqtt()
{
  if(mqttClient.connected())
  {
    mqttClient.loop(); // Just call the MQTT loop function if we're still connected
  }
  else
  {
    unsigned long currentMillis = millis();

    if(currentMillis - lastMqttReconnect >= mqttReconnectinterval)
    {
      Serial.println(F("Reconnecting to MQTT server"));
      setStatus(ERROR, true);

      connectMqtt(); // Lost connection, reconnect to server

      lastMqttReconnect = currentMillis;
    }
  }
}

void loopTelnet()
{
  if(telnetClient.connected())
  {
    unsigned long currentMillis = millis();

    if(telnetWaiting && currentMillis >= lastTelnetPoll + telnetResponseDelay)
    {
      setStatus(WORKING3);

      char line[255];

      while(telnetClient.connected() && telnetClient.available())
      {
        readline(line, 255, telnetClient, true);

        if(startsWith(line, "powerstate"))
        {
          int start = indexOf(line, '=') + 2;
          int end = indexOfBack(line, '(') - 1;
          char* powerstate = substr(line, start, end - start);

          Serial.print(F("Got powerstate: "));
          Serial.println(powerstate);

          if(strcmp(powerstate, "2") == 0 && outputs[0].state != HIGH)
          {
            execute(&outputs[0], ON, true);

            lastTelnetToggle = currentMillis;
          }
          else if(strcmp(powerstate, "2") != 0 && outputs[0].state != LOW)
          {
            if(currentMillis - lastTelnetToggle >= telnetTimeout)
            {
              execute(&outputs[0], OFF, true);

              lastTelnetToggle = currentMillis;
            }
            else
            {
              Serial.println("Telnet timeout active");
            }
          }

          free(powerstate);
        }
      }

      lastTelnetPoll = currentMillis;
      telnetWaiting = false;

      setStatus(OK, true);
    }
    else if(currentMillis - lastTelnetPoll >= telnetInterval)
    {
      Serial.println(F("Checking iDRAC"));
      setStatus(WORKING3);

      telnetClient.write(telnetCommand);
      telnetClient.write("\r\n");

      lastTelnetPoll = currentMillis;
      telnetWaiting = true;
    }
  }
  else
  {
    unsigned long currentMillis = millis();

    if(currentMillis - lastTelnetReconnect >= telnetReconnectInterval)
    {
      Serial.println(F("Reconnecting to telnet"));
      setStatus(ERROR, true);

      telnetClient.stop();
      connectTelnet();

      lastTelnetReconnect = currentMillis;
    }
  }
}

void loopStatus()
{
  if(statusFalloff && millis() - lastStatusChange >= statusFalloffDelay)
  {
    setStatus(NONE); // Turn LED off after falloff period
  }
}

/*
 * Main setup
 */

void setup()
{
  Serial.begin(9600);

  Serial.println(F("Initializing..."));

  outputCount = sizeof(outputs) / sizeof(outputs[0]); // Get available outputs
  statusPinsCount = sizeof(statusPins) / sizeof(statusPins[0]); // Count available status pins

  setupLocal(); // Setup pins

  if(enableNetwork)
    setupNetwork(); // Setup networking when either HTTP or MQTT is enabled

  if(enableHttp)
    setupHttp(); // Setup HTTP server if enabled

  if(enableMqtt)
    setupMqtt(); // Setup MQTT client if enabled

  if(enableTelnet)
    setupTelnet(); // Setup Telnet client if enabled

  Serial.println(F("Initialization complete"));
  setStatus(NONE);
}

/*
 * Main loop
 */

void loop()
{
  loopLocal(); // Run local loop before network-related services

  if(enableNetwork)
  {
    loopNetwork(); // Loop network (DHCP)

    if(enableHttp)
      loopHttp(); // Loop HTTP (accept clients)

    if(enableMqtt)
      loopMqtt(); // Loop HTTP (handle subscriptions)

    if(enableTelnet)
      loopTelnet(); // Loop Telnet (request powerstate)

    if(enableStatus || enableRgbStatus)
      loopStatus(); // Loop status (handle LED falloff)
  }
}

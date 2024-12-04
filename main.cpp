/*
 * Copyright (c) 2020, CATIE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "mbed.h"
#include <nsapi_dns.h>
#include <MQTTClientMbedOs.h>
#include "key.h"

namespace {
#define GROUP_NAME            "ABCD"
#define MQTT_TOPIC_TEMPERATURE      "RoiBrioche/feeds/temperature"
#define MQTT_TOPIC_SUBSCRIBE    "/estia/"GROUP_NAME"/downlink"
#define SYNC_INTERVAL           1
#define MQTT_CLIENT_ID          "6LoWPAN_Node_"GROUP_NAME
}

// Peripherals
static DigitalOut led(LED1);
static InterruptIn button(BUTTON1);

// Network
NetworkInterface *network;
MQTTClient *client;

// MQTT
const char* hostname = "io.adafruit.com";
int port = 1883;

// Error code
nsapi_size_or_error_t rc = 0;

// Event queue
static int id_yield;
static EventQueue main_queue(32 * EVENTS_EVENT_SIZE);

/*!
 *  \brief Called when a message is received
 *
 *  Print messages received on mqtt topic
 */
void messageArrived(MQTT::MessageData& md)
{
    MQTT::Message &message = md.message;
    printf("Message arrived: qos %d, retained %d, dup %d, packetid %d\r\n", message.qos, message.retained, message.dup, message.id);
    printf("Payload %.*s\r\n", message.payloadlen, (char*)message.payload);

    // Get the payload string
    char* char_payload = (char*)malloc((message.payloadlen+1)*sizeof(char)); // allocate the necessary size for our buffer
    char_payload = (char *) message.payload; // get the arrived payload in our buffer
    char_payload[message.payloadlen] = '\0'; // String must be null terminated

    // Compare our payload with known command strings
    if (strcmp(char_payload, "ON") == 0) {
        led = 1;
    }
    else if (strcmp(char_payload, "OFF") == 0) {
        led = 0;
    }
    else if (strcmp(char_payload, "RESET") == 0) {
        printf("RESETTING ...\n");
        system_reset();
    }
}

/*!
 *  \brief Yield to the MQTT client
 *
 *  On error, stop publishing and yielding
 */
static void yield(){
    // printf("Yield\n");
    
    rc = client->yield(100);

    if (rc != 0){
        printf("Yield error: %d\n", rc);
        main_queue.cancel(id_yield);
        main_queue.break_dispatch();
        system_reset();
    }
}

/*!
 *  \brief Publish data over a specified MQTT topic
 *
 *  \param topic The MQTT topic to publish to
 *  \param payload The message payload to send
 *  \return int8_t 0 if successful, error code otherwise
 */
static int8_t publish(const char* topic, const char* payload) {
    MQTT::Message message;
    message.qos = MQTT::QOS1;
    message.retained = false;
    message.dup = false;
    message.payload = (void*)payload;
    message.payloadlen = strlen(payload);

    printf("Publishing to topic: %s, message: %s\n", topic, payload);
    rc = client->publish(topic, message);
    if (rc != 0) {
        printf("Failed to publish to topic %s: %d\n", topic, rc);
        return rc;
    }
    return 0;
}

// main() runs in its own thread in the OS
int main() {
    printf("Connecting to border router...\n");

    /* Get Network configuration */
    network = NetworkInterface::get_default_instance();

    if (!network) {
        printf("Error! No network interface found.\n");
        return 0;
    }

    /* Add DNS */
    nsapi_addr_t new_dns = {
        NSAPI_IPv6,
        { 0xfd, 0x9f, 0x59, 0x0a, 0xb1, 0x58, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x01 }
    };
    nsapi_dns_add_server(new_dns, "LOWPAN");

    /* Border Router connection */
    rc = network->connect();
    if (rc != 0) {
        printf("Error! net->connect() returned: %d\n", rc);
        return rc;
    }

    /* Print IP address */
    SocketAddress a;
    network->get_ip_address(&a);
    printf("IP address: %s\n", a.get_ip_address() ? a.get_ip_address() : "None");

    /* Open TCP Socket */
    TCPSocket socket;
    SocketAddress address;
    network->gethostbyname(hostname, &address);
    address.set_port(port);

    /* MQTT Connection */
    client = new MQTTClient(&socket);
    socket.open(network);
    rc = socket.connect(address);
    if (rc != 0) {
        printf("Connection to MQTT broker Failed\n");
        return rc;
    }

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 4;
    data.keepAliveInterval = 25;
    data.username.cstring = "RoiBrioche";
    data.password.cstring = ADAFRUITKEY;

    if (client->connect(data) != 0) {
        printf("Connection to MQTT Broker Failed\n");
    }

    printf("Connected to MQTT broker\n");

    /* Yield every 1 second */
    id_yield = main_queue.call_every(SYNC_INTERVAL * 1000, yield);

    // Example of publishing
    const char* example_topic = MQTT_TOPIC_TEMPERATURE; // Example topic
    const char* example_payload = "25"; // Example payload
    button.fall(main_queue.event(callback(publish, example_topic, example_payload)));

    main_queue.dispatch_forever();
}
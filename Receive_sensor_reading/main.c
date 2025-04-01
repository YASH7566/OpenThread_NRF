#include <zephyr/kernel.h>
#include <openthread/thread.h>
#include <openthread/udp.h>

#define UDP_PORT 2222
#define MULTICAST_ADDRESS "ff03::1" // Multicast address

static otInstance *myInstance;
static otUdpSocket mySocket;

// UDP receive callback function
static void udp_receive(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo) {
    char buf[64];
    int length = otMessageRead(aMessage, 0, buf, sizeof(buf) - 1);
    
    if (length > 0) {
        buf[length] = '\0';  // Null-terminate the received string
        printk("Received message: %s\n", buf);  // Print the received message
    } else {
        printk("Failed to read message\n");
    }
}

// Initialize UDP and bind to the specified port
static void udp_init(void) {
    myInstance = openthread_get_default_instance();
    memset(&mySocket, 0, sizeof(mySocket));

    // Open the UDP socket with the receive callback function
    if (otUdpOpen(myInstance, &mySocket, udp_receive, NULL) != OT_ERROR_NONE) {
        printk("Failed to open UDP socket\n");
        return;
    }

    // Setup socket address for binding
    otSockAddr sockaddr;
    memset(&sockaddr, 0, sizeof(sockaddr));
    sockaddr.mPort = UDP_PORT;  // Bind to UDP port

    // Bind the socket to the port
    if (otUdpBind(myInstance, &mySocket, &sockaddr, OT_NETIF_THREAD) != OT_ERROR_NONE) {
        printk("Failed to bind UDP socket\n");
        return;
    }

    // Join the multicast group
    otIp6Address multicastAddr;
    if (otIp6AddressFromString(MULTICAST_ADDRESS, &multicastAddr) != OT_ERROR_NONE) {
        printk("Failed to convert multicast address\n");
        return;
    }
    
    if (otIp6SubscribeMulticastAddress(myInstance, &multicastAddr) != OT_ERROR_NONE) {
        printk("Failed to join multicast group\n");
        return;
    }

    printk("Receiver initialized and listening for messages...\n");
}

// Main function
void main(void) {
    udp_init();

    while (1) {
        k_msleep(1000);  // Keep the receiver alive and ready to receive messages
    }
}

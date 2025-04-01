#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <openthread/thread.h>
#include <openthread/udp.h>

#define BH1750_ADDR 0x23
#define UDP_PORT 2222
#define RECEIVER_IP "ff03::1"  // Multicast address

/* I2C device */
static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));

static otInstance *myInstance;
static otUdpSocket mySocket;

void bh1750_init(void) {
    uint8_t cmd = 0x10; // Continuous High-Resolution Mode
    if (i2c_write(i2c_dev, &cmd, sizeof(cmd), BH1750_ADDR)) {
        printk("Failed to initialize BH1750\n");
    }
}

uint16_t bh1750_read_light(void) {
    uint8_t data[2] = {0};
    if (i2c_read(i2c_dev, data, sizeof(data), BH1750_ADDR)) {
        printk("Failed to read from BH1750\n");
        return 0;
    }
    return ((data[0] << 8) | data[1]) / 1.2; // Convert to lux
}

static void udp_send_sensor_data(void) {
    otMessageInfo messageInfo;
    otMessage *message;
    char buf[32];
    
    uint16_t lux = bh1750_read_light();
    snprintf(buf, sizeof(buf), "Light: %d lux", lux);
    
    message = otUdpNewMessage(myInstance, NULL);
    if (message == NULL) {
        printk("Failed to allocate UDP message\n");
        return;
    }

    if (otMessageAppend(message, buf, strlen(buf)) != OT_ERROR_NONE) {
        printk("Failed to append message\n");
        otMessageFree(message);
        return;
    }

    memset(&messageInfo, 0, sizeof(messageInfo));
    otIp6AddressFromString(RECEIVER_IP, &messageInfo.mPeerAddr);
    messageInfo.mPeerPort = UDP_PORT;

    if (otUdpSend(myInstance, &mySocket, message, &messageInfo) != OT_ERROR_NONE) {
        printk("Failed to send UDP message\n");
        otMessageFree(message);
    } else {
        printk("Sent: %s\n", buf);
    }
}

static void udp_init(void) {
    myInstance = openthread_get_default_instance();
    memset(&mySocket, 0, sizeof(mySocket));

    if (otUdpOpen(myInstance, &mySocket, NULL, NULL) != OT_ERROR_NONE) {
        printk("Failed to open UDP socket\n");
        return;
    }
    printk("Sender initialized\n");
}

void main(void) {
    if (!device_is_ready(i2c_dev)) {
        printk("I2C device not ready\n");
        return;
    }

    bh1750_init();
    printk("BH1750 Light Sensor Initialized\n");
    
    udp_init();

    while (1) {
        udp_send_sensor_data();
        k_sleep(K_SECONDS(2));
    }
}

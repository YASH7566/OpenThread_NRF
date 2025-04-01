#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <openthread/thread.h>
#include <openthread/udp.h>

#define BH1750_ADDR 0x23
#define UDP_PORT 2222
#define RECEIVER_IP "ff03::1"  // Multicast address
#define BUTTON1_NODE DT_NODELABEL(button1)

/* I2C device */
static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static const struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET(BUTTON1_NODE, gpios);
static struct gpio_callback button1_cb;

/* OpenThread */
static otInstance *myInstance;
static otUdpSocket mySocket;
static bool rapid_send_mode = false;
static bool wakeup_flag = false;

/* Condition Variable & Mutex */
static struct k_mutex wakeup_mutex;
static struct k_condvar wakeup_cond;

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

/* Button Interrupt: Wake Up the System */
void button1_pressed_callback(const struct device *gpiob, struct gpio_callback *cb, gpio_port_pins_t pins) {
    k_mutex_lock(&wakeup_mutex, K_FOREVER);
    wakeup_flag = true;
    k_condvar_signal(&wakeup_cond);  // Wake up sleeping thread
    k_mutex_unlock(&wakeup_mutex);

    printk("Button pressed - waking up immediately\n");
}

void main(void) {
    if (!device_is_ready(i2c_dev)) {
        printk("I2C device not ready\n");
        return;
    }

    // Initialize button
    if (!device_is_ready(button1.port)) {
        printk("Button device not ready\n");
        return;
    }
    
    gpio_pin_configure_dt(&button1, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&button1, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&button1_cb, button1_pressed_callback, BIT(button1.pin));
    gpio_add_callback(button1.port, &button1_cb);

    // Initialize BH1750 sensor
    bh1750_init();
    printk("BH1750 Light Sensor Initialized\n");

    // Initialize UDP
    udp_init();

    // Initialize mutex & condition variable
    k_mutex_init(&wakeup_mutex);
    k_condvar_init(&wakeup_cond);

    while (1) {
        // Lock the mutex before waiting
        k_mutex_lock(&wakeup_mutex, K_FOREVER);
        wakeup_flag = false;

        // Wait for button press or timeout
        int err = k_condvar_wait(&wakeup_cond, &wakeup_mutex, K_SECONDS(30));

        k_mutex_unlock(&wakeup_mutex);

        if (err == 0) {
            // Condition was signaled (button pressed)
            printk("Woke up due to button press!\n");
            rapid_send_mode = true;
        } else {
            // Timeout occurred (normal mode)
            printk("Woke up due to timeout\n");
        }

        if (rapid_send_mode) {
            // Rapid send mode - send every 500ms for 5 seconds (10 messages)
            for (int i = 0; i < 10; i++) {
                udp_send_sensor_data();
                k_sleep(K_MSEC(500));
            }
            rapid_send_mode = false;
            printk("Exited rapid send mode\n");
        } else {
            // Normal mode - send every 30 seconds
            udp_send_sensor_data();
        }
    }
}

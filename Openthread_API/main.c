#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <openthread/thread.h>
#include <openthread/udp.h>

#define SLEEP_TIME_MS   1000
#define BUTTON0_NODE DT_NODELABEL(button0)
#define UDP_PORT 2222

static otUdpSocket mySocket;
static otInstance *myInstance;

static void udp_receive(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    char buf[64];
    int length = otMessageRead(aMessage, 0, buf, sizeof(buf) - 1);
    if (length > 0) {
        buf[length] = '\0';
        printk("Received: %s\n", buf);
    }
}

static void udp_init(void)
{
    otError error;
    myInstance = openthread_get_default_instance();
    memset(&mySocket, 0, sizeof(mySocket));

    error = otUdpOpen(myInstance, &mySocket, udp_receive, NULL);
    if (error != OT_ERROR_NONE) {
        printk("Failed to open UDP socket: %d\n", error);
        return;
    }

    otSockAddr sockaddr;
    memset(&sockaddr, 0, sizeof(sockaddr));
    sockaddr.mPort = UDP_PORT;

    error = otUdpBind(myInstance, &mySocket, &sockaddr, OT_NETIF_THREAD);
    if (error != OT_ERROR_NONE) {
        printk("Failed to bind UDP socket: %d\n", error);
        return;
    }
}

static void udp_send(void)
{
    otError error;
    const char *buf = "Hello Thread xd";
    otMessage *message;
    otMessageInfo messageInfo;

    memset(&messageInfo, 0, sizeof(messageInfo));
    otIp6AddressFromString("ff03::1", &messageInfo.mPeerAddr);
    messageInfo.mPeerPort = UDP_PORT;

    message = otUdpNewMessage(myInstance, NULL);
    if (!message) {
        printk("Failed to allocate message.\n");
        return;
    }

    error = otMessageAppend(message, buf, strlen(buf));
    if (error != OT_ERROR_NONE) {
        printk("Failed to append message: %d\n", error);
        return;
    }

    error = otUdpSend(myInstance, &mySocket, message, &messageInfo);
    if (error != OT_ERROR_NONE) {
        printk("UDP send error: %d\n", error);
        return;
    }

    printk("Sent: %s\n", buf);
}

void button0_pressed_callback(const struct device *gpiob, struct gpio_callback *cb, gpio_port_pins_t pins)
{
    udp_send();
}

int main(void)
{
    static const struct gpio_dt_spec button0 = GPIO_DT_SPEC_GET(BUTTON0_NODE, gpios);
    if (!device_is_ready(button0.port)) {
        printk("Port isn't ready\n");
        return;
    }

    gpio_pin_configure_dt(&button0, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&button0, GPIO_INT_EDGE_TO_INACTIVE);

    static struct gpio_callback button0_cb;
    gpio_init_callback(&button0_cb, button0_pressed_callback, BIT(button0.pin));
    gpio_add_callback(button0.port, &button0_cb);

    udp_init();

    while (1) {
        k_msleep(SLEEP_TIME_MS);
    }
    return 0;
}

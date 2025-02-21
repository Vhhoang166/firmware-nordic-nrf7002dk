/* The Clear BSD License
 *
 * Copyright (c) 2025 EdgeImpulse Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the disclaimer
 * below) provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 *   * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
 * THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* Include ----------------------------------------------------------------- */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include "ei_device_nordic_nrf7002dk.h"
#include "flash_memory.h"
#include "ei_at_handlers.h"
#include "edge-impulse-sdk/porting/ei_classifier_porting.h"
#include "edge-impulse-sdk/dsp/ei_utils.h"
#include "firmware-sdk/ei_device_memory.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ei_device_NRF7002DK);

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led1)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

using namespace std;

static void led_timer_handler(struct k_timer *dummy);
static void led_work_handler(struct k_work *work);
static void sampler_timer_handler(struct k_timer *dummy);
static void sampler_work_handler(struct k_work *work);

K_TIMER_DEFINE(led_timer, led_timer_handler, NULL);
K_WORK_DEFINE(led_work, led_work_handler);
K_TIMER_DEFINE(sampler_timer, sampler_timer_handler, NULL);
K_WORK_DEFINE(sampler_work, sampler_work_handler);

void set_max_data_output_baudrate_c(void);
void set_default_data_output_baudrate_c(void);

const struct device *uart;

static void led_work_handler(struct k_work *work)
{
    EiDeviceNRF7002DK *dev = static_cast<EiDeviceNRF7002DK*>(EiDeviceInfo::get_device());
    EiState state =dev->get_state();
    static uint8_t animation = 0;
    static uint8_t blink;

    switch(state)
    {
        case eiStateErasingFlash:
            gpio_pin_set_dt(&led, blink++ % 2);
            break;
        case eiStateSampling:
            gpio_pin_set_dt(&led, blink++ % 2);
            break;
        case eiStateUploading:
            gpio_pin_set_dt(&led, blink++ % 2);
            break;
        case eiStateFinished:
            blink = 0;
            if(animation == 0) {
                animation = 10;
            }
            break;
        default:
            break;
    }

    if(animation == 0) {
        return;
    }

    switch(animation) {
        case 10:
            gpio_pin_set_dt(&led, 0);
            break;
        case 9:
            gpio_pin_set_dt(&led, 1);
            break;
        case 8:
            gpio_pin_set_dt(&led, 0);
            break;
        case 7:
            gpio_pin_set_dt(&led, 1);
            break;
        case 6:
            gpio_pin_set_dt(&led, 0);
            break;
        case 5:
            gpio_pin_set_dt(&led, 1);
            break;
        case 4:
            gpio_pin_set_dt(&led, 0);
            break;
        case 3:
            gpio_pin_set_dt(&led, 1);
            break;
        case 2:
            gpio_pin_set_dt(&led, 0);
            break;
        case 1:
            dev->set_state(eiStateIdle);
            break;
    }
    animation--;
}

static void led_timer_handler(struct k_timer *dummy)
{
    k_work_submit(&led_work);
}

static void sampler_timer_handler(struct k_timer *dummy)
{
    k_work_submit(&sampler_work);
}

static void sampler_work_handler(struct k_work *work)
{
    EiDeviceNRF7002DK *dev = static_cast<EiDeviceNRF7002DK*>(EiDeviceInfo::get_device());

#if MULTI_FREQ_ENABLED == 1

    if (dev->get_fusioning() == 1) {
        dev->sample_read_callback();
    }
    else {
        uint8_t flag = 0;
        uint8_t i = 0;

        // update actual time
        dev->actual_timer += dev->get_sample_interval();

        for (i = 0; i < dev->get_fusioning(); i++){
            if (((uint32_t)(dev->actual_timer % (uint32_t)dev->multi_sample_interval.at(i))) == 0) {   /* check if period of sensor is a multiple of actual time*/
                flag |= (1<<i);                                                                     /* if so, time to sample it! */
            }
        }

        if (dev->sample_multi_read_callback != nullptr){
            dev->sample_multi_read_callback(flag);
        }

    }

#else
    dev->sample_read_callback();
#endif
}

EiDeviceInfo* EiDeviceInfo::get_device(void)
{
    static EiFlashMemory memory(sizeof(EiConfig));
    static EiDeviceNRF7002DK dev(&memory);

    return &dev;
}

EiDeviceNRF7002DK::EiDeviceNRF7002DK(EiDeviceMemory* mem)
{
    EiDeviceInfo::memory = mem;

    load_config();

    int ret = 0;

    if (!gpio_is_ready_dt(&led)) {
        ei_printf("Error: LED GPIO port DT_ALIAS(led1) not available\n");
        LOG_ERR("Cannot init LEDs (err: %d)", ret);
    }

    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        ei_printf("Error: LED GPIO port DT_ALIAS(led1) not available\n");
        LOG_ERR("Cannot configure LED (err: %d)", ret);
    }

    device_type = "NRF7002DK    ";
}

EiDeviceNRF7002DK::~EiDeviceNRF7002DK()
{

}

void EiDeviceNRF7002DK::init_device_id(void)
{
    bt_addr_le_t addr;
    size_t id_count = 1;
    char temp[18];

    bt_id_get(&addr, &id_count);

    snprintf(temp, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
        addr.a.val[5], addr.a.val[4], addr.a.val[3],
        addr.a.val[2], addr.a.val[1], addr.a.val[0]);

    LOG_INF("Setting ID = %s", temp);

    device_id = string(temp);
    mac_address = string(temp);
    save_config();
}

void EiDeviceNRF7002DK::clear_config(void)
{
    EiDeviceInfo::clear_config();

    init_device_id();
    save_config();
}

string EiDeviceNRF7002DK::get_mac_address(void)
{
    return mac_address;
}

bool EiDeviceNRF7002DK::start_sample_thread(void (*sample_read_cb)(void), float sample_interval_ms)
{
    this->sample_read_callback = sample_read_cb;
#if MULTI_FREQ_ENABLED == 1
    this->actual_timer = 0;
    this->fusioning = 1;
#endif

    k_timer_start(&sampler_timer, K_MSEC(sample_interval_ms), K_MSEC(sample_interval_ms));

    return true;
}

bool EiDeviceNRF7002DK::stop_sample_thread(void)
{
    k_timer_stop(&sampler_timer);

#if MULTI_FREQ_ENABLED == 1
    this->actual_timer = 0;
    this->fusioning = 0;
#endif

    return true;
}

#if MULTI_FREQ_ENABLED == 1
bool EiDeviceNRF7002DK::start_multi_sample_thread(void (*sample_multi_read_cb)(uint8_t), float* multi_sample_interval_ms, uint8_t num_fusioned)
{
    uint8_t i;
    uint8_t flag = 0;

    this->sample_multi_read_callback = sample_multi_read_cb;
    this->fusioning = num_fusioned;

    this->multi_sample_interval.clear();

    for (i = 0; i < num_fusioned; i++){
        this->multi_sample_interval.push_back(1.f/multi_sample_interval_ms[i]*1000.f);
    }

    this->sample_interval = ei_fusion_calc_multi_gcd(this->multi_sample_interval.data(), this->fusioning);

    /* force first reading */
    for (i = 0; i < this->fusioning; i++){
            flag |= (1<<i);
    }
    this->sample_multi_read_callback(flag);

    this->actual_timer = 0;

    k_timer_start(&sampler_timer, K_MSEC(this->sample_interval), K_MSEC(this->sample_interval));

    return true;
}
#endif

void EiDeviceNRF7002DK::set_state(EiState state)
{
    this->state = state;

    gpio_pin_set_dt(&led, 0);


    switch(state) {
        case eiStateErasingFlash:
        case eiStateSampling:
        case eiStateUploading:
        case eiStateFinished:
            k_timer_start(&led_timer, K_MSEC(250), K_MSEC(250));
            break;
        case eiStateIdle:
        default:
            k_timer_stop(&led_timer);
            break;
    }
}

EiState EiDeviceNRF7002DK::get_state(void)
{
    return this->state;
}

void EiDeviceNRF7002DK::set_serial_channel(serial_channel_t chan)
{
    last_channel = chan;
}

serial_channel_t EiDeviceNRF7002DK::get_serial_channel(void)
{
    return last_channel;
}

/**
 * @brief      Set output baudrate to max
 *
 */
void EiDeviceNRF7002DK::set_max_data_output_baudrate()
{
    set_max_data_output_baudrate_c();
}

/**
 * @brief      Set output baudrate to default
 *
 */
void EiDeviceNRF7002DK::set_default_data_output_baudrate()
{
    set_default_data_output_baudrate_c();
}

bool EiDeviceNRF7002DK::get_sensor_list(const ei_device_sensor_t **sensor_list, size_t *sensor_list_size)
{
    // no standalone sensors
    *sensor_list = nullptr;
    *sensor_list_size = 0;
    return true;
}

uint32_t EiDeviceNRF7002DK::get_data_output_baudrate(void)
{
    return MAX_BAUD;
}


int EiDeviceNRF7002DK::set_wifi_config(const char *ssid, const char *password, const int security)
{
    LOG_INF("Setting WiFi config");
    wifi_ssid = (std::string)ssid;
    wifi_password = (std::string)password;
    wifi_security = (EiWiFiSecurity)security;

    LOG_INF("wifi: save_config");
    save_config();

    return 0;
}
int EiDeviceNRF7002DK::get_wifi_config(char *ssid, char *password, int *security)
{
    LOG_INF("Getting WiFi config");
    load_config();

    LOG_INF("config loded");
    LOG_INF("wifi_ssdi: %s", wifi_ssid.c_str());
    LOG_INF("wifi_password: %s", wifi_password.c_str());
    LOG_INF("wifi_security: %d", wifi_security);
    strcpy(ssid, wifi_ssid.c_str());
    strcpy(password, wifi_password.c_str());
    *security = wifi_security;

    return 0;
}

/**
 * @brief      Init development kit UART
 *
 * @return     0 If successful
 * @return     -ENXIO If not successful
 *
 */
int uart_init(void)
{
    int err = 0;

    uart = DEVICE_DT_GET(DT_NODELABEL(uart0));
    if (!uart) {
        return -ENXIO;
    }

    return err;
}

/**
 * @brief      Get char from UART
 *
 * @return     rcv_char If successful
 * @return     0xFF If not successful
 *
 */
char uart_getchar(void)
{
    unsigned char rcv_char;

    if (uart_poll_in(uart, &rcv_char) == 0) {
        return rcv_char;
    }
    else{
        return 0xFF;
    }
}

/**
 * @brief      Get char from UART
 *
 * @param[in] send_char Character to be sent over UART
 *
 */
void ei_putchar(char c)
{
    uart_poll_out(uart, c);
}

void set_max_data_output_baudrate_c(void)
{
    struct uart_config cfg;

    if(uart_config_get(uart, &cfg)) {
        LOG_ERR("ERR: can't get UART config!");
        ei_printf("ERR: can't get UART config!\n");
    }

    cfg.baudrate = MAX_BAUD;

    if(uart_configure(uart, &cfg)) {
        LOG_ERR("ERR: can't set UART config!");
        ei_printf("ERR: can't set UART config!\n");
    }
}

void set_default_data_output_baudrate_c(void)
{
    struct uart_config cfg;

    if (uart_config_get(uart, &cfg)) {
        LOG_ERR("ERR: can't get UART config!");
        ei_printf("ERR: can't get UART config!\n");
    }

    cfg.baudrate = DEFAULT_BAUD;

    if (uart_configure(uart, &cfg)) {
        LOG_ERR("ERR: can't set UART config!");
        ei_printf("ERR: can't set UART config!\n");
    }
}
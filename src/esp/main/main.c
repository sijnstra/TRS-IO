
#include "driver/gpio.h"

static uint8_t data = 0;
static uint8_t switched_to_out = 0;

#define GPIO_OUTPUT_DISABLE(gpio_num) GPIO.enable_w1tc = 1 << (gpio_num)

#define GPIO_OUTPUT_ENABLE(gpio_num) GPIO.enable_w1ts = 1 << (gpio_num)



static void gpio_setup()
{
    gpio_config_t gpioConfig;

    // GPIO pins 12-19 (8 pins) are used for data bus
    gpioConfig.pin_bit_mask = GPIO_SEL_12 | GPIO_SEL_13 | GPIO_SEL_14 | GPIO_SEL_15 | GPIO_SEL_16 |
                              GPIO_SEL_17 | GPIO_SEL_18 | GPIO_SEL_19;
    gpioConfig.mode = GPIO_MODE_INPUT;
    gpioConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    gpioConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpioConfig.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&gpioConfig);

    // Configure RD_N
    gpioConfig.pin_bit_mask = GPIO_SEL_36;
    gpio_config(&gpioConfig);
    
    // Configure ESP_SEL_N
    gpioConfig.pin_bit_mask = GPIO_SEL_23;
    gpio_config(&gpioConfig);
    
    // Configure IOBUSINT_N & ESP_WAIT_N
    gpioConfig.pin_bit_mask = GPIO_SEL_25 | GPIO_SEL_27;
    gpioConfig.mode = GPIO_MODE_OUTPUT;
    gpioConfig.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&gpioConfig);
    
    // Set IOBUSINT_N to 0
    gpio_set_level(GPIO_NUM_25, 0);
    
    // Set ESP_WAIT_N to 0
    gpio_set_level(GPIO_NUM_27, 0);
    
    // Configure push button
    gpioConfig.pin_bit_mask = GPIO_SEL_22;
    gpioConfig.mode = GPIO_MODE_INPUT;
    gpioConfig.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&gpioConfig);
}



static void io_cycle()
{
    while (GPIO.in & (1 << GPIO_NUM_23)) ;

    if (GPIO.in1.data & (1 << (GPIO_NUM_36 - 32))) {
        // Read data
        data = GPIO.in >> 12;
    } else {
        GPIO_OUTPUT_ENABLE(GPIO_NUM_12);
        GPIO_OUTPUT_ENABLE(GPIO_NUM_13);
        GPIO_OUTPUT_ENABLE(GPIO_NUM_14);
        GPIO_OUTPUT_ENABLE(GPIO_NUM_15);
        GPIO_OUTPUT_ENABLE(GPIO_NUM_16);
        GPIO_OUTPUT_ENABLE(GPIO_NUM_17);
        GPIO_OUTPUT_ENABLE(GPIO_NUM_18);
        GPIO_OUTPUT_ENABLE(GPIO_NUM_19);
        switched_to_out = 1;
        // Write to bus
        uint32_t d = data << 12;
        REG_WRITE(GPIO_OUT_W1TS_REG, d);
        d = d ^ 0b11111111000000000000;
        REG_WRITE(GPIO_OUT_W1TC_REG, d);
        data++;
    }

    // Release ESP_WAIT_N
    GPIO.out_w1ts = (1 << GPIO_NUM_27);

    // Wait for ESP_SEL_N to be de-asserted
    while (!(GPIO.in & (1 << GPIO_NUM_23))) ;

    // Set SEL_WAIT_N to 0 for next IO command
    GPIO.out_w1tc = (1 << GPIO_NUM_27);

    if (switched_to_out) {
        GPIO_OUTPUT_DISABLE(GPIO_NUM_12);
        GPIO_OUTPUT_DISABLE(GPIO_NUM_13);
        GPIO_OUTPUT_DISABLE(GPIO_NUM_14);
        GPIO_OUTPUT_DISABLE(GPIO_NUM_15);
        GPIO_OUTPUT_DISABLE(GPIO_NUM_16);
        GPIO_OUTPUT_DISABLE(GPIO_NUM_17);
        GPIO_OUTPUT_DISABLE(GPIO_NUM_18);
        GPIO_OUTPUT_DISABLE(GPIO_NUM_19);
        switched_to_out = 0;
    }
}


void app_main(void)
{
    gpio_setup();
    while (true) {
        //gpio_set_level(GPIO_NUM_25, gpio_get_level(GPIO_NUM_22) ? 0 : 1);
        io_cycle();
    }
}


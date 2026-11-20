#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/uart.h"

enum {
    UART_TX = 0,
    UART_RX = 1,
    UART_BAUD = 115200,
    GPIO_COUNT = 30,
    GPIO_FIRST = 2,
    GPIO_LAST = 22,
    GPIO_ADC_FIRST = 26,
    GPIO_ADC_LAST = 28,
    PWM_SLICE_COUNT = 8,
    PWM_DIVIDER = 4,
    PWM_HZ = 1000,
    DUTY_MAX = 1000,
    LINE_CAPACITY = 64,
    MAX_TOKENS = 4
};

enum pin_mode { INPUT, PULLUP, OUTPUT, PWM };

static enum pin_mode modes[GPIO_COUNT];
static uint32_t pwm_pins[PWM_SLICE_COUNT];
static uint16_t pwm_period;

static bool pin_available(unsigned pin)
{
    return (pin >= GPIO_FIRST && pin <= GPIO_LAST) ||
           (pin >= GPIO_ADC_FIRST && pin <= GPIO_ADC_LAST);
}

static bool parse_uint(const char *token, unsigned max, unsigned *value)
{
    unsigned number = 0;
    for (; *token != '\0'; ++token) {
        if (*token < '0' || *token > '9') {
            return false;
        }
        number = number * 10 + (unsigned)(*token - '0');
        if (number > max) {
            return false;
        }
    }
    *value = number;
    return true;
}

static const char *set_mode(unsigned pin, const char *name)
{
    enum pin_mode mode;
    if (strcmp(name, "IN") == 0) {
        mode = INPUT;
    } else if (strcmp(name, "PULLUP") == 0) {
        mode = PULLUP;
    } else if (strcmp(name, "OUT") == 0) {
        mode = OUTPUT;
    } else if (strcmp(name, "PWM") == 0) {
        mode = PWM;
    } else {
        return "ERR MODE\n";
    }

    if (modes[pin] == mode) {
        return "OK\n";
    }

    unsigned slice = pwm_gpio_to_slice_num(pin);
    uint32_t pin_bit = UINT32_C(1) << pin;
    if (mode == PWM) {
        // GPIOs 16 apart map to the same slice and channel on RP2040.
        uint32_t alias = UINT32_C(1) << (pin ^ 16u);
        if (pwm_pins[slice] & alias) {
            return "ERR PWM_CHANNEL\n";
        }
        pwm_set_gpio_level(pin, 0);
        gpio_disable_pulls(pin);
        gpio_set_function(pin, GPIO_FUNC_PWM);
        pwm_pins[slice] |= pin_bit;
    } else {
        // Prepare SIO direction and initial output before selecting it.
        gpio_put(pin, 0);
        gpio_set_dir(pin, mode == OUTPUT ? GPIO_OUT : GPIO_IN);
        gpio_set_pulls(pin, mode == PULLUP, false);
        gpio_set_function(pin, GPIO_FUNC_SIO);
        if (modes[pin] == PWM) {
            pwm_set_gpio_level(pin, 0);
            pwm_pins[slice] &= ~pin_bit;
        }
    }

    modes[pin] = mode;
    return "OK\n";
}

static const char *handle_command(char *line)
{
    char *tokens[MAX_TOKENS];
    size_t count = 0;
    for (char *token = strtok(line, " \t"); token != NULL;
         token = strtok(NULL, " \t")) {
        if (count == MAX_TOKENS) {
            return "ERR SYNTAX\n";
        }
        tokens[count++] = token;
    }
    if (count == 0) {
        return NULL;
    }

    const char *command = tokens[0];
    if (strcmp(command, "PING") == 0) {
        return count == 1 ? "OK PONG\n" : "ERR SYNTAX\n";
    }

    bool is_mode = strcmp(command, "MODE") == 0;
    bool is_read = strcmp(command, "READ") == 0;
    bool is_write = strcmp(command, "WRITE") == 0;
    bool is_pwm = strcmp(command, "PWM") == 0;

    if (!is_mode && !is_read && !is_write && !is_pwm) {
        return "ERR COMMAND\n";
    }
    if (count != (is_read ? 2u : 3u)) {
        return "ERR SYNTAX\n";
    }

    unsigned pin;
    if (!parse_uint(tokens[1], GPIO_COUNT - 1, &pin) || !pin_available(pin)) {
        return "ERR PIN\n";
    }

    if (is_mode) {
        return set_mode(pin, tokens[2]);
    }
    if (is_read) {
        return gpio_get(pin) ? "OK 1\n" : "OK 0\n";
    }

    unsigned value;
    if (!parse_uint(tokens[2], is_pwm ? DUTY_MAX : 1, &value)) {
        return "ERR VALUE\n";
    }
    if (is_write && modes[pin] == OUTPUT) {
        gpio_put(pin, value != 0);
    } else if (is_pwm && modes[pin] == PWM) {
        uint16_t level = (uint16_t)((pwm_period * value + DUTY_MAX / 2) / DUTY_MAX);
        pwm_set_gpio_level(pin, level);
    } else {
        return "ERR MODE\n";
    }
    return "OK\n";
}

int main(void)
{
    for (unsigned pin = 0; pin < GPIO_COUNT; ++pin) {
        if (pin_available(pin)) {
            gpio_init(pin);
            gpio_disable_pulls(pin);
        }
    }

    uint32_t clock_hz = clock_get_hz(clk_sys);
    unsigned divisor = PWM_DIVIDER * PWM_HZ;
    pwm_period = (uint16_t)((clock_hz + divisor / 2) / divisor);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&config, PWM_DIVIDER);
    pwm_config_set_wrap(&config, pwm_period - 1);
    for (unsigned slice = 0; slice < PWM_SLICE_COUNT; ++slice) {
        pwm_init(slice, &config, true);
    }

    uart_init(uart0, UART_BAUD);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(uart0, false, false);
    gpio_set_function(UART_TX, GPIO_FUNC_UART);
    gpio_set_function(UART_RX, GPIO_FUNC_UART);

    char line[LINE_CAPACITY];
    size_t length = 0;
    const char *line_error = NULL;

    for (;;) {
        unsigned char byte = (unsigned char)uart_getc(uart0);
        if (byte == '\r' || byte == '\n') {
            line[length] = '\0';
            const char *response = line_error ? line_error : handle_command(line);
            if (response != NULL) {
                uart_write_blocking(uart0, (const uint8_t *)response, strlen(response));
            }
            length = 0;
            line_error = NULL;
        } else if (line_error == NULL) {
            if (byte != '\t' && (byte < ' ' || byte > '~')) {
                line_error = "ERR SYNTAX\n";
            } else if (length == sizeof(line) - 1) {
                line_error = "ERR LINE_TOO_LONG\n";
            } else {
                line[length++] = (char)byte;
            }
        }
    }
}

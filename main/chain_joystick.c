#include "chain_joystick.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "chain_joystick";

#define PACK_HEAD_HIGH 0xAA
#define PACK_HEAD_LOW 0x55
#define PACK_END_HIGH 0x55
#define PACK_END_LOW 0xAA

#define CHAIN_CMD_SET_RGB_VALUE 0x20
#define CHAIN_CMD_SET_RGB_LIGHT 0x22
#define CHAIN_CMD_GET_DEVICE_TYPE 0xFB
#define CHAIN_CMD_GET_16ADC 0x30

#define CHAIN_DEVICE_TYPE_JOYSTICK 0x0004

static uint8_t chain_crc(const uint8_t *buffer, size_t size)
{
    uint8_t crc = 0;

    for (size_t i = 4; i < size - 3; ++i) {
        crc = (uint8_t)(crc + buffer[i]);
    }

    return crc;
}

static esp_err_t chain_send_command(const chain_joystick_t *ctx, uint8_t cmd, const uint8_t *payload, size_t payload_len)
{
    uint8_t packet[32] = {0};
    const uint16_t length = (uint16_t)(3 + payload_len);
    const size_t packet_len = payload_len + 9;

    if (packet_len > sizeof(packet)) {
        return ESP_ERR_INVALID_SIZE;
    }

    packet[0] = PACK_HEAD_HIGH;
    packet[1] = PACK_HEAD_LOW;
    packet[2] = (uint8_t)(length & 0xFF);
    packet[3] = (uint8_t)((length >> 8) & 0xFF);
    packet[4] = ctx->device_id;
    packet[5] = cmd;

    if (payload_len > 0 && payload != NULL) {
        memcpy(&packet[6], payload, payload_len);
    }

    packet[packet_len - 3] = chain_crc(packet, packet_len);
    packet[packet_len - 2] = PACK_END_HIGH;
    packet[packet_len - 1] = PACK_END_LOW;

    uart_flush_input(ctx->uart_port);
    const int written = uart_write_bytes(ctx->uart_port, packet, packet_len);
    if (written != (int)packet_len) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static bool chain_packet_valid(const uint8_t *packet, size_t packet_len, uint8_t expected_id, uint8_t expected_cmd)
{
    if (packet_len < 9) {
        return false;
    }

    if (packet[0] != PACK_HEAD_HIGH || packet[1] != PACK_HEAD_LOW) {
        return false;
    }

    if (packet[packet_len - 2] != PACK_END_HIGH || packet[packet_len - 1] != PACK_END_LOW) {
        return false;
    }

    const uint16_t length = (uint16_t)packet[2] | ((uint16_t)packet[3] << 8);
    if ((size_t)(length + 6) != packet_len) {
        return false;
    }

    if (packet[4] != expected_id || packet[5] != expected_cmd) {
        return false;
    }

    return packet[packet_len - 3] == chain_crc(packet, packet_len);
}

static esp_err_t chain_read_response(const chain_joystick_t *ctx, uint8_t expected_cmd, uint8_t *packet, size_t *packet_len)
{
    uint8_t header[4] = {0};
    int read = uart_read_bytes(ctx->uart_port, header, sizeof(header), pdMS_TO_TICKS(50));
    if (read != (int)sizeof(header)) {
        return ESP_ERR_TIMEOUT;
    }

    if (header[0] != PACK_HEAD_HIGH || header[1] != PACK_HEAD_LOW) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const uint16_t length = (uint16_t)header[2] | ((uint16_t)header[3] << 8);
    const size_t total_len = length + 6;

    if (total_len > *packet_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(packet, header, sizeof(header));

    read = uart_read_bytes(ctx->uart_port, packet + 4, total_len - 4, pdMS_TO_TICKS(50));
    if (read != (int)(total_len - 4)) {
        return ESP_ERR_TIMEOUT;
    }

    if (!chain_packet_valid(packet, total_len, ctx->device_id, expected_cmd)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *packet_len = total_len;
    return ESP_OK;
}

esp_err_t chain_joystick_init(chain_joystick_t *ctx, uart_port_t uart_port, int tx_gpio, int rx_gpio, uint8_t device_id)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(uart_port, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(uart_port, &config));
    ESP_ERROR_CHECK(uart_set_pin(uart_port, tx_gpio, rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ctx->uart_port = uart_port;
    ctx->device_id = device_id;
    ctx->present = false;

    return ESP_OK;
}

esp_err_t chain_joystick_probe(chain_joystick_t *ctx)
{
    uint8_t packet[16] = {0};
    size_t packet_len = sizeof(packet);
    esp_err_t err = chain_send_command(ctx, CHAIN_CMD_GET_DEVICE_TYPE, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }

    err = chain_read_response(ctx, CHAIN_CMD_GET_DEVICE_TYPE, packet, &packet_len);
    if (err != ESP_OK) {
        ctx->present = false;
        return err;
    }

    const uint16_t device_type = (uint16_t)packet[6] | ((uint16_t)packet[7] << 8);
    ctx->present = device_type == CHAIN_DEVICE_TYPE_JOYSTICK;
    if (!ctx->present) {
        ESP_LOGW(TAG, "Unexpected chain device type: 0x%04X", device_type);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Chain joystick detected at ID %u", ctx->device_id);
    return ESP_OK;
}

esp_err_t chain_joystick_read_raw(chain_joystick_t *ctx, chain_joystick_sample_t *sample)
{
    uint8_t packet[16] = {0};
    size_t packet_len = sizeof(packet);
    esp_err_t err;

    if (ctx == NULL || sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = chain_send_command(ctx, CHAIN_CMD_GET_16ADC, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }

    err = chain_read_response(ctx, CHAIN_CMD_GET_16ADC, packet, &packet_len);
    if (err != ESP_OK) {
        return err;
    }

    sample->x_raw = (uint16_t)packet[6] | ((uint16_t)packet[7] << 8);
    sample->y_raw = (uint16_t)packet[8] | ((uint16_t)packet[9] << 8);
    return ESP_OK;
}

esp_err_t chain_joystick_set_brightness(chain_joystick_t *ctx, uint8_t brightness)
{
    uint8_t payload[2] = {brightness, 0};
    uint8_t packet[16] = {0};
    size_t packet_len = sizeof(packet);
    esp_err_t err;

    if (brightness > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    err = chain_send_command(ctx, CHAIN_CMD_SET_RGB_LIGHT, payload, sizeof(payload));
    if (err != ESP_OK) {
        return err;
    }

    err = chain_read_response(ctx, CHAIN_CMD_SET_RGB_LIGHT, packet, &packet_len);
    if (err != ESP_OK) {
        return err;
    }

    return packet[6] == 1 ? ESP_OK : ESP_FAIL;
}

esp_err_t chain_joystick_set_rgb(chain_joystick_t *ctx, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t payload[5] = {0, 1, r, g, b};
    uint8_t packet[16] = {0};
    size_t packet_len = sizeof(packet);
    esp_err_t err = chain_send_command(ctx, CHAIN_CMD_SET_RGB_VALUE, payload, sizeof(payload));
    if (err != ESP_OK) {
        return err;
    }

    err = chain_read_response(ctx, CHAIN_CMD_SET_RGB_VALUE, packet, &packet_len);
    if (err != ESP_OK) {
        return err;
    }

    return packet[6] == 1 ? ESP_OK : ESP_FAIL;
}

esp_err_t chain_joystick_deinit(chain_joystick_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ctx->present = false;
    return uart_driver_delete(ctx->uart_port);
}

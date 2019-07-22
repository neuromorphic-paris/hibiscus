const byte bnc_pin = 37;
const byte unused_pin = 38;
const byte jetson_up_pin = 28;
const byte jetson_switch_on_pin = 29;
const byte led_pin = 13;

/// serial_state contains the state parameters for the software serial reader.
struct serial_state {
    const byte pin;
    const uint32_t start_offset;
    const uint32_t tick_duration;
    byte code;
    uint32_t start_t;
    byte value;
    byte value_index;
};

/// read_serial must be called by the main loop.
/// true is returned if a new byte is available on the input serial.
bool read_serial(const uint32_t now, byte* value, serial_state* state) {
    switch (state->code) {
        case 0: {
            if (digitalReadFast(state->pin) == LOW) {
                state->code = 1;
                state->start_t = now;
                state->value = 0;
                state->value_index = 0;
            }
            break;
        }
        case 1: {
            if (now - state->start_t
                >= (uint32_t)state->start_offset + (uint32_t)state->value_index * (uint32_t)state->tick_duration) {
                if (digitalReadFast(state->pin) == HIGH) {
                    state->value |= (1 << state->value_index);
                }
                if (state->value_index == 7) {
                    state->code = 2;
                } else {
                    ++state->value_index;
                }
            }
            break;
        }
        case 2: {
            if (now - state->start_t >= (uint32_t)state->start_offset + 8u * (uint32_t)state->tick_duration) {
                if (digitalReadFast(state->pin) == HIGH) {
                    *value = state->value;
                    state->code = 0;
                    return true;
                } else {
                    state->code = 3;
                }
            }
            break;
        }
        case 3: {
            if (digitalReadFast(state->pin) == HIGH) {
                state->start_t = now;
                state->code = 4;
            }
            break;
        }
        case 4: {
            if (digitalReadFast(state->pin) == LOW) {
                state->code = 3;
            } else if (now - state->start_t >= (uint32_t)state->start_offset + 8 * (uint32_t)state->tick_duration) {
                state->code = 0;
            }
            break;
        }
        default: {
            state->code = 0;
            break;
        }
    }
    return false;
}

/// write escapes and writes a null-terminated message to the serial.
void write(const byte* message, const uint32_t size, const bool flush) {
    Serial.write(0x00);
    for (uint8_t index = 0; index < size; ++index) {
        switch (message[index]) {
            case 0x00:
                Serial.write(0xaa);
                Serial.write(0xab);
                break;
            case 0xaa:
                Serial.write(0xaa);
                Serial.write(0xac);
                break;
            case 0xff:
                Serial.write(0xaa);
                Serial.write(0xad);
                break;
            default:
                Serial.write(message[index]);
        }
    }
    Serial.write(0xff);
    if (flush) {
        Serial.send_now();
    }
}

/// cumulated_bits_to_message estimates the most likely message from the sent replicas.
byte cumulated_bits_to_message(const byte* cumulated_bits, const uint8_t threshold) {
    byte result = 0;
    for (uint8_t index = 0; index < 8; ++index) {
        if (cumulated_bits[index] >= threshold) {
            result |= (1 << index);
        }
    }
    return result;
}

/// state variables
serial_state state = {bnc_pin, (uint32_t)(1e6 / 9600 * 1.5), (uint32_t)(1e6 / 9600), 3, 0, 0, 0};
byte cumulated_bits[8] = {0, 0, 0, 0, 0, 0, 0, 0};
byte read_index = 0;
uint32_t previous_read_t = 0;

void setup() {
    pinMode(jetson_up_pin, INPUT);
    pinMode(jetson_switch_on_pin, OUTPUT);
    pinMode(bnc_pin, INPUT);
    pinMode(unused_pin, OUTPUT);
    pinMode(led_pin, OUTPUT);
    digitalWrite(led_pin, LOW);

    // boot the jetson
    digitalWrite(jetson_switch_on_pin, HIGH);
    if (digitalRead(jetson_up_pin) == LOW) {
        delay(500);
        digitalWrite(jetson_switch_on_pin, LOW);
        while (digitalRead(jetson_up_pin) == LOW) {
        }
        digitalWrite(jetson_switch_on_pin, HIGH);
    }

    // start the USB communication
    Serial.begin(9600);

    digitalWrite(led_pin, HIGH);
}

void loop() {
    const uint32_t now = micros();
    byte read_byte;
    if (read_serial(now, &read_byte, &state)) {
        ++read_index;
        previous_read_t = now;
        for (uint8_t index = 0; index < 8; ++index) {
            if (((read_byte >> index) & 1) == 1) {
                ++cumulated_bits[index];
            }
        }
        if (read_index >= 5) {
            const byte message = cumulated_bits_to_message(cumulated_bits, (read_index + 1) / 2);
            write(&message, 1, true);
            read_index = 0;
            memset(cumulated_bits, 0, sizeof(cumulated_bits));
        }
    } else if (read_index > 0 && now - previous_read_t > 20000) {
        const byte message = cumulated_bits_to_message(cumulated_bits, (read_index + 1) / 2);
        write(&message, 1, true);
        read_index = 0;
        memset(cumulated_bits, 0, sizeof(cumulated_bits));
    }
}

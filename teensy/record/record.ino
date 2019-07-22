const byte bnc_pin = 37;
const byte dmd_pin = 22;
const byte jack_l_pin = 2;
const byte jack_r_pin = 3;
const byte jetson_up_pin = 28;
const byte jetson_switch_on_pin = 29;
const byte led_pin = 13;
const uint32_t tick_half_period = 8333;

/// input represents a signal to watch and timestamp.
struct input {
    const byte id;
    const byte pin;
    const uint32_t debounce_t;
    void (*interrupt_callback)();
    uint8_t tail;
    volatile uint32_t change_t;
    volatile bool is_high;
    volatile uint8_t head;
    volatile uint32_t ts[256];
};

/// interrupt_callback is called whenever the input changes.
/// rising edge changes are notified to the main loop.
void interrupt_callback(input* updated_input) {
    const volatile uint32_t interrupt_t = micros();
    if (updated_input->is_high != (digitalRead(updated_input->pin) == HIGH)) {
        if (updated_input->is_high) {
            updated_input->is_high = false;
        } else {
            updated_input->is_high = true;
            if (interrupt_t - updated_input->change_t > updated_input->debounce_t) {
                updated_input->ts[updated_input->head] = interrupt_t;
                ++updated_input->head;
            }
        }
        updated_input->change_t = interrupt_t;
    }
}

/// inputs lists the input signals.
/// the character 'c' is reserved for clock messages.
input inputs[] = {
    {'d', dmd_pin, 0, d_callback},         // dmd
    {'l', jack_l_pin, 100000, l_callback}, // red button (left)
    {'r', jack_r_pin, 100000, r_callback}, // green button (right)
};
void d_callback() {
    interrupt_callback(&inputs[0]);
}
void l_callback() {
    interrupt_callback(&inputs[1]);
}
void r_callback() {
    interrupt_callback(&inputs[2]);
}

/// read_state stores the globals used by the read function.
struct read_state {
    byte* message;
    const uint32_t message_size;
    uint32_t index;
    bool reading;
    bool escaped;
};

/// read tries to consume a Serial byte and returns true if a new message is ready.
bool read(struct read_state* state) {
    if (Serial.available() > 0) {
        const byte read_byte = Serial.read();
        if (state->reading) {
            byte interpreted_byte;
            bool byte_available = false;
            if (state->escaped) {
                state->escaped = false;
                switch (read_byte) {
                    case 0xab:
                        interpreted_byte = 0x00;
                        byte_available = true;
                        break;
                    case 0xac:
                        interpreted_byte = 0xaa;
                        byte_available = true;
                        break;
                    case 0xad:
                        interpreted_byte = 0xff;
                        byte_available = true;
                        break;
                    default:
                        state->reading = false;
                }
            } else {
                switch (read_byte) {
                    case 0x00:
                        state->index = 0;
                        break;
                    case 0xaa:
                        state->escaped = true;
                        break;
                    case 0xff:
                        state->reading = false;
                        return true;
                    default:
                        interpreted_byte = read_byte;
                        byte_available = true;
                }
            }
            if (byte_available) {
                if (state->index < state->message_size) {
                    state->message[state->index] = interpreted_byte;
                    ++(state->index);
                } else {
                    state->reading = false;
                }
            }
        } else if (read_byte == 0x00) {
            state->reading = true;
            state->escaped = false;
            state->index = 0;
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

/// send generates and writes a message.
void send(const byte type, const uint32_t t, const bool flush) {
    byte message[5] = {
        type, (byte)(t & 0xff), (byte)((t >> 8) & 0xff), (byte)((t >> 16) & 0xff), (byte)((t >> 24) & 0xff)};
    write(message, sizeof(message), flush);
}

/// state variables
byte read_message[1];
read_state state = {read_message, sizeof(read_message), 0, false, false};
uint32_t previous_d_t = 0;
bool frame_boundary = false;
uint32_t tick = 0;
uint32_t tick_t = 0;
bool ticked = false;
uint32_t previous_flush_t = 0;
uint32_t computer_tick = 0;
bool pinged = false;
bool send_fake_event = false;

void setup() {
    // setup pins
    pinMode(jetson_up_pin, INPUT);
    pinMode(jetson_switch_on_pin, OUTPUT);
    pinMode(led_pin, OUTPUT);
    digitalWrite(led_pin, LOW);
    pinMode(bnc_pin, OUTPUT);
    digitalWrite(bnc_pin, LOW);

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

    // initialize the observables
    for (unsigned int index = 0; index < sizeof(inputs) / sizeof(input); ++index) {
        pinMode(inputs[index].pin, INPUT);
        inputs[index].tail = 0;
        inputs[index].change_t = 0;
        inputs[index].is_high = false;
        inputs[index].head = 0;
        attachInterrupt(digitalPinToInterrupt(inputs[index].pin), inputs[index].interrupt_callback, CHANGE);
    }

    // switch on the board LED
    digitalWrite(led_pin, HIGH);
}

void loop() {
    const volatile uint32_t loop_t = micros();
    {
        bool has_message = false;
        for (unsigned int index = 0; index < sizeof(inputs) / sizeof(input); ++index) {
            const uint8_t local_head = inputs[index].head;
            while (inputs[index].tail != local_head) {
                has_message = true;
                const uint32_t local_t = inputs[index].ts[inputs[index].tail];
                if (inputs[index].id == 'd') {
                    const uint32_t delta_t = local_t - previous_d_t;
                    if (delta_t > 1030 - 100 && delta_t < 1030 + 100) {
                        send('e', local_t, false);
                        frame_boundary = true;
                    } else {
                        if (frame_boundary) {
                            send('d', local_t, false);
                            frame_boundary = false;
                            tick_t = local_t;
                            ticked = true;
                            ++tick;
                        } else {
                            send('e', local_t, false);
                        }
                    }
                    previous_d_t = local_t;
                } else {
                    send(inputs[index].id, local_t, false);
                }
                ++inputs[index].tail;
            }
        }
        if (send_fake_event) {
            send_fake_event = false;
            send(loop_t % 2 == 0 ? 'l' : 'r', loop_t, false);
        }
        if (has_message || loop_t - previous_flush_t > 100000) {
            send('f', loop_t, true);
            previous_flush_t = loop_t;
        }
    }
    if (ticked && (micros() - tick_t > tick_half_period)) {
        send('c', tick, true);
        ticked = false;
    }
    if (read(&state)) {
        if (state.index == 1) {
            switch (state.message[0]) {
                case 'a':
                case 'b': {
                    digitalWrite(bnc_pin, state.message[0] == 'a' ? HIGH : LOW);
                    const uint32_t now = micros();
                    send(state.message[0], now, true);
                    break;
                }
                case 'r': {
                    digitalWrite(bnc_pin, LOW);
                    previous_d_t = 0;
                    tick = 0;
                    tick_t = 0;
                    ticked = false;
                    previous_flush_t = 0;
                    computer_tick = 0;
                    pinged = false;
                    noInterrupts();
                    for (unsigned int index = 0; index < sizeof(inputs) / sizeof(input); ++index) {
                        inputs[index].tail = inputs[index].head;
                    }
                    byte message[1] = {'r'};
                    write(message, sizeof(message), true);
                    interrupts();
                    break;
                }
                case 'f': {
                    send_fake_event = true;
                    break;
                }
                default:
                    break;
            }
        }
    }
}

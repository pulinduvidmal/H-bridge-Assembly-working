#include <avr/io.h>
#include <util/delay.h>
#include <avr/eeprom.h>
#include <Wire.h>
#include "LiquidCrystal_I2C.h"

// Define F_CPU for _delay_ms()
#define F_CPU 16000000UL

// Pin Definitions
#define DIR1_PORT PORTD
#define DIR1_PIN PD3
#define STEP1_PORT PORTD
#define STEP1_PIN PD2

#define DIR2_PORT PORTD
#define DIR2_PIN PD5
#define STEP2_PORT PORTD
#define STEP2_PIN PD4

#define DIR3_PORT PORTD
#define DIR3_PIN PD7
#define STEP3_PORT PORTD
#define STEP3_PIN PD6

#define DIR4_PORT PORTB
#define DIR4_PIN PB1
#define STEP4_PORT PORTB
#define STEP4_PIN PB0

#define UP_BUTTON_PIN PC0
#define DOWN_BUTTON_PIN PC1
#define MENU_BUTTON_PIN PC2
#define CANCEL_BUTTON_PIN PC3
#define IMMEDIATE_BUTTON_PIN PC4

// EEPROM Addresses for saving motor positions
uint16_t eepromAddr_stepper1 = 0;
uint16_t eepromAddr_stepper2 = 2;
uint16_t eepromAddr_stepper3 = 4;
uint16_t eepromAddr_stepper4 = 6;

// Variables to store saved positions
int16_t savedPosition1 = 0;
int16_t savedPosition2 = 0;
int16_t savedPosition3 = 0;
int16_t savedPosition4 = 0;

int16_t initial_speed1 = 10;
int16_t initial_speed2 = 10;
int16_t initial_speed3 = 10;
int16_t initial_speed4 = 10;
int16_t initial_x_coordinate = 10;
int16_t initial_z_coordinate = 10;
int16_t default_holes = 6;

int16_t temp_speed1 = 0;
int16_t temp_speed2 = 0;
int16_t temp_speed3 = 10;
int16_t temp_speed4 = 10;
int16_t temp_x_coordinate = 0;
int16_t temp_z_coordinate = 0;
int16_t temp_holes = 0;

// LCD Initialization
LiquidCrystal_I2C lcd(0x27, 16, 2);

void setupLCD() {
    lcd.init();
    lcd.backlight();
    lcd.print("Welcome to Pick and Place Robot Arm");
    _delay_ms(5000);
    lcd.clear();
}

// Button Configuration
void setupButtonPins() {
    DDRC &= ~((1 << UP_BUTTON_PIN) | (1 << DOWN_BUTTON_PIN) | (1 << MENU_BUTTON_PIN) | (1 << CANCEL_BUTTON_PIN) | (1 << IMMEDIATE_BUTTON_PIN));
    PORTC |= (1 << UP_BUTTON_PIN) | (1 << DOWN_BUTTON_PIN) | (1 << MENU_BUTTON_PIN) | (1 << CANCEL_BUTTON_PIN) | (1 << IMMEDIATE_BUTTON_PIN);
}

// Main Menu and Submenus
const char* options_main[] = { "1 - Menu", "2 - Continue" };
int current_main_mode = 0;
const int max_main_modes = 2;

const char* options[] = { "1 - Calibration Mode", "2 - Set Speed", "3 - Number of the holes", "4 - Back" };
const char* sub1_options[] = { "1 - Distance X axis", "2 - Distance Z axis", "3 - Back" };
const char* sub2_options[] = { "1 - Stepper 1", "2 - Stepper 2", "3 - Stepper 3", "4 - Stepper 4", "5 - Back" };

void displayMainMenu() {
    lcd.clear();
    lcd.print(options_main[current_main_mode]);
}

void navigateMenu();
void execute_operation();
bool currentStateChange(uint8_t pin);
void savePositionToEEPROM(uint16_t addr, int16_t value);
int16_t readPositionFromEEPROM(uint16_t addr);
void navigateSub1Menu();
void navigateSub2Menu();
void calibrateXAxis();
void calibrateZAxis();
void setSpeed(int stepper);
void setNumberOfHoles();
void step_motor(volatile uint8_t *step_port, uint8_t step_pin, volatile uint8_t *dir_port, uint8_t dir_pin, uint16_t steps, uint8_t direction);
bool immidiate();

void loop() {
    while (1) {
        displayMainMenu();
        _delay_ms(100);

        if (currentStateChange(UP_BUTTON_PIN)) {
            current_main_mode = (current_main_mode + 1) % max_main_modes;
        } else if (currentStateChange(DOWN_BUTTON_PIN)) {
            current_main_mode = (current_main_mode - 1 + max_main_modes) % max_main_modes;
        } else if (currentStateChange(MENU_BUTTON_PIN)) {
            if (current_main_mode == 0) {
                navigateMenu();
            } else {
                execute_operation();
            }
        }
    }
}

void execute_operation() {
    int runloop = 1;

    while (runloop) {
        // Move to positions
        step_motor(&STEP1_PORT, STEP1_PIN, &DIR1_PORT, DIR1_PIN, 2000, 1);  // Move 2000 steps forward
        step_motor(&STEP2_PORT, STEP2_PIN, &DIR2_PORT, DIR2_PIN, 2000, 1);
        step_motor(&STEP3_PORT, STEP3_PIN, &DIR3_PORT, DIR3_PIN, 200, 1);
        step_motor(&STEP4_PORT, STEP4_PIN, &DIR4_PORT, DIR4_PIN, 20, 1);

        // Wait until movement is complete or immediate button is pressed
        while (runloop) {
            if (immidiate()) {
                runloop = 0;
                break;
            }
        }

        save_positions();
        _delay_ms(2000);

        // Move back to zero
        step_motor(&STEP1_PORT, STEP1_PIN, &DIR1_PORT, DIR1_PIN, 2000, 0);  // Move 2000 steps backward
        step_motor(&STEP2_PORT, STEP2_PIN, &DIR2_PORT, DIR2_PIN, 2000, 0);
        step_motor(&STEP3_PORT, STEP3_PIN, &DIR3_PORT, DIR3_PIN, 200, 0);
        step_motor(&STEP4_PORT, STEP4_PIN, &DIR4_PORT, DIR4_PIN, 20, 0);

        // Wait until movement is complete or immediate button is pressed
        while (runloop) {
            if (immidiate()) {
                runloop = 0;
                break;
            }
        }

        save_positions();
        _delay_ms(2000);
    }
}

// Helper Functions
bool currentStateChange(uint8_t pin) {
    static uint8_t lastState = 0xFF;
    uint8_t currentState = PINC & (1 << pin);
    if (currentState != lastState) {
        lastState = currentState;
        if (currentState == 0) {
            return true;
        }
    }
    return false;
}

void savePositionToEEPROM(uint16_t addr, int16_t value) {
    eeprom_write_word((uint16_t*)addr, value);
}

int16_t readPositionFromEEPROM(uint16_t addr) {
    return eeprom_read_word((uint16_t*)addr);
}

void navigateMenu() {
    int current_sub_mode = 0;
    const int max_sub_modes = 4;
    while (1) {
        lcd.clear();
        lcd.print(options[current_sub_mode]);
        _delay_ms(100);

        if (currentStateChange(UP_BUTTON_PIN)) {
            current_sub_mode = (current_sub_mode + 1) % max_sub_modes;
        } else if (currentStateChange(DOWN_BUTTON_PIN)) {
            current_sub_mode = (current_sub_mode - 1 + max_sub_modes) % max_sub_modes;
        } else if (currentStateChange(MENU_BUTTON_PIN)) {
            if (current_sub_mode == 0) {
                navigateSub1Menu();
            } else if (current_sub_mode == 1) {
                navigateSub2Menu();
            } else if (current_sub_mode == 2) {
                setNumberOfHoles();
            } else {
                break;
            }
        }
    }
}

void navigateSub1Menu() {
    int current_sub1_mode = 0;
    const int max_sub1_modes = 3;
    while (1) {
        lcd.clear();
        lcd.print(sub1_options[current_sub1_mode]);
        _delay_ms(100);

        if (currentStateChange(UP_BUTTON_PIN)) {
            current_sub1_mode = (current_sub1_mode + 1) % max_sub1_modes;
        } else if (currentStateChange(DOWN_BUTTON_PIN)) {
            current_sub1_mode = (current_sub1_mode - 1 + max_sub1_modes) % max_sub1_modes;
        } else if (currentStateChange(MENU_BUTTON_PIN)) {
            if (current_sub1_mode == 0) {
                calibrateXAxis();
            } else if (current_sub1_mode == 1) {
                calibrateZAxis();
            } else {
                break;
            }
        }
    }
}

void navigateSub2Menu() {
    int current_sub2_mode = 0;
    const int max_sub2_modes = 5;
    while (1) {
        lcd.clear();
        lcd.print(sub2_options[current_sub2_mode]);
        _delay_ms(100);

        if (currentStateChange(UP_BUTTON_PIN)) {
            current_sub2_mode = (current_sub2_mode + 1) % max_sub2_modes;
        } else if (currentStateChange(DOWN_BUTTON_PIN)) {
            current_sub2_mode = (current_sub2_mode - 1 + max_sub2_modes) % max_sub2_modes;
        } else if (currentStateChange(MENU_BUTTON_PIN)) {
            if (current_sub2_mode == 0) {
                setSpeed(1);
            } else if (current_sub2_mode == 1) {
                setSpeed(2);
            } else if (current_sub2_mode == 2) {
                setSpeed(3);
            } else if (current_sub2_mode == 3) {
                setSpeed(4);
            } else {
                break;
            }
        }
    }
}

void calibrateXAxis() {
    lcd.clear();
    lcd.print("Calibrating X Axis");
    _delay_ms(2000);
    savePositionToEEPROM(eepromAddr_stepper1, savedPosition1);
}

void calibrateZAxis() {
    lcd.clear();
    lcd.print("Calibrating Z Axis");
    _delay_ms(2000);
    savePositionToEEPROM(eepromAddr_stepper2, savedPosition2);
}

void setSpeed(int stepper) {
    lcd.clear();
    lcd.print("Setting Speed for Stepper ");
    lcd.print(stepper);
    _delay_ms(2000);
}

void setNumberOfHoles() {
    lcd.clear();
    lcd.print("Setting Number of Holes");
    _delay_ms(2000);
}

void step_motor(volatile uint8_t *step_port, uint8_t step_pin, volatile uint8_t *dir_port, uint8_t dir_pin, uint16_t steps, uint8_t direction) {
    // Example function to step the motor
    *dir_port = (*dir_port & ~(1 << dir_pin)) | (direction << dir_pin);  // Set direction
    for (uint16_t i = 0; i < steps; i++) {
        *step_port |= (1 << step_pin);  // Set step pin high
        _delay_us(100);  
        *step_port &= ~(1 << step_pin);  // Set step pin low
        _delay_us(100);
    }
}

bool immidiate() {
    // Check if the immediate stop button is pressed
    return !(PINC & (1 << IMMEDIATE_BUTTON_PIN));
}

void save_positions() {
    savePositionToEEPROM(eepromAddr_stepper1, savedPosition1);
    savePositionToEEPROM(eepromAddr_stepper2, savedPosition2);
    savePositionToEEPROM(eepromAddr_stepper3, savedPosition3);
    savePositionToEEPROM(eepromAddr_stepper4, savedPosition4);
}

void setup() {
    // Initialize LCD
    setupLCD();
    // Initialize button pins
    setupButtonPins();
   
}

int main() {
    setup();
    while (1) {
        loop();
    }
    return 0;
}

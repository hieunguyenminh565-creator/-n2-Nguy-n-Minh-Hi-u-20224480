/*
 * GccApplication3.c
 *
 * Created: 12/7/2025 8:57:12 AM
 * Author : hieun
 */

#define F_CPU 8000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* ----------------------------------------------------
 *  LCD 16x2 8-bit (D0..D7 -> PORTC, RS/RW/E -> PD6/PD5/PD7)
 * --------------------------------------------------*/
#define LCD_DATA_PORT   PORTC
#define LCD_DATA_DDR    DDRC

#define LCD_CTRL_PORT   PORTD
#define LCD_CTRL_DDR    DDRD
#define LCD_RS          PD6
#define LCD_RW          PD5
#define LCD_E           PD7

static void lcd_pulse_enable(void)
{
    LCD_CTRL_PORT |= (1 << LCD_E);
    _delay_us(1);
    LCD_CTRL_PORT &= ~(1 << LCD_E);
    _delay_us(100);
}

static void lcd_write(uint8_t data, bool isData)
{
    if (isData) LCD_CTRL_PORT |= (1 << LCD_RS);
    else        LCD_CTRL_PORT &= ~(1 << LCD_RS);

    LCD_CTRL_PORT &= ~(1 << LCD_RW); // write mode
    LCD_DATA_PORT = data;
    lcd_pulse_enable();
}

static void lcd_command(uint8_t cmd) { lcd_write(cmd, false); }
static void lcd_data(uint8_t data)   { lcd_write(data, true); }

static void lcd_clear(void)
{
    lcd_command(0x01);
    _delay_ms(2);
}

static void lcd_gotoxy(uint8_t x, uint8_t y)
{
    uint8_t addr = (y == 0) ? 0x00 : 0x40;
    addr += x;
    lcd_command(0x80 | addr);
}

static void lcd_print(const char *s)
{
    while (*s) lcd_data(*s++);
}

static void lcd_init(void)
{
    LCD_DATA_DDR = 0xFF;
    LCD_CTRL_DDR |= (1 << LCD_RS) | (1 << LCD_RW) | (1 << LCD_E);

    _delay_ms(40);
    lcd_command(0x38); // 8-bit, 2 lines, 5x8 font
    lcd_command(0x0C); // display on, cursor off
    lcd_command(0x06); // entry mode
    lcd_clear();
}

/* ----------------------------------------------------
 *  Keypad 4x4 (PA0..PA3 rows, PA4..PA7 cols)
 *  Layout gi?ng ?nh:
 *  1 2 3 A
 *  4 5 6 B
 *  7 8 9 C
 *  * 0 # D
 * --------------------------------------------------*/
static void keypad_init(void)
{
    DDRA  = 0x0F;  // PA0..PA3 output (rows), PA4..PA7 input (cols)
    PORTA = 0xF0;  // pull-up for columns, rows default high
}

static const char keypad_map[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

static char keypad_getkey_blocking(void)
{
    while (1)
    {
        for (uint8_t row = 0; row < 4; row++)
        {
            PORTA |= 0x0F;          // all rows high
            PORTA &= ~(1 << row);   // current row low
            _delay_us(5);

            uint8_t cols = (PINA >> 4) & 0x0F;

            if (cols != 0x0F)
            {
                for (uint8_t col = 0; col < 4; col++)
                {
                    if (!(cols & (1 << col)))
                    {
                        char key = keypad_map[row][col];

                        // wait release
                        while (!(((PINA >> 4) & (1 << col))))
                            ;

                        _delay_ms(20);
                        return key;
                    }
                }
            }
        }
    }
}

/* ----------------------------------------------------
 *  Buzzer, Relay, LEDs
 * --------------------------------------------------*/
// PORTB:
// PB4: relay out (active-high)
// PB6, PB7: buzzer

#define RELAY_PORT PORTB
#define RELAY_DDR  DDRB
#define RELAY_PIN  PB4

#define BUZZER_DDR  DDRB
#define BUZZER_PORT PORTB
#define BUZZER1_PIN PB6
#define BUZZER2_PIN PB7

// LEDs: PORTD (PD0..PD7) - chú ý PD5/PD6/PD7 dùng LCD control
#define LED_DDR    DDRD
#define LED_PORT   PORTD
#define LED_MASK   0xFF

static void io_init(void)
{
    // (Không dùng nút r?i n?a) - ?? tránh floating, b?t pull-up PB0..PB3 (tu? b?n có th? b?)
    DDRB  &= ~((1 << PB0) | (1 << PB1) | (1 << PB2) | (1 << PB3));
    PORTB |=  (1 << PB0) | (1 << PB1) | (1 << PB2) | (1 << PB3);

    // Relay
    RELAY_DDR |= (1 << RELAY_PIN);
    RELAY_PORT &= ~(1 << RELAY_PIN);

    // Buzzer
    BUZZER_DDR |= (1 << BUZZER1_PIN) | (1 << BUZZER2_PIN);
    BUZZER_PORT &= ~((1 << BUZZER1_PIN) | (1 << BUZZER2_PIN));

    // LEDs
    LED_DDR |= LED_MASK;
    LED_PORT &= ~LED_MASK;
}

static void relay_on(void)  { RELAY_PORT |=  (1 << RELAY_PIN); }
static void relay_off(void) { RELAY_PORT &= ~(1 << RELAY_PIN); }

static void buzzer_on(void)
{
    BUZZER_PORT |= (1 << BUZZER1_PIN) | (1 << BUZZER2_PIN);
}

static void buzzer_off(void)
{
    BUZZER_PORT &= ~((1 << BUZZER1_PIN) | (1 << BUZZER2_PIN));
}

static void buzzer_beep(uint16_t ms)
{
    buzzer_on();
    while (ms--) _delay_ms(1);
    buzzer_off();
}

static void buzzer_short(void)
{
    BUZZER_PORT |= (1 << BUZZER1_PIN);
    _delay_ms(20);
    BUZZER_PORT &= ~(1 << BUZZER1_PIN);
}

static void leds_set(uint8_t value)
{
    LED_PORT = (LED_PORT & ~LED_MASK) | (value & LED_MASK);
}

/* ----------------------------------------------------
 *  Password logic
 * --------------------------------------------------*/
#define PASS_LEN 6
static const char DEFAULT_PASS[PASS_LEN + 1] = "000000";
static char current_pass[PASS_LEN + 1] = "000000";

typedef enum {
    STATE_LOCKED = 0,
    STATE_UNLOCKED
} system_state_t;

static system_state_t g_state = STATE_LOCKED;

static void ui_show_locked(void)
{
    lcd_clear();
    lcd_gotoxy(0,0);
    lcd_print("Enter password");
    lcd_gotoxy(0,1);
    lcd_print("> ");
    leds_set(0x00);
    relay_off();
}

static void ui_show_unlocked(void)
{
    lcd_clear();
    lcd_gotoxy(0,0);
    lcd_print("UNLOCKED");
    lcd_gotoxy(0,1);
    lcd_print("Door is OPEN");
    leds_set(0xFF);
    relay_on();
}

static void update_leds_by_count(uint8_t count)
{
    if (count == 0) {
        leds_set(0x00);
    } else if (count <= 8) {
        uint8_t led_pattern = 0x00;
        for (uint8_t i = 0; i < count; i++) led_pattern |= (1 << i);
        leds_set(led_pattern);
    } else {
        leds_set(0xFF);
    }
}

/*
 * Nh?p m?t kh?u b?ng keypad:
 *  - ??: thêm ký t? (t?i ?a PASS_LEN)
 *  - '*': xóa 1 ký t?
 *  - 'A': xóa h?t
 *  - '#': enter (ch? accept khi ?? PASS_LEN)
 *  - phím khác: b? qua
 */
static void read_password_from_keypad(char *buf, const char *prompt)
{
    uint8_t idx = 0;

    lcd_clear();
    lcd_gotoxy(0,0);
    lcd_print(prompt);
    lcd_gotoxy(0,1);
    lcd_print("> ");

    memset(buf, 0, PASS_LEN + 1);
    update_leds_by_count(0);

    while (1)
    {
        char key = keypad_getkey_blocking();
        buzzer_short();

        if (key >= '0' && key <= '9')
        {
            if (idx < PASS_LEN)
            {
                buf[idx++] = key;
                lcd_data('*');
                update_leds_by_count(idx);
            }
        }
        else if (key == '*') // backspace
        {
            if (idx > 0)
            {
                idx--;
                buf[idx] = '\0';
                lcd_gotoxy(2 + idx, 1);
                lcd_data(' ');
                lcd_gotoxy(2 + idx, 1);
                update_leds_by_count(idx);
            }
        }
        else if (key == 'A') // clear all
        {
            idx = 0;
            memset(buf, 0, PASS_LEN + 1);
            lcd_gotoxy(2,1);
            for (uint8_t i = 0; i < PASS_LEN; i++) lcd_data(' ');
            lcd_gotoxy(2,1);
            update_leds_by_count(0);
        }
        else if (key == '#') // enter
        {
            if (idx == PASS_LEN)
            {
                buf[PASS_LEN] = '\0';
                return;
            }
            else
            {
                // ch?a ?? ký t? thì báo nh?
                buzzer_beep(60);
            }
        }
        // B/C/D ... b? qua khi ?ang nh?p pass
    }
}

/* ----------------------------------------------------
 *  Change / Reset password (ch? khi UNLOCKED)
 * --------------------------------------------------*/
static void change_password_flow(void)
{
    char new_pass[PASS_LEN + 1];

    read_password_from_keypad(new_pass, "New password:");

    memcpy(current_pass, new_pass, PASS_LEN + 1);

    lcd_clear();
    lcd_gotoxy(0,0);
    lcd_print("Password saved");
    buzzer_beep(150);
    _delay_ms(800);

    ui_show_unlocked();
}

static void reset_password_default(void)
{
    memcpy(current_pass, DEFAULT_PASS, PASS_LEN + 1);

    lcd_clear();
    lcd_gotoxy(0,0);
    lcd_print("Reset to default");
    lcd_gotoxy(0,1);
    lcd_print("PASS=000000");
    buzzer_beep(150);
    _delay_ms(900);

    ui_show_unlocked();
}

/* ----------------------------------------------------
 *  MAIN
 * --------------------------------------------------*/
int main(void)
{
    io_init();
    lcd_init();
    keypad_init();

    ui_show_locked();

    while (1)
    {
        if (g_state == STATE_LOCKED)
        {
            char entered[PASS_LEN + 1];
            read_password_from_keypad(entered, "Enter password");

            if (memcmp(entered, current_pass, PASS_LEN) == 0)
            {
                g_state = STATE_UNLOCKED;
                ui_show_unlocked();
                buzzer_beep(80);
            }
            else
            {
                lcd_clear();
                lcd_gotoxy(0,0);
                lcd_print("Wrong password");
                buzzer_beep(150);
                _delay_ms(1200);
                ui_show_locked();
            }
        }
        else // STATE_UNLOCKED
        {
            ui_show_unlocked();

            // Không còn ??m ng??c / t? khóa theo th?i gian
            // Ch? phím ch?c n?ng:
            // B: khóa l?i
            // C: ??i m?t kh?u
            // D: reset v? m?c ??nh 000000
            while (g_state == STATE_UNLOCKED)
            {
                char key = keypad_getkey_blocking();
                buzzer_short();

                if (key == 'B')
                {
                    g_state = STATE_LOCKED;
                    ui_show_locked();
                    buzzer_beep(120);
                    break;
                }
                else if (key == 'C')
                {
                    change_password_flow();
                }
                else if (key == 'D')
                {
                    reset_password_default();
                }
                // phím khác b? qua
            }
        }
    }

    return 0;
}

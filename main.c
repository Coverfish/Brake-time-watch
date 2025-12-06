/*
 * File:   main.c
 * Author: Coverfish
 *
 * Created on 26. November 2025, 13:33
 */

// PIC16F716 Configuration Bit Settings

// 'C' source line config statements

// CONFIG
#pragma config FOSC = XT        // Oscillator Selection bits (RC oscillator)
#pragma config WDTE = OFF        // Watchdog Timer Enable bit (WDT disabled)
#pragma config PWRTE = OFF      // Power-up Timer Enable bit (PWRT disabled)
#pragma config BOREN = ON       // Brown-out Reset Enable bit (BOR enabled)
#pragma config BODENV = 40      // Brown-out Reset Voltage bit (VBOR set to 4.0V)
#pragma config CP = OFF         // Code Protect (Program memory code protection is disabled)

// #pragma config statements should precede project file includes.
// Use project enums instead of #define for ON and OFF.


#include <xc.h>

#define _XTAL_FREQ 4000000UL   // 4 MHz Quarz

// ================== 7-Segment Grundtabelle ==================
// Logisch: Bit0=a, Bit1=b, Bit2=c, Bit3=d, Bit4=e, Bit5=f, Bit6=g
// Diese Tabelle beschreibt NUR, welche Segmente für 0..9 leuchten sollen.
const unsigned char seg_lut_raw[10] = {
    0x3F, // 0
    0x06, // 1
    0x5B, // 2
    0x4F, // 3
    0x66, // 4
    0x6D, // 5
    0x7D, // 6
    0x07, // 7
    0x7F, // 8
    0x6F  // 9
};

// ================== Mapping auf dein PORTB ==================
// Deine Verdrahtung:
//   RB0 = f
//   RB1 = a
//   RB2 = b
//   RB3 = g
//   RB4 = d
//   RB5 = c
//   RB7 = e
//   RB6 unbenutzt
static unsigned char seg_to_port(unsigned char digit)
{
    unsigned char s = seg_lut_raw[digit];  // logische Segmente a..g
    unsigned char o = 0;

    if (s & (1 << 5)) o |= (1 << 0); // f -> RB0
    if (s & (1 << 0)) o |= (1 << 1); // a -> RB1
    if (s & (1 << 1)) o |= (1 << 2); // b -> RB2
    if (s & (1 << 6)) o |= (1 << 3); // g -> RB3
    if (s & (1 << 3)) o |= (1 << 4); // d -> RB4
    if (s & (1 << 2)) o |= (1 << 5); // c -> RB5
    if (s & (1 << 4)) o |= (1 << 7); // e -> RB7

    // RB6 bleibt 0
    return o;
}

// ================== Zustände ==================
#define STATE_IDLE    0   // Anzeige eingefroren, wartet auf neues HIGH
#define STATE_RUNNING 1   // zählt Zeit

volatile unsigned int  tenths    = 0;  // 0..999 -> 0,0..99,9 s
volatile unsigned char state     = STATE_IDLE;
volatile unsigned char mux_digit = 0;  // 0,1,2 -> welche Stelle aktiv
volatile unsigned char tick_10ms = 0;  // 0..9 -> 100 ms

// ================== Initialisierung ==================
void setup(void)
{
    // Alle analogen Eingänge aus -> RA0..RA3 als digital
    ADCON1 = 0b00000111;   // PCFG2:0 = 111 -> RA3..RA0 alle digital, Vref = Vdd

    // PORTB: Segmente (RB0..RB7, RB6 unbenutzt) als Ausgang
    TRISB = 0x00;
    PORTB = 0x00;

    // PORTA:
    // RA0..RA2 -> Digit-Ausgänge (links, mitte, rechts)
    // RA3      -> Eingang vom 74HC14
    TRISAbits.TRISA0 = 0;  // 10s-Display (links)
    TRISAbits.TRISA1 = 0;  // 1s-Display (mitte)
    TRISAbits.TRISA2 = 0;  // 0,1s-Display (rechts)
    TRISAbits.TRISA3 = 1;  // Eingang
    TRISAbits.TRISA4 = 1;  // unbenutzt / Eingang

    PORTAbits.RA0 = 0;
    PORTAbits.RA1 = 0;
    PORTAbits.RA2 = 0;

    // ========== Timer1 für 5-ms-Interrupt ==========
    T1CON = 0b00110001;  // bleibt gleich (Prescaler 1:8)
    TMR1H = 0xFD;        // 5 ms bei 4 MHz, 1:8 -> Preload 0xFD8F
    TMR1L = 0x8F;

    PIR1bits.TMR1IF = 0;
    PIE1bits.TMR1IE = 1;   // Timer1-Interrupt erlauben

    INTCONbits.PEIE = 1;   // Peripherie-Interrupts
    INTCONbits.GIE  = 1;   // Global Interrupts
}

// ================== Interrupt ==================
void __interrupt() isr(void)
{
    // ----- Timer1 alle 5 ms -----
    if (PIR1bits.TMR1IF) {
        PIR1bits.TMR1IF = 0;

        // Timer für die nächsten 5 ms neu vorladen
        TMR1H = 0xFD;
        TMR1L = 0x8F;
        
        // ----- Anzeige multiplexen -----
        unsigned int val = tenths;          // Kopie für Anzeige
        unsigned char d0 = val / 100;       // 10er Sekunden (links)
        unsigned char d1 = (val / 10) % 10; // Sekunden (mitte)
        unsigned char d2 = val % 10;        // Zehntel (rechts)

        // Alle Digits zuerst aus
        PORTAbits.RA0 = 0;
        PORTAbits.RA1 = 0;
        PORTAbits.RA2 = 0;

    switch (mux_digit) {
        case 0: // linke Stelle (10s)
            PORTB = seg_to_port(d0);
            PORTAbits.RA2 = 1;
            break;

        case 1: // mittlere Stelle (1s)
            PORTB = seg_to_port(d1);
            PORTAbits.RA1 = 1;
            break;

        default: // rechte Stelle (Zehntel)
            PORTB = seg_to_port(d2);
            PORTAbits.RA0 = 1;
            break;
    }

        mux_digit++;
        if (mux_digit >= 3) mux_digit = 0;

        // ----- Zeitbasis 5 ms -> 0,1 s -----
        tick_10ms++;
        if (tick_10ms >= 20) {    // 20 * 5 ms = 0,1 s
            tick_10ms = 0;

            if (state == STATE_RUNNING) {
                if (tenths < 999) { // max 99,9 s
                    tenths++;
                }
            }
        }
    }
}

// ================== Hauptprogramm ==================
int main(void)
{
    setup();

    unsigned char last_in = 0;

    while (1) {
        // Eingang vom 74HC14 einlesen:
        // HIGH  -> 40 VAC am Gleichrichter (Bremsrelais an)
        // LOW   -> 8,7 VAC oder 0 V (Bremsrelais aus)
        unsigned char in = PORTAbits.RA3;

        // Flanke LOW -> HIGH: neue Messung starten
        if (in && !last_in) {
            tenths    = 0;           // Zeit auf 0,0 s
            tick_10ms = 0;
            state     = STATE_RUNNING;
        }

        // Flanke HIGH -> LOW: Zählen stoppen, Anzeige einfrieren
        if (!in && last_in) {
            state = STATE_IDLE;
            // tenths NICHT zurücksetzen -> Wert bleibt stehen,
            // bis beim nächsten HIGH wieder von 0 neu gestartet wird.
        }

        last_in = in;
        // Rest macht der Interrupt, daher hier keine Delays nötig
    }
}

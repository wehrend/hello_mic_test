// hello_mic.cpp - Minimaltest für das INMP441-I2S-Mikrofon, analog zum
// allerersten Sinuston-Test für den PCM5102A-DAC (siehe Blogartikel
// Kapitel 1). Keine FreeRTOS, kein Vocoder - nur: Rohwerte vom
// Mikrofon einlesen und über die serielle Verbindung ausgeben, um die
// Hardware-/PIO-Kette isoliert zu prüfen, bevor sie in den Vocoder
// integriert wird.
//
// WICHTIG: Das zugehörige PIO-Programm (i2s_mic.pio) ist neu und noch
// nicht an echter Hardware getestet. Die Logik beruht auf dem
// Standard-I2S-Timing, aber falls die Werte sich beim Sprechen nicht
// verändern, ist das der normale erste Schritt bei komplett neuem
// Terrain, kein Rückschlag - dann mit einem Logikanalysator (falls
// vorhanden) SCK/WS/SD direkt ansehen.
//
// Verkabelung INMP441 (bewusst ANDERE Pins als der bestehende DAC!):
//   VDD -> 3V3
//   GND -> GND
//   L/R -> GND   (wählt den LINKEN Kanalslot - genau den lesen wir aus)
//   WS  -> GPIO 11
//   SCK -> GPIO 10
//   SD  -> GPIO 12

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include <cstdio>
#include "i2s_mic.pio.h"

namespace {

constexpr uint kMicSckPin = 10;  // side-set Basis-Pin (SCK); WS = SCK+1 automatisch
constexpr uint kMicWsPin  = 11;
constexpr uint kMicSdPin  = 12;  // Dateneingang

// Bewusst moderat für den ersten Test - keine hohe Samplerate nötig,
// um nur zu prüfen, ob überhaupt sauber Daten ankommen.
constexpr uint32_t kSampleRateHz = 16000;

} // namespace

int main() {
    stdio_init_all();
    sleep_ms(2000); // Zeit, den seriellen Monitor zu öffnen, bevor die erste Ausgabe kommt

    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &i2s_mic_in_program);

    pio_sm_config c = i2s_mic_in_program_get_default_config(offset);

    // Side-Set-Pins: SCK (Basis) + WS (Basis+1), beide Ausgänge
    sm_config_set_sideset_pins(&c, kMicSckPin);
    pio_gpio_init(pio, kMicSckPin);
    pio_gpio_init(pio, kMicWsPin);
    pio_sm_set_consecutive_pindirs(pio, sm, kMicSckPin, 2, true);

    // Daten-Eingangspin (SD)
    sm_config_set_in_pins(&c, kMicSdPin);
    pio_gpio_init(pio, kMicSdPin);
    // Pulldown: das INMP441 schaltet SD außerhalb seines Slots (und beim
    // I2S-Verzögerungsbit) hochohmig - ohne Pulldown wären diese Bits
    // zufällig. Datenblatt empfiehlt ~100k, der interne reicht.
    gpio_pull_down(kMicSdPin);
    pio_sm_set_consecutive_pindirs(pio, sm, kMicSdPin, 1, false);

    // MSB zuerst einschieben (shift_right=false -> shift links, erstes
    // Bit landet nach 32 Schritten an der höchstwertigen Stelle),
    // Auto-Push nach 32 Bit - kein manuelles "push" im PIO-Programm nötig.
    sm_config_set_in_shift(&c, false, true, 32);

    // RX-FIFO auf 8 Wörter verdoppeln (TX wird nicht gebraucht) - mehr
    // Puffer, während printf() über USB die Schleife aufhält.
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

    // Taktteiler: exakt 128 PIO-Takte pro Frame (2 je SCK-Periode,
    // 32 SCK-Perioden je Kanal, 2 Kanäle) -> Samplerate = sys_clk / (128*Teiler).
    //
    // WICHTIG: über die Config setzen, NICHT per pio_sm_set_clkdiv() vor
    // pio_sm_init() - pio_sm_init() schreibt die komplette Config inkl.
    // Teiler in die State Machine und hätte den Wert wieder auf den
    // Default 1.0 zurückgesetzt (SCK ~62,5 MHz statt ~1 MHz).
    float div = (float)clock_get_hz(clk_sys) / ((float)kSampleRateHz * 128.0f);
    sm_config_set_clkdiv(&c, div);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    printf("INMP441 Hello-World-Test gestartet (Samplerate ~%lu Hz).\n", (unsigned long)kSampleRateHz);
    printf("Sprich oder klopf ans Mikro - min/max sollten sich bewegen.\n");

    int32_t minVal = 0x7fffffff;
    int32_t maxVal = -0x7fffffff;
    uint32_t sampleCount = 0;
    uint64_t lastPrintUs = time_us_64();

    for (;;) {
        if (!pio_sm_is_rx_fifo_empty(pio, sm)) {
            uint32_t raw = pio_sm_get(pio, sm);
            // Aufbau des 32-Bit-Worts (MSB zuerst eingeschoben):
            //   Bit 31     : I2S-Verzögerungsbit (kein Datenbit, per Pulldown 0)
            //   Bit 30..7  : 24 Datenbits D23..D0 des INMP441
            //   Bit 6..0   : Füllbits
            // "<< 1" wirft das Verzögerungsbit raus, sodass D23 (Vorzeichen)
            // auf Bit 31 landet; das arithmetische ">> 8" liefert dann den
            // vorzeichenrichtigen 24-Bit-Wert.
            int32_t sample = (int32_t)(raw << 1) >> 8;
            if (sample < minVal) minVal = sample;
            if (sample > maxVal) maxVal = sample;
            sampleCount++;
        }

        uint64_t nowUs = time_us_64();
        if (nowUs - lastPrintUs >= 200000) { // alle 200ms
            printf("min=%ld max=%ld samples=%lu\n",
                   (long)minVal, (long)maxVal, (unsigned long)sampleCount);
            minVal = 0x7fffffff;
            maxVal = -0x7fffffff;
            sampleCount = 0;
            lastPrintUs = nowUs;
        }
    }
}

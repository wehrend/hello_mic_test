# hello_mic_test

Minimaltest für das **INMP441**-I2S-MEMS-Mikrofon am Raspberry Pi Pico (RP2040), analog zum allerersten Sinuston-Test für den PCM5102A-DAC.

Kein FreeRTOS, kein Vocoder: Das Programm liest nur Rohwerte vom Mikrofon über einen eigenen PIO-I2S-Empfänger ein und gibt alle 200 ms Minimum, Maximum und Anzahl der Samples über USB-Seriell aus. So lässt sich die Hardware-/PIO-Kette isoliert prüfen, bevor das Mikrofon in den Vocoder integriert wird.

## Hintergrund

Der RP2040 hat keine eingebaute I2S-Schnittstelle. Der Empfänger ist deshalb als PIO-Programm (`src/i2s_mic.pio`) umgesetzt. Der Pico ist dabei I2S-**Master**: Die PIO erzeugt den Bit-Takt (SCK) und die Wortauswahl (WS) selbst, das INMP441 liefert passend dazu seine Daten auf SD.

- 32 Bit-Takte je Kanal, 2 Kanäle → 64 SCK-Perioden pro Frame
- exakt 128 PIO-Takte pro Frame (2 je SCK-Periode)
- gelesen wird nur der **linke** Kanal (L/R-Pin des INMP441 auf GND)
- Autopush nach 32 Bit, ein Wort pro Sample im RX-FIFO

Aufbau des 32-Bit-Worts im FIFO:

| Bits   | Inhalt                                              |
| ------ | --------------------------------------------------- |
| 31     | I2S-Verzögerungsbit (kein Datenbit, per Pulldown 0) |
| 30 … 7 | 24 Datenbits D23 … D0 (vorzeichenbehaftet)          |
| 6 … 0  | Füllbits                                            |

Umrechnung in der Software: `int32_t sample = (int32_t)(raw << 1) >> 8;`

## Verkabelung

Bewusst andere Pins als der DAC (GP16–18) und der ADC (GP26/27).

```
   INMP441                  Raspberry Pi Pico
  ┌─────────┐
  │  VDD  ●─┼──────────────● 3V3(OUT)  Pin 36
  │  GND  ●─┼──────┬───────● GND       Pin 13
  │  L/R  ●─┼──────┘
  │  SCK  ●─┼──────────────● GP10      Pin 14
  │  WS   ●─┼──────────────● GP11      Pin 15
  │  SD   ●─┼──────────────● GP12      Pin 16
  └─────────┘
```

| INMP441 | Pico     | Physischer Pin      |
| ------- | -------- | ------------------- |
| VDD     | 3V3(OUT) | 36                  |
| GND     | GND      | 13                  |
| L/R     | GND      | 13 (oder jeder GND) |
| SCK     | GP10     | 14                  |
| WS      | GP11     | 15                  |
| SD      | GP12     | 16                  |

Hinweise:

- **VDD nur an 3V3(OUT)**, niemals an VBUS oder VSYS (5 V).
- **L/R fest auf GND.** Offen gelassen ist der Kanalslot undefiniert.
- SCK und WS müssen **aufeinanderfolgende GPIOs** sein (WS = SCK + 1), weil beide per `side_set` gesetzt werden. SD kann ein beliebiger freier Pin sein.
- SD bekommt einen internen Pulldown (`gpio_pull_down`), da das INMP441 die Leitung außerhalb seines Slots hochohmig schaltet.
- Leitungen kurz halten (möglichst < 15 cm), GND-Draht neben SCK führen, räumlich getrennt von den DAC-Leitungen.
- Nur stromlos verkabeln. Vor dem ersten Einschalten 3V3 gegen GND messen: einige kΩ sind normal, wenige Ω wären ein Kurzschluss.

## Bauen

Voraussetzung: Pico SDK und ARM-Toolchain sind installiert.

```bash
export PICO_SDK_PATH=~/development/frontend_masterclass/abschlussprojekt/hardware/pico-sdk

# einmalig: dieselbe Datei wie im Vocoder-Hauptprojekt
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .

mkdir build && cd build
cmake ..
make -j 4
```

Ergebnis: `build/hello_mic.uf2`

## Flashen

1. BOOTSEL-Taste gedrückt halten und den Pico per USB anschließen.
2. `hello_mic.uf2` auf das erscheinende Laufwerk `RPI-RP2` kopieren.
3. Der Pico startet neu, nach 2 Sekunden beginnt die Ausgabe.

## Serielle Ausgabe ansehen

```bash
screen /dev/ttyACM0 115200
# oder
minicom -D /dev/ttyACM0
```

Beispielausgabe:

```
INMP441 Hello-World-Test gestartet (Samplerate ~16000 Hz).
Sprich oder klopf ans Mikro - min/max sollten sich bewegen.
min=-1834 max=1907 samples=3200
min=-152400 max=148912 samples=3200
```

## Was ist normal?

- **Stille:** min/max liegen eng um 0, im Bereich einiger Hundert bis weniger Tausend (bei einem Vollausschlag von ±8 388 608).
- **Sprechen oder Klopfen:** min/max schlagen deutlich aus.
- **`samples=`** liegt bei etwa 3200 pro 200 ms (16 000 Hz × 0,2 s).
- Die ersten ~85 ms nach dem Start liefert das INMP441 noch keine gültigen Daten.

## Fehlersuche

| Symptom                                                 | Wahrscheinliche Ursache                                                               |
| ------------------------------------------------------- | ------------------------------------------------------------------------------------- |
| min/max bleiben bei 0, auch beim Klopfen                | L/R nicht auf GND, SD falsch verkabelt, Mic ohne Versorgung                           |
| `samples=0`                                             | State Machine läuft nicht, PIO-Programm nicht geladen                                 |
| `samples` deutlich unter 3200                           | FIFO-Überlauf, falscher Taktteiler                                                    |
| min/max springen wild auf riesige Werte, auch in Stille | Bit-Ausrichtung falsch, fehlender Pulldown an SD, zu lange Leitungen                  |
| Werte reagieren, aber nur extrem schwach                | normal: Sprache landet grob bei −50 bis −60 dBFS. Im Vocoder per Shift-Faktor anheben |

Läuft der Test nicht, als Nächstes SCK, WS und SD mit einem Logikanalysator ansehen.

Übersprechtest: min/max in Stille einmal mit laufendem und einmal mit abgeschaltetem DAC vergleichen. Sind die Werte gleich, gibt es kein Übersprechen zwischen den I2S-Leitungen.

## Dateien

```
hello_mic_test/
├── CMakeLists.txt
├── README.md
└── src/
    ├── hello_mic.cpp   Setup der PIO, Ausleseschleife, serielle Ausgabe
    └── i2s_mic.pio     PIO-Programm: I2S-Empfänger (Master), 128 Takte/Frame
```

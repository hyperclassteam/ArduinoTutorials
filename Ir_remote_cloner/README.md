# Telecomandă IR cu învățare (Arduino Micro + 24LC08B)

Proiect Arduino care învață o comandă de la o telecomandă IR obișnuită, o salvează într-o memorie EEPROM externă (24LC08B) și o poate retransmite printr-un LED IR la apăsarea unui buton. Comanda rămâne memorată și după întreruperea alimentării.

## Funcționare

1. Apeși **LEARN (D10)** și îndrepți telecomanda originală către receptorul TSOP1838.
2. Apeși o tastă pe telecomanda originală.
3. Arduino înregistrează temporizările brute (raw) ale semnalului și le scrie în EEPROM.
4. Apeși **SEND (D8)** pentru a retransmite comanda prin LED-ul IR (38 kHz).
5. La pornire, comanda salvată este încărcată automat din EEPROM.

Mesajele de stare apar în Serial Monitor la **115200 baud**.

## Componente

- Arduino Micro R3 (ATmega32U4, logică 5 V)
- Receptor IR TSOP1838 (38 kHz)
- Memorie EEPROM 24LC08B (1 KB, I²C)
- LED emițător IR
- Tranzistor NPN (BC547, BC337, 2N2222 etc.)
- 2 butoane fără reținere: LEARN și SEND
- 2 × rezistențe 4,7 kΩ (pull-up I²C)
- 1 × rezistență 1 kΩ (baza tranzistorului)
- 1 × rezistență 100–220 Ω (în serie cu LED-ul IR)
- 2 × condensatoare ceramice 100 nF
- Breadboard și fire de legătură

## Biblioteci necesare

- [IRremote](https://github.com/Arduino-IRremote/Arduino-IRremote) (versiunea 4.x) — se instalează din Library Manager în Arduino IDE
- `Wire` (inclusă în Arduino IDE)

## Conexiuni

| Funcție | Pin Arduino Micro | Conectare |
|---|---|---|
| Receptor IR | D12 | OUT TSOP1838 |
| EEPROM SDA | D2 / SDA | Pin 5 24LC08B (+ 4,7 kΩ la 5V) |
| EEPROM SCL | D3 / SCL | Pin 6 24LC08B (+ 4,7 kΩ la 5V) |
| Buton LEARN | D10 | Celălalt contact la GND |
| Buton SEND | D8 | Celălalt contact la GND |
| Emisie IR | D6 | Prin 1 kΩ la baza NPN |
| Alimentare | 5V | VCC EEPROM, VCC receptor, anod LED IR |
| Masă | GND | Toate masele în comun |

Butoanele folosesc `INPUT_PULLUP` (fără rezistențe externe; apăsat = LOW).

### 24LC08B (DIP-8, crestătura în sus)

```text
          ┌───────┐
     NC 1 │●      │ 8 VCC → 5V
     NC 2 │       │ 7 WP  → GND
     NC 3 │       │ 6 SCL → D3
    GND 4 │       │ 5 SDA → D2
          └───────┘
```

- WP (pin 7) la GND permite scrierea.
- Condensator 100 nF între pinii 8 și 4, cât mai aproape de cip.
- Memoria răspunde la adresele I²C **0x50–0x53** (4 blocuri de 256 octeți).

### TSOP1838

- OUT → D12, VCC → 5V, GND → GND
- Condensator 100 nF între VCC și GND, lângă receptor.
- Verifică pinout-ul în fișa tehnică — diferă între variante.

### Etaj de emisie IR

```text
Arduino D6 ──[1 kΩ]── B (bază) NPN
                       C (colector) ── catod LED IR
                       E (emitor)   ── GND

5V ──[100–220 Ω]── anod LED IR
```

Nu conecta LED-ul IR direct la D6. Verifică ordinea pinilor B/C/E pentru modelul tău de tranzistor.

## Format date în EEPROM

| Adresă | Conținut |
|---|---|
| 0 | Octet `MAGIC` (0xA7) — marchează o comandă validă |
| 1–2 | Număr de temporizări (little-endian) |
| 3… | Temporizări în µs, câte 2 octeți (little-endian) |
| 1023 | Rezervat pentru testul EEPROM de la pornire |

`MAGIC` este șters la începutul salvării și scris la final, astfel încât o salvare întreruptă nu lasă o comandă incompletă marcată ca validă.

## Mesaje Serial

| Mesaj | Semnificație |
|---|---|
| `EEPROM OK` | Testul de scriere/citire a reușit |
| `EROARE EEPROM: verifica conexiunile I2C` | Memoria nu răspunde — verifică SDA/SCL, pull-up, alimentare |
| `Comanda salvata: <protocol> ...` | Comanda a fost învățată și salvată |
| `Semnal prea lung, nu poate fi salvat.` | Cadrul depășește 200 de temporizări |
| `Nu exista comanda memorata.` | Nu s-a învățat încă nicio comandă |
| `Comanda trimisa.` | Comanda a fost retransmisă |

## Limitări

- Memorează o singură comandă.
- Emite doar la 38 kHz.
- Maxim 200 de temporizări per cadru (limită de RAM pe ATmega32U4).
- Cadrele de repetare sunt ignorate; unele telecomenzi (ex. aer condiționat cu cadre lungi) pot să nu fie compatibile.

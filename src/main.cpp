#include <Arduino.h>
#include <SPI.h>

// Подключение к ESP32-S3
#define CC1101_SCK   13
#define CC1101_MISO  11
#define CC1101_MOSI  12
#define CC1101_CS    46
#define CC1101_GDO0  9
#define CC1101_GDO2  10

SPIClass hspi(HSPI);

// ВЧ-команды CC1101
#define CC1101_SRES    0x30
#define CC1101_SRX     0x34
#define CC1101_SIDLE   0x36
#define CC1101_SFRX    0x3A

// Регистры Flipper Zero для 2-FSK (238kHz BW, Async)
const uint8_t flipper_2fsk_preset[][2] = {
    {0x00, 0x0D}, // IOCFG2: GDO2 High Impedance
    {0x02, 0x0D}, // IOCFG0: GDO0 Serial Data Output (Async)
    {0x0B, 0x06}, // FSCTRL1
    {0x10, 0xC8}, // MDMCFG4: 270kHz Filter BW
    {0x11, 0x93}, // MDMCFG3
    {0x12, 0x02}, // MDMCFG2: 2-FSK, no sync word (RAW Async)
    {0x15, 0x34}, // DEVIATN: ~20kHz deviation
    {0x18, 0x18}, // MCSM0
    {0x19, 0x16}, // FOCCFG
    {0x23, 0xE9}, // FSCAL3
    {0x24, 0x2A}, // FSCAL2
    {0x25, 0x00}  // FSCAL1
};

// Кольцевой буфер для импульсов (ISR)
volatile uint32_t last_edge_time = 0;
volatile uint32_t pulse_buffer[256];
volatile uint8_t head = 0;
volatile uint8_t tail = 0;

void IRAM_ATTR gdo0_isr() {
    uint32_t now = micros();
    uint32_t duration = now - last_edge_time;
    last_edge_time = now;
    
    // Фильтруем слишком короткие помехи (<100us)
    if (duration > 100 && duration < 30000) {
        uint8_t next_head = (head + 1) % 256;
        if (next_head != tail) {
            pulse_buffer[head] = duration;
            head = next_head;
        }
    }
}

void writeRegister(uint8_t reg, uint8_t val) {
    hspi.beginTransaction(SPISettings(5000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CC1101_CS, LOW);
    hspi.transfer(reg);
    hspi.transfer(val);
    digitalWrite(CC1101_CS, HIGH);
    hspi.endTransaction();
}

void sendStrobe(uint8_t strobe) {
    hspi.beginTransaction(SPISettings(5000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CC1101_CS, LOW);
    hspi.transfer(strobe);
    digitalWrite(CC1101_CS, HIGH);
    hspi.endTransaction();
}

void setFrequency(uint32_t freq) {
    uint32_t reg_freq = (uint32_t)((double)freq * 65536.0 / 26000000.0);
    writeRegister(0x0D, (reg_freq >> 16) & 0xFF);
    writeRegister(0x0E, (reg_freq >> 8) & 0xFF);
    writeRegister(0x0F, reg_freq & 0xFF);
}

void setup() {
    Serial.begin(115200);
    
    pinMode(CC1101_CS, OUTPUT);
    digitalWrite(CC1101_CS, HIGH);
    pinMode(CC1101_GDO0, INPUT);

    hspi.begin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);

    // Сброс и загрузка пресета Flipper
    sendStrobe(CC1101_SRES);
    delay(10);
    sendStrobe(CC1101_SIDLE);

    for (auto &reg : flipper_2fsk_preset) {
        writeRegister(reg[0], reg[1]);
    }

    // Настраиваем на 433.92 МГц (или 868.35 для StarLine)
    setFrequency(433920000);

    sendStrobe(CC1101_SFRX);
    sendStrobe(CC1101_SRX);

    // Вешаем асинхронный перехват
    attachInterrupt(digitalPinToInterrupt(CC1101_GDO0), gdo0_isr, CHANGE);

    Serial.println(">> Flipper CC1101 Async Engine Started! Press your Remote...");
}

void loop() {
    // Вычитываем импульсы из буфера и выводим сырые длительности
    while (tail != head) {
        uint32_t pulse = pulse_buffer[tail];
        tail = (tail + 1) % 256;
        
        Serial.printf("%d, ", pulse);
    }
}

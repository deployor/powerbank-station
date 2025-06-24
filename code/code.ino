#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>
#include <IRremote.h>
#include <EEPROM.h>

#define SS_PIN 10
#define RST_PIN 9
#define PIEZO_PIN 8
#define VIBRATION_PIN 7
#define IR_RECEIVE_PIN 2

LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522 rfid(SS_PIN, RST_PIN);
IRrecv irrecv(IR_RECEIVE_PIN);
decode_results irResults;

const int ADDR_MAGIC_BYTE = 0;
const int ADDR_MASTER_UID = 2;
const int ADDR_CURRENT_STATE = 50;
const int ADDR_AUTHORIZED_CARDS = 100;
const int ADDR_TRANSACTION_LOG = 400;
const int MAX_CARDS = 10;
const int MAX_LOG_ENTRIES = 5;
const byte MAGIC_BYTE = 0xAB;

bool powerbankAusgeliehen = false;
String aktuelleAusleiherUID = "";
bool imWartungsmodus = false;
unsigned long lastActivityTime = 0;
unsigned long lastVibrationTime = 0;
bool vibrationDetected = false;

void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();
  irrecv.enableIRIn();

  lcd.init();
  lcd.backlight();
  
  pinMode(PIEZO_PIN, OUTPUT);
  pinMode(VIBRATION_PIN, INPUT_PULLUP);

  if (EEPROM.read(ADDR_MAGIC_BYTE) != MAGIC_BYTE) {
    initialisiereEEPROM();
  } else {
    ladeZustandAusEEPROM();
  }
  
  lcd.clear();
  lcd.print("System Startet...");
  ton(1200, 100);
  delay(100);
  ton(1400, 100);
  delay(1500);
  updateDisplayNormal();
  lastActivityTime = millis();
}

void loop() {
  if (irrecv.decode(&irResults)) {
    lastActivityTime = millis();
    if(imWartungsmodus) {
      handleIRInput(irResults.value);
    }
    irrecv.resume();
  }

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    lastActivityTime = millis();
    String geleseneUID = getCardUID();
    
    if (geleseneUID == getMasterUID()) {
      toggleWartungsmodus();
    } else {
      if (!imWartungsmodus) {
        handleKartenAktionNormal(geleseneUID);
      }
    }
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  if (millis() - lastActivityTime > 30000 && !imWartungsmodus && !powerbankAusgeliehen) {
    lcd.noBacklight();
  }
    if (powerbankAusgeliehen && digitalRead(VIBRATION_PIN) == LOW) {
        if (!vibrationDetected) { // Only trigger once
            vibrationDetected = true;
            lcd.clear();
            lcd.print("Entnahme OK!");
            lcd.setCursor(0, 1);
            lcd.print("Gute Reise!");
            delay(2000);
            updateDisplayNormal();
        }
  } else if (digitalRead(VIBRATION_PIN) == HIGH) {
    vibrationDetected = false;
  }
}

void saveStateToEEPROM() {
    EEPROM.update(ADDR_CURRENT_STATE, powerbankAusgeliehen);
    if (powerbankAusgeliehen) {
        writeStringToEEPROM(ADDR_CURRENT_STATE + 1, aktuelleAusleiherUID, 25);
    } else {
        writeStringToEEPROM(ADDR_CURRENT_STATE + 1, "", 25);
    }
}

void updateDisplayNormal() {
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    if (powerbankAusgeliehen) {
        lcd.print("Ausgeliehen an:");
        lcd.setCursor(0, 1);
        lcd.print(aktuelleAusleiherUID);
    } else {
        lcd.print("Powerbank bereit");
        lcd.setCursor(0, 1);
        lcd.print("Karte vorhalten");
    }
}

void toggleWartungsmodus() {
    imWartungsmodus = !imWartungsmodus;
    if (imWartungsmodus) {
        ton(1000, 100); delay(100); ton(1000, 100);
        displayWartungsmenu(0);
    } else {
        ton(800, 100);
        updateDisplayNormal();
    }
}

void handleKartenAktionNormal(String uid) {
    if (!isCardAuthorized(uid)) {
        lcd.clear();
        lcd.print("Karte unbekannt!");
        ton(400, 500);
        delay(2000);
        updateDisplayNormal();
        return;
    }
    if (!powerbankAusgeliehen) {
        powerbankAusgeliehen = true;
        aktuelleAusleiherUID = uid;
        saveStateToEEPROM();
        logTransaction("Ausleihe", uid);
        lcd.clear();
        lcd.print("Hallo! Nimm die");
        lcd.setCursor(0, 1);
        lcd.print("Powerbank jetzt.");
        ton(800, 100); delay(100); ton(1200, 150);
        delay(2000);
    } else {
        if (uid == aktuelleAusleiherUID) {
            powerbankAusgeliehen = false;
            aktuelleAusleiherUID = "";
            saveStateToEEPROM();
            logTransaction("Rueckgabe", uid);
            lcd.clear();
            lcd.print("Danke fuer die");
            lcd.setCursor(0, 1);
            lcd.print("Rueckgabe!");
            ton(1200, 100); delay(100); ton(800, 150);
            delay(2000);
        } else {
            lcd.clear();
            lcd.print("Falsche Karte!");
            lcd.setCursor(0, 1);
            lcd.print("Bereits belegt.");
            ton(400, 200); delay(100); ton(400, 200);
            delay(2000);
        }
    }
    updateDisplayNormal();
}

const char* mainMenu[] = {"Karten verwalten", "Transaktionen", "System Reset", "Verlassen"};
int mainMenuPunkt = 0;

void displayWartungsmenu(int menuIndex) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("> " + String(mainMenu[menuIndex]));
    lcd.setCursor(0, 1);
    if(menuIndex + 1 < sizeof(mainMenu)/sizeof(char*)) {
      lcd.print("  " + String(mainMenu[menuIndex + 1]));
    }
}

void handleIRInput(long irCode) {
    switch (irCode) {
        case 0xFF629D: // UP
            mainMenuPunkt = (mainMenuPunkt == 0) ? (sizeof(mainMenu)/sizeof(char*)-1) : mainMenuPunkt - 1;
            break;
        case 0xFFA857: // DOWN
            mainMenuPunkt = (mainMenuPunkt + 1) % (sizeof(mainMenu)/sizeof(char*));
            break;
        case 0xFFC23D: // OK
            executeMenuAction();
            return;
    }
    ton(600, 50);
    displayWartungsmenu(mainMenuPunkt);
}

void executeMenuAction() {
    ton(800, 100);
    switch (mainMenuPunkt) {
        case 0: menuKartenVerwalten(); break;
        case 1: menuTransaktionenAnzeigen(); break;
        case 2: menuSystemReset(); break;
        case 3: toggleWartungsmodus(); break;
    }
    if(imWartungsmodus) displayWartungsmenu(mainMenuPunkt);
}

void menuKartenVerwalten() {
    const char* kartenMenu[] = {"Karte hinzufuegen", "Karte loeschen", "Alle loeschen", "Zurueck"};
    int kartenMenuPunkt = 0;
    
    while(true) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("> " + String(kartenMenu[kartenMenuPunkt]));
        
        while(true) {
            if (irrecv.decode(&irResults)) {
                switch (irResults.value) {
                    case 0xFF629D: // UP
                        kartenMenuPunkt = (kartenMenuPunkt == 0) ? 3 : kartenMenuPunkt - 1;
                        ton(600, 50);
                        break;
                    case 0xFFA857: // DOWN
                        kartenMenuPunkt = (kartenMenuPunkt + 1) % 4;
                        ton(600, 50);
                        break;
                    case 0xFFC23D: // OK
                        ton(800, 100);
                        switch (kartenMenuPunkt) {
                            case 0: karteHinzufuegen(); break;
                            case 1: karteLoeschen(); break;
                            case 2: alleKartenLoeschen(); break;
                            case 3: return;
                        }
                        break;
                }
                irrecv.resume();
                break;
            }
        }
    }
}

void menuTransaktionenAnzeigen() {
    lcd.clear();
    lcd.print("Lese Log-Daten...");
    String log[MAX_LOG_ENTRIES];
    readLog(log);
    
    int logIndex = 0;
    while(true) {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Log (" + String(logIndex+1) + "/" + String(MAX_LOG_ENTRIES) + ")");
      lcd.setCursor(0,1);
      lcd.print(log[logIndex]);
      
      long startTime = millis();
      while(millis() - startTime < 5000) {
         if (irrecv.decode(&irResults)) {
            if(irResults.value == 0xFFC23D) {
                ton(800, 100);
                return;
            }
            irrecv.resume();
         }
      }
      logIndex = (logIndex + 1) % MAX_LOG_ENTRIES;
    }
}

void menuSystemReset() {
    lcd.clear();
    lcd.print("Wirklich Reset?");
    lcd.setCursor(0, 1);
    lcd.print("OK = JA");
    
    long startTime = millis();
    while (millis() - startTime < 5000) {
        if (irrecv.decode(&irResults)) {
            if(irResults.value == 0xFFC23D) {
                initialisiereEEPROM();
                lcd.clear();
                lcd.print("System Reset!");
                ton(1200, 100); delay(100); ton(600, 300);
                delay(2000);
                return;
            }
            irrecv.resume();
        }
    }
}

void karteHinzufuegen() {
    lcd.clear();
    lcd.print("Neue Karte zum");
    lcd.setCursor(0, 1);
    lcd.print("Anlernen halten");

    long startTime = millis();
    while (millis() - startTime < 10000) {
        if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
            String neueUID = getCardUID();
            if (isCardAuthorized(neueUID) || neueUID == getMasterUID()) {
                lcd.clear(); lcd.print("Karte bekannt!"); ton(400, 300);
            } else {
                if(addCard(neueUID)) {
                    lcd.clear(); lcd.print("Karte hinzu!"); ton(1200, 200);
                } else {
                    lcd.clear(); lcd.print("Speicher voll!"); ton(400, 300);
                }
            }
            delay(2000);
            rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
            return;
        }
    }
    lcd.clear(); lcd.print("Timeout!"); delay(2000);
}

void karteLoeschen() {
    lcd.clear();
    lcd.print("Karte zum");
    lcd.setCursor(0, 1);
    lcd.print("Loeschen halten");

    long startTime = millis();
    while (millis() - startTime < 10000) {
        if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
            String uidToDelete = getCardUID();
            if (deleteCard(uidToDelete)) {
                lcd.clear(); lcd.print("Karte geloescht!"); ton(1200, 200);
            } else {
                lcd.clear(); lcd.print("Karte nicht gef.!"); ton(400, 300);
            }
            delay(2000);
            rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
            return;
        }
    }
    lcd.clear(); lcd.print("Timeout!"); delay(2000);
}

void alleKartenLoeschen() {
    for (int i = 0; i < MAX_CARDS; i++) {
        writeStringToEEPROM(ADDR_AUTHORIZED_CARDS + (i * 25), "", 25);
    }
    lcd.clear();
    lcd.print("Alle Karten");
    lcd.setCursor(0,1);
    lcd.print("geloescht!");
    ton(1200, 100); delay(100); ton(800, 100);
    delay(2000);
}

void initialisiereEEPROM() {
    lcd.clear();
    lcd.print("Initialisiere...");
    lcd.setCursor(0,1);
    lcd.print("Bitte warten.");
    
    EEPROM.write(ADDR_MAGIC_BYTE, MAGIC_BYTE);
    
    lcd.clear();
    lcd.print("Master-Karte");
    lcd.setCursor(0,1);
    lcd.print("vorhalten!");
    String masterUID = "";
    while(masterUID == "") {
      if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        masterUID = getCardUID();
        writeStringToEEPROM(ADDR_MASTER_UID, masterUID, 25);
        rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
      }
    }
    ton(1400, 200);
    
    alleKartenLoeschen();
    for (int i=0; i < MAX_LOG_ENTRIES; i++) {
      writeStringToEEPROM(ADDR_TRANSACTION_LOG + (i * 50), "", 50);
    }    
    powerbankAusgeliehen = false;
    aktuelleAusleiherUID = "";
    saveStateToEEPROM();

    lcd.clear();
    lcd.print("Setup fertig!");
    delay(2000);
}

void ladeZustandAusEEPROM() {
    Serial.println("Lade Zustand aus EEPROM.");
    powerbankAusgeliehen = EEPROM.read(ADDR_CURRENT_STATE);
    if (powerbankAusgeliehen) {
        aktuelleAusleiherUID = readStringFromEEPROM(ADDR_CURRENT_STATE + 1, 25);
    }
}

String getMasterUID() {
    return readStringFromEEPROM(ADDR_MASTER_UID, 25);
}

bool isCardAuthorized(String uid) {
    for (int i = 0; i < MAX_CARDS; i++) {
        String storedUID = readStringFromEEPROM(ADDR_AUTHORIZED_CARDS + (i * 25), 25);
        if (uid == storedUID) return true;
    }
    return false;
}

bool addCard(String uid) {
    for (int i = 0; i < MAX_CARDS; i++) {
        String storedUID = readStringFromEEPROM(ADDR_AUTHORIZED_CARDS + (i * 25), 25);
        if (storedUID == "") {
            writeStringToEEPROM(ADDR_AUTHORIZED_CARDS + (i * 25), uid, 25);
            return true;
        }
    }
    return false;
}

bool deleteCard(String uid) {
    for (int i = 0; i < MAX_CARDS; i++) {
        String storedUID = readStringFromEEPROM(ADDR_AUTHORIZED_CARDS + (i * 25), 25);
        if (uid == storedUID) {
            writeStringToEEPROM(ADDR_AUTHORIZED_CARDS + (i * 25), "", 25);
            return true;
        }
    }
    return false;
}

void logTransaction(String type, String uid) {
    for(int i = MAX_LOG_ENTRIES - 1; i > 0; i--) {
        String oldLog = readStringFromEEPROM(ADDR_TRANSACTION_LOG + ((i-1) * 50), 50);
        writeStringToEEPROM(ADDR_TRANSACTION_LOG + (i * 50), oldLog, 50);
    }
    String newLog = type + ":" + uid;
    writeStringToEEPROM(ADDR_TRANSACTION_LOG, newLog, 50);
}

void readLog(String logArray[]) {
  for(int i=0; i<MAX_LOG_ENTRIES; i++) {
    logArray[i] = readStringFromEEPROM(ADDR_TRANSACTION_LOG + (i * 50), 50);
    if(logArray[i] == "") logArray[i] = "---";
  }
}

String getCardUID() {
  String content = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    content.concat(String(rfid.uid.uidByte[i] < 0x10 ? " 0" : " "));
    content.concat(String(rfid.uid.uidByte[i], HEX));
  }
  content.toUpperCase();
  return content.substring(1);
}

void writeStringToEEPROM(int addr, const String &str, int maxLength) {
  int len = str.length();
  for (int i = 0; i < maxLength; i++) {
    if(i < len) {
      EEPROM.update(addr + i, str[i]);
    } else {
      EEPROM.update(addr + i, 0);
    }
  }
}

String readStringFromEEPROM(int addr, int maxLength) {
  String str = "";
  for (int i = 0; i < maxLength; i++) {
    char c = EEPROM.read(addr + i);
    if (c == 0) break;
    str += c;
  }
  return str;
}

void ton(int frequenz, int dauer) {
  tone(PIEZO_PIN, frequenz, dauer);
}
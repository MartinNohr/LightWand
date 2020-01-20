/*
  Digital Light Wand + SD + LCD + Arduino MEGA - V MRR-3.0 (WS2812 RGB LED Light Strip)
  by Michael Ross 2014
  Based on original code by myself in 2010 then enhanced by Is0-Mick in 2012

  The Digital Light Wand is for use in specialized Light Painting Photography
  Applications.

  This code is totally rewritten using code that IsO-Mick created made to specifically
  support the WS2812 RGB LED strips running with an SD Card, an LCD Display, and the
  Arduino Mega 2560 Microprocessor board.

  The functionality that is included in this code is as follows:

  Menu System
  1 - File select
  2 - Brightness
  3 - Initial Delay
  4 - Frame Delay
  5 - Repeat Times (The number of times to repeat the current file playback)
  6 - Repeat Delay (if you want a delay between repeated files)

  This code supports direct reading of a 24bit Windows BMP from the SD card.
  BMP images must be rotated 90 degrees clockwise and the width of the image should match the
  number of pixels you have on your LED strip.  The bottom of the tool will be the INPUT
  end of the strips where the Arduino is connected and will be the left side of the input
  BMP image.

  Mick also added a Gamma Table from adafruit code which gives better conversion of 24 bit to
  21 bit coloring.

*/

// Library initialization
#include <Adafruit_NeoPixel.h>           // Library for the WS2812 Neopixel Strip
#include <SD.h>                          // Library for the SD Card
#include <LiquidCrystal.h>               // Library for the LCD Display
#include <SPI.h>                         // Library for the SPI Interface
#include <avr/eeprom.h>
#include <timer.h>

// Pin assignments for the Arduino (Make changes to these if you use different Pins)
#define BACKLIGHT 10                      // Pin used for the LCD Backlight
#define SDssPin 53                        // SD card CS pin
int NPPin = 31;                           // Data Pin for the NeoPixel LED Strip
int AuxButton = 44;                       // Aux Select Button Pin
int AuxButtonGND = 45;                    // Aux Select Button Ground Pin
int g = 0;                                // Variable for the Green Value
int b = 0;                                // Variable for the Blue Value
int r = 0;                                // Variable for the Red Value

// Initial Variable declarations and assignments (Make changes to these if you want to change defaults)
int stripLength = 144;                    // Set the number of LEDs the LED Strip
int frameHold = 15;                       // default for the frame delay 
int lastMenuItem = -1;                    // check to see if we need to redraw menu
int menuItem = 1;                         // Variable for current main menu selection
int initDelay = 0;                        // Variable for delay between button press and start of light sequence, in seconds
int repeat = 0;                           // Variable to select auto repeat (until select button is pressed again)
int repeatDelay = 0;                      // Variable for delay between repeats
int updateMode = 0;                       // Variable to keep track of update Modes
int repeatTimes = 1;                      // Variable to keep track of number of repeats
int nStripBrightness = 50;                      // Variable and default for the Brightness of the strip
bool bGammaCorrection = true;             // set to use the gamma table
bool bAutoLoadSettings = false;           // set to automatically load saved settings

// Other program variable declarations, assignments, and initializations
byte x;
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);      // Init the LCD

// Declaring the two LED Strips and pin assignments to each 
Adafruit_NeoPixel strip = Adafruit_NeoPixel(stripLength, NPPin, NEO_GRB + NEO_KHZ800);

// Variable assignments for the Keypad
int adc_key_val[5] = { 30, 170, 390, 600, 800 };
enum keyvals { KEYNONE = -1, KEYRIGHT = 0, KEYUP, KEYDOWN, KEYLEFT, KEYSELECT, NUM_KEYS };
int adc_key_in;
int key = -1;
int oldkey = -1;

// SD Card Variables and assignments
File root;
File dataFile;
String m_CurrentFilename = "";
int m_FileIndex = 0;
int m_NumberOfFiles = 0;
String m_FileNames[200];
//long stripBuffer[STRIP_LENGTH];

// keyboard speeds up when held down longer
#define KEYWAITPAUSE 250
int kbdWaitTime = KEYWAITPAUSE;
int kbdPause = 0;
// set to zero when no key down, and watch time when one is pressed
unsigned long startKeyDown = 0;

// built-in tests
enum e_tests { mtDots = 0, mtTwoDots, MAXTEST };
const char* testStrings[] = {
    "Running Dots",
    "Opposite Dots",
};
int nTestNumber = 0;

// menu strings
enum e_menuitem {
    mSelectFile = 1,
    mStripBrightness,
    mInitDelay,
    mFrameHoldTime,
    mRepeatTimes,
    mRepeatDelay,
    mGammaCorrection,
    mStripLength,
    mTest,
    mBackLightBrightness,
    mBackLightTimer,
    mAutoLoadSettings,
    mSavedSettings,
    MAXMENU = mSavedSettings
};
const char* menuStrings[] = {
    "File",
    "Brightness",
    "Init Delay",
    "Frame Time",
    "Repeat Times",
    "Repeat Delay",
    "Gamma Correct",
    "Strip Length",
    "Test Patterns",
    "LCD Brightness",
    "LCD Timeout",
    "Autoload Sets",
    "Saved Settings",
};

// storage for special character
byte chZeroPattern[8];

int nMaxBackLight = 75;        // maximum backlight to use in %
int nBackLightSeconds = 5;      // how long to leave the backlight on before dimming
bool bBackLightOn = false;      // used by backlight timer to indicate that backlight is on
bool bTurnOnBacklight = true;   // set to turn the backlight on, safer than calling the BackLightControl code
// timers to run things
auto backLightTimer = timer_create_default();
// this gets called every second/TIMERSTEPS
#define TIMERSTEPS 10
bool BackLightControl(void*)
{
    static int light;
    static int fade;
    static int timer;
    // change % to 0-255
    int abslight = 255 * nMaxBackLight / 100;
    if (bTurnOnBacklight) {
        timer = nBackLightSeconds * TIMERSTEPS;
        bBackLightOn = true;
        bTurnOnBacklight = false;
    }
    if (timer > 1) {
        light = abslight;
    }
    else if (timer == 1) {
        // start the fade timer
        fade = abslight / TIMERSTEPS;
    }
    if (bBackLightOn)
        analogWrite(BACKLIGHT, light);
    if (timer > 0)
        --timer;
    if (fade) {
        light -= fade;
        if (light < 0) {
            light = 0;
            bBackLightOn = false;
            analogWrite(BACKLIGHT, light);
        }
    }
    return true;    // repeat true
}

// Setup loop to get everything ready.  This is only run once at power on or reset
void setup() {
    Serial.begin(115200);
    Serial.println("Starting setup");

    pinMode(AuxButton, INPUT);
    digitalWrite(AuxButton, HIGH);
    pinMode(AuxButtonGND, OUTPUT);
    digitalWrite(AuxButtonGND, LOW);

    setupLEDs();
    setupLCDdisplay();
    setupSDcard();
    // turn on the keyboard reader
    digitalWrite(LED_BUILTIN, HIGH);
    Serial.println("Finishing setup");
    SaveSettings(false, true);
    backLightTimer.every(1000 / TIMERSTEPS, BackLightControl);
}

// create a character by filling blocks to indicate how far down the menu we are
void createZeroCharacter()
{
    memset(chZeroPattern, 0, sizeof chZeroPattern);
    for (int menu = 0; menu < menuItem; ++menu) {
        chZeroPattern[menu % 7] |= (1 << (4 - menu / 7));
    }
    lcd.createChar(0, chZeroPattern);
}

// The Main Loop for the program starts here... 
// This will loop endlessly looking for a key press to perform a function
void loop() {
    backLightTimer.tick();
    if (bBackLightOn && menuItem != lastMenuItem) {
        createZeroCharacter();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.write((byte)0);
        lcd.print(menuStrings[menuItem - 1]);
        if (menuItem == mSelectFile) {
            lcd.print(" " + String(m_FileIndex + 1) + "/" + String(m_NumberOfFiles));
        }
        lcd.setCursor(0, 1);
        switch (menuItem) {
        case mSelectFile:
            lcd.print(m_CurrentFilename);
            break;
        case mStripBrightness:
            lcd.print(nStripBrightness);
            if (nStripBrightness == 100) {
                lcd.setCursor(3, 1);
            }
            else {
                lcd.setCursor(2, 1);
            }
            lcd.print("%");
            break;
        case mInitDelay:
            lcd.print(String(initDelay) + " Seconds");
            break;
        case mFrameHoldTime:
            lcd.print(String(frameHold) + " mSec");
            break;
        case mRepeatTimes:
            lcd.print(repeatTimes);
            break;
        case mRepeatDelay:
            lcd.print(String(repeatDelay) + " mSec");
            break;
        case mTest:
            lcd.print(testStrings[nTestNumber]);
            break;
        case mBackLightBrightness:
            lcd.print(String(nMaxBackLight) + " %");
            break;
        case mBackLightTimer:
            lcd.print(String(nBackLightSeconds) + " Seconds");
            break;
        case mSavedSettings:
            lcd.print("<=Load >=Save");
            break;
        case mGammaCorrection:
            lcd.print(bGammaCorrection ? "ON" : "OFF");
            break;
        case mStripLength:
            lcd.print(stripLength);
            lcd.print(" pixels");
            break;
        case mAutoLoadSettings:
            lcd.print(bAutoLoadSettings ? "ON" : "OFF");
            break;
        }
        lastMenuItem = menuItem;
    }
    bool oldBackLightOn = bBackLightOn;
    int keypress = ReadKeypad();

    if (keypress != KEYNONE) {
        if (!oldBackLightOn) {
            // just eat the key if the light was off
            // wait for release
            while (ReadKeypad() != -1)
                ;
            return;
        }
    }

    if ((keypress == KEYSELECT) || (digitalRead(AuxButton) == LOW)) {    // The select key was pressed
        if (menuItem == mTest) {
            lcd.setCursor(0, 0);
            lcd.print("Running Test... ");
            lcd.setCursor(0, 1);
            lcd.print(testStrings[nTestNumber]);
            delay(initDelay * 1000);
            for (int x = repeatTimes; x > 0; x--) {
                RunTest();
                if (x > 1) {
                    delay(repeatDelay);
                }
            }
            ClearStrip();
        }
        else {
            lcd.setCursor(0, 0);
            lcd.print("Displaying      ");
            lcd.setCursor(0, 1);
            lcd.print(m_CurrentFilename);
            delay(initDelay * 1000);
            for (int x = repeatTimes; x > 0; x--) {
                Serial.println("sendfile");
                SendFile(m_CurrentFilename);
                if (x > 1) {
                    delay(repeatDelay);
                }
            }
            ClearStrip();
        }
        lastMenuItem = -1;  // show the menu again
    }
    if (keypress == KEYRIGHT) {                    // The Right Key was Pressed
        // redraw
        lastMenuItem = -1;
        // redid this as if/else if because switch was crashing
        if (menuItem == mSelectFile) {
            if (m_FileIndex < m_NumberOfFiles - 1) {
                m_FileIndex++;
            }
            else {
                m_FileIndex = 0;                // On the last file so wrap round to the first file
            }
            DisplayCurrentFilename();
        }
        else if (menuItem == mStripBrightness) {
            if (nStripBrightness < 100) {
                ++nStripBrightness;
            }
        }
        else if (menuItem == mInitDelay) {
            ++initDelay;
        }
        else if (menuItem == mFrameHoldTime) {
            frameHold += 1;
        }
        else if (menuItem == mRepeatTimes) {
            repeatTimes += 1;
        }
        else if (menuItem == mRepeatDelay) {
            repeatDelay += 100;
        }
        else if (menuItem == mTest) {
            if (nTestNumber < MAXTEST - 1)
                ++nTestNumber;
        }
        else if (menuItem == mBackLightBrightness) {
            if (nMaxBackLight < 100)
                ++nMaxBackLight;
        }
        else if (menuItem == mBackLightTimer) {
            ++nBackLightSeconds;
        }
        else if (menuItem == mSavedSettings) {
            SaveSettings(true, false);
        }
        else if (menuItem == mGammaCorrection) {
            bGammaCorrection = !bGammaCorrection;
        }
        else if (menuItem == mStripLength) {
            strip.updateLength(++stripLength);
        }
        else if (menuItem == mAutoLoadSettings) {
            bAutoLoadSettings = !bAutoLoadSettings;
        }
    }

    if (keypress == KEYLEFT) {                    // The Left Key was Pressed
        // redraw
        lastMenuItem = -1;

        if (menuItem == mSelectFile) {
            if (m_FileIndex > 0) {
                m_FileIndex--;
            }
            else {
                m_FileIndex = m_NumberOfFiles - 1;    // On the last file so wrap round to the first file
            }
            DisplayCurrentFilename();
        }
        else if (menuItem == mStripBrightness) {
            if (nStripBrightness > 1) {
                --nStripBrightness;
            }
        }
        else if (menuItem == mInitDelay) {
            if (initDelay > 0) {
                --initDelay;
            }
        }
        else if (menuItem == mFrameHoldTime) {
            if (frameHold > 0) {
                frameHold -= 1;
            }
        }
        else if (menuItem == mRepeatTimes) {
            if (repeatTimes > 1) {
                repeatTimes -= 1;
            }
        }
        else if (menuItem == mRepeatDelay) {
            if (repeatDelay > 0) {
                repeatDelay -= 100;
            }
        }
        else if (menuItem == mTest) {
            if (nTestNumber)
                --nTestNumber;
        }
        else if (menuItem == mBackLightBrightness) {
            if (nMaxBackLight > 5)
                --nMaxBackLight;
        }
        else if (menuItem == mBackLightTimer) {
            if (nBackLightSeconds > 1)
                --nBackLightSeconds;
        }
        else if (menuItem == mSavedSettings) {
            // load the settings
            SaveSettings(false, false);
        }
        else if (menuItem == mGammaCorrection) {
            bGammaCorrection = !bGammaCorrection;
        }
        else if (menuItem == mStripLength) {
            if (stripLength > 1)
                strip.updateLength(--stripLength);
        }
        else if (menuItem == mAutoLoadSettings) {
            bAutoLoadSettings = !bAutoLoadSettings;
        }
    }

    if ((keypress == KEYUP)) {                 // The up key was pressed
        if (menuItem == 1) {
            menuItem = MAXMENU;
        }
        else {
            menuItem -= 1;
        }
    }
    if ((keypress == KEYDOWN)) {                 // The down key was pressed
        if (menuItem == MAXMENU) {
            menuItem = 1;
        }
        else {
            menuItem += 1;
        }
    }
    // wait a bit between keypresses
    if (keypress == KEYNONE) {
        // no key is pressed, reset the timer
        startKeyDown = 0;
        // and the keypause
        kbdWaitTime = KEYWAITPAUSE;
    }
    else {
        // a key is down
        // remember the time it was pressed
        if (startKeyDown == 0) {
            startKeyDown = millis();
        }
        // calcualate how long to wait
        unsigned long now = millis();
        if (now > startKeyDown + 2000)
            kbdWaitTime = KEYWAITPAUSE / 4;
        if (now > startKeyDown + 4000)
            kbdWaitTime = KEYWAITPAUSE / 10;
        // do the prescribed wait
        delay(kbdWaitTime);
    }
}

// save some settings in the eeprom
// if autoload is true, check the first flag, and load the rest if it is true
void SaveSettings(bool save, bool autoload)
{
    void* where = (void*)NULL;
    struct saveValues {
        void* val;
        int size;
    };
    const saveValues valueList[] = {
        {&bAutoLoadSettings, sizeof bAutoLoadSettings},
        {&nStripBrightness, sizeof nStripBrightness},
        {&frameHold, sizeof frameHold},
        {&initDelay, sizeof initDelay},
        {&repeat, sizeof repeat},
        {&repeatTimes, sizeof repeatTimes},
        {&repeatDelay, sizeof repeatDelay},
        {&bGammaCorrection, sizeof bGammaCorrection},
        {&stripLength, sizeof stripLength},
        {&nBackLightSeconds, sizeof nBackLightSeconds},
        {&nMaxBackLight, sizeof nMaxBackLight},
    };
    for (int ix = 0; ix < (sizeof valueList / sizeof * valueList); ++ix) {
        if (save) {
            eeprom_write_block(valueList[ix].val, where, valueList[ix].size);
        }
        else {  // load
            eeprom_read_block(valueList[ix].val, where, valueList[ix].size);
            // if autoload, exit if the save value is not true
            if (autoload && ix == 0) {
                if (!bAutoLoadSettings) {
                    return;
                }
            }
        }
        where = (void*)((byte*)where + valueList[ix].size);
    }
}


void setupLEDs() {
    strip.begin();
    strip.show();
}



void setupLCDdisplay() {
    lcd.begin(16, 2);
    lcd.print("*LightWand V3.1*");
    lcd.setCursor(0, 1);
    lcd.print("Initializing...");
    delay(2000);
    lcd.clear();
}



void setupSDcard() {
    pinMode(SDssPin, OUTPUT);

    while (!SD.begin(SDssPin)) {
        bBackLightOn = true;
        lcd.print("SD init failed! ");
        delay(1000);
        lcd.clear();
        delay(500);
    }
    lcd.clear();
    lcd.print("SD init done    ");
    delay(1000);
    root = SD.open("/");
    lcd.clear();
    lcd.print("Reading SD...   ");
    delay(500);
    GetFileNamesFromSD(root);
    isort(m_FileNames, m_NumberOfFiles);
    m_CurrentFilename = m_FileNames[0];
    DisplayCurrentFilename();
}



int ReadKeypad() {
    adc_key_in = analogRead(0);             // read the value from the sensor  
    key = get_key(adc_key_in);              // convert into key press

    if (key != oldkey) {                    // if keypress is detected
        delay(50);                            // wait for debounce time
        adc_key_in = analogRead(0);           // read the value from the sensor  
        key = get_key(adc_key_in);            // convert into key press
        if (key != oldkey) {
            oldkey = key;
            if (key >= 0) {
                // turn the light on
                bTurnOnBacklight = true;
                return key;
            }
        }
    }
    return key;
}



// Convert ADC value to key number
int get_key(unsigned int input) {
    int k;
    for (k = 0; k < NUM_KEYS; k++) {
        if (input < adc_key_val[k]) {
            return k;
        }
    }
    if (k >= NUM_KEYS)
        k = -1;                               // No valid key pressed
    return k;
}

void SendFile(String Filename) {
    char temp[14];
    Filename.toCharArray(temp, 14);

    dataFile = SD.open(temp);

    // if the file is available send it to the LED's
    if (dataFile) {
        ReadTheFile();
        dataFile.close();
    }
    else {
        lcd.clear();
        lcd.print("  Error reading ");
        lcd.setCursor(4, 1);
        lcd.print("file");
        bBackLightOn = true;
        delay(1000);
        lcd.clear();
        setupSDcard();
        return;
    }
}

void DisplayCurrentFilename() {
    m_CurrentFilename = m_FileNames[m_FileIndex];
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print(m_CurrentFilename);
}



void GetFileNamesFromSD(File dir) {
    int fileCount = 0;
    String CurrentFilename = "";
    while (1) {
        File entry = dir.openNextFile();
        if (!entry) {
            // no more files
            m_NumberOfFiles = fileCount;
            entry.close();
            break;
        }
        else {
            if (entry.isDirectory()) {
                //GetNextFileName(root);
            }
            else {
                CurrentFilename = entry.name();
                CurrentFilename.toUpperCase();
                if (CurrentFilename.endsWith(".bmp") || CurrentFilename.endsWith(".BMP")) { //find files with our extension only
                    m_FileNames[fileCount] = entry.name();
                    fileCount++;
                }
            }
        }
        entry.close();
    }
}



void latchanddelay(int dur) {
    strip.show();
    delay(dur);
}

void ClearStrip() {
    for (int x = 0; x < stripLength; x++) {
        strip.setPixelColor(x, 0);
    }
    strip.show();
}

// run a test pattern
void RunTest()
{
    switch (nTestNumber) {
    case 0:
        WalkLight();
        break;
    case 1:
        WalkOpposites();
        break;
    }

}

// running bits
void WalkLight()
{
    for (int mode = 0; mode <= 3; ++mode) {
        // RGBW
        byte r, g, b;
        switch (mode) {
        case 0: // red
            r = 255;
            g = 0;
            b = 0;
            break;
        case 1: // green
            r = 0;
            g = 255;
            b = 0;
            break;
        case 2: // blue
            r = 0;
            g = 0;
            b = 255;
            break;
        case 3: // white
            r = 255;
            g = 255;
            b = 255;
            break;
        }
        fixRGBwithGamma(&r, &g, &b);
        for (int ix = 0; ix < stripLength; ++ix) {
            if (ix > 0) {
                strip.setPixelColor(ix - 1, 0);
            }
            strip.setPixelColor(ix, r, g, b);
            strip.show();
            delay(frameHold);
        }
        // remember the last one, turn it off
        strip.setPixelColor(stripLength - 1, 0);
        strip.show();
    }
}

void WalkOpposites()
{
    for (int mode = 0; mode <= 3; ++mode) {
        // RGBW
        byte r, g, b;
        switch (mode) {
        case 0: // red
            r = 255;
            g = 0;
            b = 0;
            break;
        case 1: // green
            r = 0;
            g = 255;
            b = 0;
            break;
        case 2: // blue
            r = 0;
            g = 0;
            b = 255;
            break;
        case 3: // white
            r = 255;
            g = 255;
            b = 255;
            break;
        }
        fixRGBwithGamma(&r, &g, &b);
        for (int ix = 0; ix < stripLength; ++ix) {
            if (ix > 0) {
                strip.setPixelColor(ix - 1, 0);
                strip.setPixelColor(stripLength - ix + 1, 0);
            }
            strip.setPixelColor(stripLength - ix, r, g, b);
            strip.setPixelColor(ix, r, g, b);
            strip.show();
            delay(frameHold);
        }
        // remember the last one, turn it off
        strip.setPixelColor(stripLength - 1, 0);
        strip.show();
    }
}

uint32_t readLong() {
    uint32_t retValue;
    byte incomingbyte;

    incomingbyte = readByte();
    retValue = (uint32_t)((byte)incomingbyte);

    incomingbyte = readByte();
    retValue += (uint32_t)((byte)incomingbyte) << 8;

    incomingbyte = readByte();
    retValue += (uint32_t)((byte)incomingbyte) << 16;

    incomingbyte = readByte();
    retValue += (uint32_t)((byte)incomingbyte) << 24;

    return retValue;
}



uint16_t readInt() {
    byte incomingbyte;
    uint16_t retValue;

    incomingbyte = readByte();
    retValue += (uint16_t)((byte)incomingbyte);

    incomingbyte = readByte();
    retValue += (uint16_t)((byte)incomingbyte) << 8;

    return retValue;
}



int readByte() {
    int retbyte = -1;
    while (retbyte < 0) retbyte = dataFile.read();
    return retbyte;
}


void getRGBwithGamma() {
    g = gamma(readByte()) / (101 - nStripBrightness);
    b = gamma(readByte()) / (101 - nStripBrightness);
    r = gamma(readByte()) / (101 - nStripBrightness);
}

void fixRGBwithGamma(byte* rp, byte* gp, byte* bp) {
    *gp = gamma(*gp) / (101 - nStripBrightness);
    *bp = gamma(*bp) / (101 - nStripBrightness);
    *rp = gamma(*rp) / (101 - nStripBrightness);
}


void ReadTheFile() {
#define MYBMP_BF_TYPE           0x4D42
#define MYBMP_BF_OFF_BITS       54
#define MYBMP_BI_SIZE           40
#define MYBMP_BI_RGB            0L
#define MYBMP_BI_RLE8           1L
#define MYBMP_BI_RLE4           2L
#define MYBMP_BI_BITFIELDS      3L

    uint16_t bmpType = readInt();
    uint32_t bmpSize = readLong();
    uint16_t bmpReserved1 = readInt();
    uint16_t bmpReserved2 = readInt();
    uint32_t bmpOffBits = readLong();
    bmpOffBits = 54;

    /* Check file header */
    if (bmpType != MYBMP_BF_TYPE || bmpOffBits != MYBMP_BF_OFF_BITS) {
        lcd.setCursor(0, 0);
        lcd.print("not a bitmap");
        delay(1000);
        return;
    }

    /* Read info header */
    uint32_t imgSize = readLong();
    uint32_t imgWidth = readLong();
    uint32_t imgHeight = readLong();
    uint16_t imgPlanes = readInt();
    uint16_t imgBitCount = readInt();
    uint32_t imgCompression = readLong();
    uint32_t imgSizeImage = readLong();
    uint32_t imgXPelsPerMeter = readLong();
    uint32_t imgYPelsPerMeter = readLong();
    uint32_t imgClrUsed = readLong();
    uint32_t imgClrImportant = readLong();

    /* Check info header */
    if (imgSize != MYBMP_BI_SIZE || imgWidth <= 0 ||
        imgHeight <= 0 || imgPlanes != 1 ||
        imgBitCount != 24 || imgCompression != MYBMP_BI_RGB ||
        imgSizeImage == 0)
    {
        lcd.setCursor(0, 0);
        lcd.print("Unsupported");
        lcd.setCursor(0, 1);
        lcd.print("Bitmap Use 24bpp");
        delay(1000);
        return;
    }

    int displayWidth = imgWidth;
    if (imgWidth > stripLength) {
        displayWidth = stripLength;           //only display the number of led's we have
    }


    /* compute the line length */
    uint32_t lineLength = imgWidth * 3;
    if ((lineLength % 4) != 0)
        lineLength = (lineLength / 4 + 1) * 4;



    // Note:  
    // The x,r,b,g sequence below might need to be changed if your strip is displaying
    // incorrect colors.  Some strips use an x,r,b,g sequence and some use x,r,g,b
    // Change the order if needed to make the colors correct.

    for (int y = imgHeight; y > 0; y--) {
        lcd.setCursor(12, 0);
        char num[6];
        sprintf(num, "%4d", y);
        lcd.print(num);
        int bufpos = 0;
        for (int x = 0; x < displayWidth; x++) {
            uint32_t offset = (MYBMP_BF_OFF_BITS + (((y - 1) * lineLength) + (x * 3)));
            dataFile.seek(offset);

            getRGBwithGamma();

            strip.setPixelColor(x, r, b, g);

        }
        latchanddelay(frameHold);
    }
}



// Sort the filenames in alphabetical order
void isort(String* filenames, int n) {
    for (int i = 1; i < n; ++i) {
        String j = filenames[i];
        int k;
        for (k = i - 1; (k >= 0) && (j < filenames[k]); k--) {
            filenames[k + 1] = filenames[k];
        }
        filenames[k + 1] = j;
    }
}



PROGMEM const unsigned char gammaTable[] = {
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,
  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,
  4,  4,  4,  4,  5,  5,  5,  5,  5,  6,  6,  6,  6,  6,  7,  7,
  7,  7,  7,  8,  8,  8,  8,  9,  9,  9,  9, 10, 10, 10, 10, 11,
  11, 11, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 15, 15, 16, 16,
  16, 17, 17, 17, 18, 18, 18, 19, 19, 20, 20, 21, 21, 21, 22, 22,
  23, 23, 24, 24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 30,
  30, 31, 32, 32, 33, 33, 34, 34, 35, 35, 36, 37, 37, 38, 38, 39,
  40, 40, 41, 41, 42, 43, 43, 44, 45, 45, 46, 47, 47, 48, 49, 50,
  50, 51, 52, 52, 53, 54, 55, 55, 56, 57, 58, 58, 59, 60, 61, 62,
  62, 63, 64, 65, 66, 67, 67, 68, 69, 70, 71, 72, 73, 74, 74, 75,
  76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91,
  92, 93, 94, 95, 96, 97, 98, 99,100,101,102,104,105,106,107,108,
  109,110,111,113,114,115,116,117,118,120,121,122,123,125,126,127
};


inline byte gamma(byte x) {
    return bGammaCorrection ? pgm_read_byte(&gammaTable[x]) : (x & 0x7f);
}

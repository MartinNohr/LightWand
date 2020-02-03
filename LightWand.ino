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

  Feb 2020: Extensive rewrites and added features by Martin Nohr
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
#define SDcsPin 53                        // SD card CS pin
int NPPin = 31;                           // Data Pin for the NeoPixel LED Strip
int AuxButton = 35;                       // Aux Select Button Pin
int g = 0;                                // Variable for the Green Value
int b = 0;                                // Variable for the Blue Value
int r = 0;                                // Variable for the Red Value

// Initial Variable declarations and assignments (Make changes to these if you want to change defaults)
const char signature[]{ "MLW" };          // set to make sure saved values are valid
int stripLength = 144;                    // Set the number of LEDs the LED Strip
int frameHold = 100;                      // default for the frame delay 
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
bool bScaleHeight = false;                // scale the Y values to fit the number of pixels
bool bCancelRun = false;                  // set to cancel a running job
bool bChainFiles = false;            // set to run all the files from current to the last one in the current folder

// Other program variable declarations, assignments, and initializations
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
#define OPEN_FOLDER_CHAR '\x7e'
#define OPEN_PARENT_FOLDER_CHAR '\x7f'
#define MAXFOLDERS 10
File folders[MAXFOLDERS];
int folderLevel = 0;
File dataFile;
String CurrentFilename = "";
int CurrentFileIndex = 0;
int NumberOfFiles = 0;
String FileNames[200];

// keyboard speeds up when held down longer
#define KEYWAITPAUSE 250
int kbdWaitTime = KEYWAITPAUSE;
int kbdPause = 0;
// set to zero when no key down, and watch time when one is pressed
unsigned long startKeyDown = 0;

// built-in test patterns
enum e_tests { mtDots = 0, mtTwoDots, mtRandomBars, mtCheckerBoard, mtRandomRunningDot, MAXTEST };
// test functions, in same order as enums above
void (*testFunctions[MAXTEST])() = { RunningDot, OppositeRunningDots, RandomBars, CheckerBoard, RandomRunningDot };
const char* testStrings[MAXTEST] = {
    "Running Dot",
    "Opposite Dots",
    "Random Bars",
    "Checker Board",
    "Random Run Dot",
};
// which one to use
int nTestNumber = 0;

// menu strings
enum e_menuitem {
    mSelectFile = 1,
    mChainFiles,
    mStripBrightness,
    mInitDelay,
    mFrameHoldTime,
    mRepeatTimes,
    mRepeatDelay,
    mGammaCorrection,
    mStripLength,
    mScaleHeight,
    mTest,
    mBackLightBrightness,
    mBackLightTimer,
    mAutoLoadSettings,
    mSavedSettings,
    MAXMENU = mSavedSettings
};
const char* menuStrings[] = {
    "File",
    "Chain Files",
    "Brightness",
    "Init Delay",
    "Frame Time",
    "Repeat Times",
    "Repeat Delay",
    "Gamma Correct",
    "Strip Length",
    "Scale Height",
    "Test",
    "LCD Brightness",
    "LCD Timeout",
    "Autoload Sets",
    "Saved Settings",
};

// storage for special character
byte chZeroPattern[8];

int nMaxBackLight = 75;                 // maximum backlight to use in %
int nBackLightSeconds = 5;              // how long to leave the backlight on before dimming
volatile bool bBackLightOn = false;     // used by backlight timer to indicate that backlight is on
volatile bool bTurnOnBacklight = true;  // set to turn the backlight on, safer than calling the BackLightControl code
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

    pinMode(AuxButton, INPUT_PULLUP);

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
            lcd.print(" " + String(CurrentFileIndex + 1) + "/" + String(NumberOfFiles));
        }
        if (menuItem == mTest) {
            lcd.print(" " + String(nTestNumber + 1) + "/" + String(MAXTEST));
        }
        lcd.setCursor(0, 1);
        switch (menuItem) {
        case mSelectFile:
            DisplayCurrentFilename();
            break;
        case mChainFiles:
            lcd.print(bChainFiles ? "ON" : "OFF");
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
        case mScaleHeight:
            lcd.print(bScaleHeight ? "ON" : "OFF");
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
    // run the file selected except when test menu is up, then run the test
    if ((keypress == KEYSELECT) || (digitalRead(AuxButton) == LOW)) {    // The select key was pressed
        // don't run until key released
        while (ReadKeypad() != KEYNONE)
            ;
        int chainNumber = FileCountOnly() - CurrentFileIndex;
        bool isFolder = ProcessFileOrTest(bChainFiles ? chainNumber : 0);
        isFolder |= FileNames[CurrentFileIndex][0]==OPEN_FOLDER_CHAR
            || FileNames[CurrentFileIndex][0]==OPEN_PARENT_FOLDER_CHAR;
        // check if file chaining is on
        if (!isFolder && bChainFiles) {
            // save our settings and process files to the end of the list
            int savedFileIndex = CurrentFileIndex;
            while (CurrentFileIndex < NumberOfFiles - 1) {
                --chainNumber;
                ++CurrentFileIndex;
                // stop on folder
                if (FileNames[CurrentFileIndex][0] == OPEN_FOLDER_CHAR
                    || FileNames[CurrentFileIndex][0] == OPEN_PARENT_FOLDER_CHAR)
                    break;
                DisplayCurrentFilename();
                ProcessFileOrTest(chainNumber);
            }
            CurrentFileIndex = savedFileIndex;
        }
        lastMenuItem = -1;  // show the menu again
    }
    if (keypress == KEYRIGHT) {                    // The Right Key was Pressed
        // redid this as if/else if because switch was crashing
        if (menuItem == mSelectFile) {
            if (CurrentFileIndex < NumberOfFiles - 1) {
                CurrentFileIndex++;
            }
            else {
                CurrentFileIndex = 0;                // On the last file so wrap round to the first file
            }
            DisplayCurrentFilename();
        }
        else if (menuItem == mChainFiles) {
            bChainFiles = !bChainFiles;
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
            ++nTestNumber;
            if (nTestNumber >= MAXTEST)
                nTestNumber = 0;
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
        else if (menuItem == mScaleHeight) {
            bScaleHeight = !bScaleHeight;
        }
        else if (menuItem == mAutoLoadSettings) {
            bAutoLoadSettings = !bAutoLoadSettings;
        }
        // redraw
        lastMenuItem = -1;
    }

    if (keypress == KEYLEFT) {                    // The Left Key was Pressed
        if (menuItem == mSelectFile) {
            if (CurrentFileIndex > 0) {
                CurrentFileIndex--;
            }
            else {
                CurrentFileIndex = NumberOfFiles - 1;    // On the last file so wrap round to the first file
            }
            DisplayCurrentFilename();
        }
        else if (menuItem == mChainFiles) {
            bChainFiles = !bChainFiles;
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
            --nTestNumber;
            if (nTestNumber < 0)
                nTestNumber = MAXTEST - 1;
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
        else if (menuItem == mScaleHeight) {
            bScaleHeight = !bScaleHeight;
        }
        else if (menuItem == mAutoLoadSettings) {
            bAutoLoadSettings = !bAutoLoadSettings;
        }
        // redraw
        lastMenuItem = -1;
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

// count the actual files
int FileCountOnly()
{
    int count = 0;
    // ignore folders, at the end
    char start = FileNames[0][0];
    while (start != OPEN_FOLDER_CHAR && start != OPEN_PARENT_FOLDER_CHAR) {
        ++count;
        start = FileNames[count][0];
    }
    return count;
}

// returns true if the folder was changed
bool ProcessFileOrTest(int chainnumber)
{
    bool bFolderChanged = false;
    char line[17];
    lcd.clear();
    lcd.setCursor(0, 0);
    if (initDelay) {
        for (int seconds = initDelay; seconds; --seconds) {
            lcd.setCursor(0, 0);
            sprintf(line, "Wait: %d", seconds);
            lcd.print(line);
            delay(1000);
        }
    }
    for (int x = repeatTimes; x > 0; x--) {
        lcd.clear();
        lcd.setCursor(0, 1);
        if (menuItem == mTest) {
            lcd.print(testStrings[nTestNumber]);
        }
        else {
            lcd.print(CurrentFilename);
        }
        if (chainnumber) {
            lcd.setCursor(13, 1);
            char line[10];
            sprintf(line, "%2d", chainnumber);
            lcd.print(line);
        }
        lcd.setCursor(0, 0);
        // only display if a file
        char first = CurrentFilename[0];
        if (first != OPEN_FOLDER_CHAR && first != OPEN_PARENT_FOLDER_CHAR) {
            sprintf(line, "Repeat %d", x);
            lcd.print(line);
        }
        if (menuItem == mTest) {
            // run the test
            (*testFunctions[nTestNumber])();
        }
        else {
            // first see if a folder
            if (first == OPEN_FOLDER_CHAR) {
                if (folderLevel < MAXFOLDERS - 1) {
                    ++folderLevel;
                    folders[folderLevel] = SD.open(CurrentFilename.substring(1));
                    GetFileNamesFromSD(folders[folderLevel]);
                }
                else {
                    lcd.clear();
                    lcd.setCursor(0, 0);
                    lcd.print("MAX " + MAXFOLDERS);
                    lcd.setCursor(0, 1);
                    lcd.print("FOLDERS");
                }
                // stop if folder
                bFolderChanged = true;
                break;
            }
            else if (first == OPEN_PARENT_FOLDER_CHAR) {
                // go back a level
                if (folderLevel > 0) {
                    --folderLevel;
                    folders[folderLevel] = SD.open(folders[folderLevel].name());
                    GetFileNamesFromSD(folders[folderLevel]);
                }
                // stop if folder
                bFolderChanged = true;
                break;
            }
            //CurrentFilename = FileNames[CurrentFileIndex];
            // output the file
            SendFile(CurrentFilename);
        }
        strip.clear();
        strip.show();
        if (bCancelRun) {
            bCancelRun = false;
            break;
        }
        if (x > 1) {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Repeat delay...");
            delay(repeatDelay);
        }
    }
}


// save some settings in the eeprom
// if autoload is true, check the first flag, and load the rest if it is true
void SaveSettings(bool save, bool autoload)
{
    void* blockpointer = (void*)NULL;
    struct saveValues {
        void* val;
        int size;
    };
    const saveValues valueList[] = {
        {&signature, sizeof signature},
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
        {&CurrentFileIndex,sizeof CurrentFileIndex},
        {&nTestNumber,sizeof nTestNumber},
        {&bScaleHeight,sizeof bScaleHeight},
        {&bChainFiles,sizeof bChainFiles},
    };
    for (int ix = 0; ix < (sizeof valueList / sizeof * valueList); ++ix) {
        if (save) {
            eeprom_write_block(valueList[ix].val, blockpointer, valueList[ix].size);
        }
        else {  // load
            // check signature
            char svalue[sizeof signature];
            eeprom_read_block(svalue, (void*)NULL, sizeof svalue);
            if (strncmp(svalue, signature, sizeof signature)) {
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("bad signature");
                lcd.setCursor(0, 1);
                lcd.print(svalue);
                delay(1000);
                return;
            }
            eeprom_read_block(valueList[ix].val, blockpointer, valueList[ix].size);
            // if autoload, exit if the save value is not true
            if (autoload && ix == 0) {
                if (!bAutoLoadSettings) {
                    return;
                }
            }
            // make sure file index isn't too big
            if (CurrentFileIndex >= NumberOfFiles) {
                CurrentFileIndex = 0;
            }
            CurrentFilename = FileNames[CurrentFileIndex];
            // check test number also
            if (nTestNumber >= MAXTEST) {
                nTestNumber = 0;
            }
        }
        blockpointer = (void*)((byte*)blockpointer + valueList[ix].size);
    }
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(save ? "Settings Saved" : "Settings Loaded");
    delay(1000);
}


void setupLEDs() {
    strip.begin();
    strip.show();
}



void setupLCDdisplay() {
    lcd.begin(16, 2);
    lcd.print("LightWand V4.1");
    lcd.setCursor(0, 1);
    lcd.print("Initializing...");
    delay(2000);
    lcd.clear();
}



void setupSDcard() {
    pinMode(SDcsPin, OUTPUT);

    while (!SD.begin(SDcsPin)) {
        bBackLightOn = true;
        lcd.print("SD init failed! ");
        delay(1000);
        lcd.clear();
        delay(500);
    }
    lcd.clear();
    lcd.print("SD init done    ");
    delay(1000);
    folders[folderLevel = 0] = SD.open("/");
    lcd.clear();
    lcd.print("Reading SD...   ");
    delay(500);
    GetFileNamesFromSD(folders[folderLevel]);
    CurrentFilename = FileNames[0];
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
        }
    }
    if (key != -1) {
        // turn the light on
        bTurnOnBacklight = true;
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

// build the folder path
String GetFilePath()
{
    String path;
    for (int ix = 0; ix <= folderLevel; ++ix) {
        path += folders[ix].name();
    }
    return path + "/";
}

void SendFile(String Filename) {
    char temp[14];
    Filename.toCharArray(temp, 14);
    String fn = GetFilePath() + temp;
    dataFile = SD.open(fn);

    // if the file is available send it to the LED's
    if (dataFile) {
        ReadAndDisplayFile();
        dataFile.close();
    }
    else {
        lcd.clear();
        lcd.print("* Error reading ");
        lcd.setCursor(0, 1);
        lcd.print(CurrentFilename);
        bBackLightOn = true;
        delay(1000);
        lcd.clear();
        setupSDcard();
        return;
    }
}

void DisplayCurrentFilename() {
    CurrentFilename = FileNames[CurrentFileIndex];
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print(CurrentFilename);
}



void GetFileNamesFromSD(File dir) {
    NumberOfFiles = 0;
    CurrentFileIndex = 0;
    String CurrentFilename = "";
    if (strcmp(dir.name(), "/") != 0) {
        // add an arrow to go back
        FileNames[NumberOfFiles++] = String(OPEN_PARENT_FOLDER_CHAR) + folders[folderLevel - 1].name();
    }
    while (1) {
        File entry = dir.openNextFile();
        if (!entry) {
            // no more files
            entry.close();
            break;
        }
        else {
            if (entry.isDirectory()) {
                CurrentFilename = entry.name();
                CurrentFilename.toUpperCase();
                if (!CurrentFilename.startsWith("SYSTEM\x7e")) {
                    FileNames[NumberOfFiles] = String(OPEN_FOLDER_CHAR) + entry.name();
                    NumberOfFiles++;
                }
            }
            else {
                CurrentFilename = entry.name();
                CurrentFilename.toUpperCase();
                if (CurrentFilename.endsWith(".bmp") || CurrentFilename.endsWith(".BMP")) { //find files with our extension only
                    FileNames[NumberOfFiles] = entry.name();
                    NumberOfFiles++;
                }
            }
        }
        entry.close();
    }
    isort(FileNames, NumberOfFiles);
}



void latchanddelay(int dur) {
    strip.show();
    delay(dur);
}

void ClearStrip() {
    //for (int x = 0; x < stripLength; x++) {
    //    strip.setPixelColor(x, 0);
    //}
    strip.clear();
    strip.show();
}

// test builtin patterns

// checkerboard
void CheckerBoard()
{
    byte r, g, b;
    int size = sqrt(stripLength);
    for (int x = 0; x < size * 2; ++x) {
        // one row with BW, and the next WB
        // write pixels alternating white and black
        for (int y = 0; y < stripLength; ++y) {
            r = g = b = ((((y / size) % 2) ^ (x % 2)) & 1) ? 0 : 255;
            fixRGBwithGamma(&r, &g, &b);
            strip.setPixelColor(y, r, g, b);
        }
        strip.show();
        delay(frameHold);
    }
}

// show random bars of lights, 20 times
void RandomBars()
{
    byte r, g, b;
    srand(millis());
    char line[] = "                ";
//    lcd.setCursor(0, 1);
//    lcd.write(line, 16);
    for (int pass = 0; pass < 50; ++pass) {
        sprintf(line, "%2d/50", pass + 1);
        lcd.setCursor(10, 0);
        lcd.print(line);
        if (pass % 2) {
            // odd numbers, clear
            strip.clear();
        }
        else {
            // even numbers, show bar
            r = random(0, 255);
            g = random(0, 255);
            b = random(0, 255);
            fixRGBwithGamma(&r, &g, &b);
            // fill the strip color
            strip.fill(strip.Color(r, g, b), 0, stripLength);
            //for (int ix = 0; ix < stripLength; ++ix) {
            //    strip.setPixelColor(ix, r, g, b);
            //}
        }
        strip.show();
        delay(frameHold);
    }
}

// running bits
void RunningDot()
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
        char line[10];
        for (int ix = 0; ix < stripLength; ++ix) {
            lcd.setCursor(11, 0);
            sprintf(line, "%3d", ix);
            lcd.print(line);
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

// running dot with random times
void RandomRunningDot()
{
    randomSeed(millis());
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
        char line[10];
        for (int ix = 0; ix < stripLength; ++ix) {
            lcd.setCursor(11, 0);
            sprintf(line, "%3d", ix);
            lcd.print(line);
            if (ix > 0) {
                strip.setPixelColor(ix - 1, 0);
            }
            int step = random(1, 10);
            if (step < 3) {
                ix -= step;
            }
            strip.setPixelColor(ix, r, g, b);
            strip.show();
            // randomize the hold time
            delay(random(0, frameHold * 2));
        }
        // remember the last one, turn it off
        strip.setPixelColor(stripLength - 1, 0);
        strip.show();
    }
}

void OppositeRunningDots()
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
    g = strip.gamma8(readByte()) / (101 - nStripBrightness);
    b = strip.gamma8(readByte()) / (101 - nStripBrightness);
    r = strip.gamma8(readByte()) / (101 - nStripBrightness);
    //g = gamma(readByte()) / (101 - nStripBrightness);
    //b = gamma(readByte()) / (101 - nStripBrightness);
    //r = gamma(readByte()) / (101 - nStripBrightness);
}

void fixRGBwithGamma(byte* rp, byte* gp, byte* bp) {
    *gp = strip.gamma8(*gp) / (101 - nStripBrightness);
    *bp = strip.gamma8(*bp) / (101 - nStripBrightness);
    *rp = strip.gamma8(*rp) / (101 - nStripBrightness);
    //*gp = gamma(*gp) / (101 - nStripBrightness);
    //*bp = gamma(*bp) / (101 - nStripBrightness);
    //*rp = gamma(*rp) / (101 - nStripBrightness);
}


void ReadAndDisplayFile() {
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
    // fix for padding to 4 byte words
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
        uint32_t offset = (MYBMP_BF_OFF_BITS + ((y - 1) * lineLength));
        dataFile.seek(offset);
        for (int x = 0; x < displayWidth; x++) {
            getRGBwithGamma();
            // see if we want this one
            if (bScaleHeight && (x * displayWidth) % imgWidth) {
                continue;
            }
            strip.setPixelColor(x, r, b, g);
        }
        latchanddelay(frameHold);
        // check keys
        int key = ReadKeypad();
        if (key == KEYSELECT) {
            lcd.setCursor(0, 0);
            lcd.print("Select to cancel");
            while (ReadKeypad() != KEYNONE)
                ;
            while (true) {
                key = ReadKeypad();
                if (key == KEYSELECT) {
                    // cancel here
                    while (ReadKeypad() != KEYNONE)
                        ;
                    bCancelRun = true;
                    return;
                }
                else if (key == KEYNONE)
                    continue;
                else {
                    lcd.setCursor(0, 0);
                    lcd.print("                ");
                    break;
                }
            }
        }
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

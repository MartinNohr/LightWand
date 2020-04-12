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
  Switched over to SDFAT, reading is over twice as fast!
*/

// Library initialization
#include <Adafruit_NeoPixel.h>           // Library for the WS2812 Neopixel Strip
#include <SDfat.h>                       // Library for the SD Card
#include <LiquidCrystal.h>               // Library for the LCD Display
#include <SPI.h>                         // Library for the SPI Interface
#include <avr/eeprom.h>
#include <timer.h>
#include "LightWand.h"

SdFat SD;
// Pin assignments for the Arduino (Make changes to these if you use different Pins)
#define BACKLIGHT 10                      // Pin used for the LCD Backlight
#define SDcsPin 53                        // SD card CS pin
int NPPin = 31;                           // Data Pin for the NeoPixel LED Strip
int AuxButton = 35;                       // Aux Select Button Pin
int g = 0;                                // Variable for the Green Value
int b = 0;                                // Variable for the Blue Value
int r = 0;                                // Variable for the Red Value

// menu strings
enum e_menuitem {
    mFirstMenu,
    mSelectFile = mFirstMenu,
    mFrameHoldTime,
    mStripBrightness,
    mRepeatCount,
    mRepeatDelay,
    mInitDelay,
    mChainFiles,
    mGammaCorrection,
    mStripLength,
    mScaleHeight,
    mBackLightBrightness,
    mBackLightTimer,
    mAutoLoadSettings,
    mSavedSettings,
    mTestPatterns,
    mDeleteConfigFile,
    mSaveConfigFile,
    MAXMENU
};
const char* menuStrings[] = {
    "#",            // file, keep short to show folder name
    "Frame Time",
    "Brightness",
    "Repeat Count",
    "Repeat Delay",
    "Init Delay",
    "Chain Files",
    "Gamma Correct",
    "Strip Length",
    "Scale Height",
    "LCD Brightness",
    "LCD Timeout",
    "Autoload Sets",
    "Saved Settings",
    "Tests",
    "Delete File CFG",
    "Save File CFG",
};

// Initial Variable declarations and assignments (Make changes to these if you want to change defaults)
char signature[]{ "MLW" };                // set to make sure saved values are valid
int stripLength = 144;                    // Set the number of LEDs the LED Strip
int frameHold = 15;                       // default for the frame delay 
int lastMenuItem = -1;                    // check to see if we need to redraw menu
int menuItem = mFirstMenu;                // Variable for current main menu selection
int startDelay = 0;                       // Variable for delay between button press and start of light sequence, in seconds
int repeat = 0;                           // Variable to select auto repeat (until select button is pressed again)
int repeatDelay = 0;                      // Variable for delay between repeats
int repeatCount = 1;                      // Variable to keep track of number of repeats
int nStripBrightness = 50;                // Variable and default for the Brightness of the strip
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
bool bWaitForKeyNone = false;       // set this after SELECT so holding the key won't try to cancel the run
String sCurrentLine0;

// SD Card Variables and assignments
#define OPEN_FOLDER_CHAR '\x7e'
#define OPEN_PARENT_FOLDER_CHAR '\x7f'
#define MAXFOLDERS 10
String folders[MAXFOLDERS];
int folderLevel = 0;
SdFile dataFile;
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
enum e_tests {
    mtDots = 0,
    mtTwoDots,
    mtRandomBars,
    mtRandomColors,
    mtCheckerBoard,
    mtRandomRunningDot,
    mtBarberPole,
    mtTestCylon,
    mtTwinkle,
    mtBouncingBalls,
    mtMeteor,
    MAXTEST 
};
// test functions, in same order as enums above
void (*testFunctions[MAXTEST])() = {
    RunningDot,
    OppositeRunningDots,
    RandomBars,
    RandomColors,
    CheckerBoard,
    RandomRunningDot,
    BarberPole,
    TestCylon,
    TestTwinkle,
    TestBouncingBalls,
    TestMeteor
};
const char* testStrings[MAXTEST] = {
    "Running Dot",
    "Opposite Dots",
    "Random Bars",
    "Random Colors",
    "Checker Board",
    "Random Run Dot",
    "Barber Pole",
    "Cylon Eye",
    "Twinkle",
    "Bouncing Balls",
    "Meteor"
};
// which one to use
int nTestNumber = 0;


// storage for special character
byte chZeroPattern[8];

int nMaxBackLight = 75;                 // maximum backlight to use in %
int nBackLightSeconds = 10;             // how long to leave the backlight on before dimming
volatile bool bBackLightOn = false;     // used by backlight timer to indicate that backlight is on
volatile bool bTurnOnBacklight = true;  // set to turn the backlight on, safer than calling the BackLightControl code

struct saveValues {
    void* val;
    int size;
};
const saveValues saveValueList[] = {
    {&signature, sizeof signature},
    {&bAutoLoadSettings, sizeof bAutoLoadSettings},
    {&nStripBrightness, sizeof nStripBrightness},
    {&frameHold, sizeof frameHold},
    {&startDelay, sizeof startDelay},
    {&repeat, sizeof repeat},
    {&repeatCount, sizeof repeatCount},
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

                                        
// timers to run things
auto EventTimers = timer_create_default();

// set this to the delay time while we get the next frame
bool bStripWaiting = false;

// this gets called every second/TIMERSTEPS
#define TIMERSTEPS 10
bool BackLightControl(void*)
{
    static int light;
    static int fade;
    static int timer;
    // don't do anything while writing the file out
    if (bStripWaiting) {
        return;
    }
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

// counts the delay for the frame hold time
bool StripDelay(void*)
{
    bTurnOnBacklight = true;
    bStripWaiting = false;
    return false;
}

// Setup loop to get everything ready.  This is only run once at power on or reset
void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.println("Starting setup");

    pinMode(AuxButton, INPUT_PULLUP);
    folders[folderLevel = 0] = String("/");
    setupLEDs();
    setupLCDdisplay();
    setupSDcard();
    // turn on the keyboard reader
    digitalWrite(LED_BUILTIN, HIGH);
    SaveSettings(false, true);
    EventTimers.every(1000 / TIMERSTEPS, BackLightControl);
    Serial.println("Finishing setup");
}

// create a character by filling blocks to indicate how far down the menu we are
void CreateMenuCharacter()
{
    memset(chZeroPattern, 0, sizeof chZeroPattern);
    for (int menu = 0; menu <= menuItem; ++menu) {
        chZeroPattern[menu % 7] |= (1 << (4 - menu / 7));
    }
    lcd.createChar(0, chZeroPattern);
}

// The Main Loop for the program starts here... 
// This will loop endlessly looking for a key press to perform a function
void loop() {
    EventTimers.tick();
    if (bBackLightOn && menuItem != lastMenuItem) {
        CreateMenuCharacter();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.write((byte)0);
        lcd.print(menuStrings[menuItem]);
        if (menuItem == mSelectFile) {
            lcd.print(String(CurrentFileIndex + 1) + "/" + String(NumberOfFiles) + " " + folders[folderLevel]);
        }
        if (menuItem == mTestPatterns) {
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
            lcd.print(" %  ");
            break;
        case mInitDelay:
            lcd.print(String(startDelay) + " Seconds");
            break;
        case mFrameHoldTime:
            lcd.print(String(frameHold) + " mSec");
            break;
        case mRepeatCount:
            lcd.print(repeatCount);
            break;
        case mRepeatDelay:
            lcd.print(String(repeatDelay) + " mSec");
            break;
        case mTestPatterns:
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
        case mDeleteConfigFile:
            lcd.print("SELECT to Delete");
            break;
        case mSaveConfigFile:
            lcd.print("<=Load >=Save");
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
    switch (keypress) {
    case KEYSELECT:
        HandleKeySelect();
        lastMenuItem = -1;  // show the menu again
        break;
    case KEYUP:
        menuItem = menuItem > mFirstMenu ? menuItem - 1 : MAXMENU - 1;
        break;
    case KEYDOWN:
        menuItem = menuItem < MAXMENU - 1 ? menuItem + 1 : mFirstMenu;
        break;
    case KEYLEFT:
        HandleKeyLeft();
        // redraw
        lastMenuItem = -1;
        break;
    case KEYRIGHT:
        HandleKeyRight();
        // redraw
        lastMenuItem = -1;
        break;
    case KEYNONE:
        // no key is pressed, reset the timer
        startKeyDown = 0;
        // and the keypause
        kbdWaitTime = KEYWAITPAUSE;
        break;
    }
    // wait a bit between keypresses
    if (keypress != KEYNONE) {
        // a key is down
        // remember the time it was pressed
        if (startKeyDown == 0) {
            startKeyDown = millis();
        }
        // calcualate how long to wait
        unsigned long now = millis();
        if (now > startKeyDown + 4000)
            kbdWaitTime = KEYWAITPAUSE / 5;
        if (now > startKeyDown + 6000)
            kbdWaitTime = KEYWAITPAUSE / 10;
        if (now > startKeyDown + 8000)
            kbdWaitTime = KEYWAITPAUSE / 20;
        // do the prescribed wait
        delay(kbdWaitTime);
    }
}

// always run the current file unless the test menu is slected
void HandleKeySelect()
{
    if (menuItem == mDeleteConfigFile) {
        WriteOrDeleteConfigFile(CurrentFilename, true);
        lcd.setCursor(0, 1);
        lcd.print("Deleted         ");
        delay(1000);
        return;
    }
    // make sure we wait before accepting this key again
    bWaitForKeyNone = true;
    int chainNumber = FileCountOnly() - CurrentFileIndex;
    bool isFolder = ProcessFileOrTest(bChainFiles ? chainNumber : 0);
    isFolder |= FileNames[CurrentFileIndex][0] == OPEN_FOLDER_CHAR
        || FileNames[CurrentFileIndex][0] == OPEN_PARENT_FOLDER_CHAR;
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
    // wait for release
    while (ReadKeypad() != KEYNONE)
        delay(10);
}

void HandleKeyRight()
{
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
        ++startDelay;
    }
    else if (menuItem == mFrameHoldTime) {
        ++frameHold;
    }
    else if (menuItem == mRepeatCount) {
        repeatCount += 1;
    }
    else if (menuItem == mRepeatDelay) {
        repeatDelay += 100;
    }
    else if (menuItem == mTestPatterns) {
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
    else if (menuItem == mSaveConfigFile) {
        if (WriteOrDeleteConfigFile(CurrentFilename, false)) {
            lcd.setCursor(0, 0);
            lcd.print("Saved CFG       ");
            delay(1000);
        }
    }
}

void HandleKeyLeft()
{
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
        if (startDelay > 0) {
            --startDelay;
        }
    }
    else if (menuItem == mFrameHoldTime) {
        --frameHold;
        if (frameHold < 15) {
            frameHold = 15;
        }
    }
    else if (menuItem == mRepeatCount) {
        if (repeatCount > 1) {
            repeatCount -= 1;
        }
    }
    else if (menuItem == mRepeatDelay) {
        if (repeatDelay > 0) {
            repeatDelay -= 100;
        }
    }
    else if (menuItem == mTestPatterns) {
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
    else if (menuItem == mSaveConfigFile) {
        if (WriteOrDeleteConfigFile(CurrentFilename, false)) {
            lcd.setCursor(0, 0);
            lcd.print("Loaded LWC");
            delay(1000);
        }
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
    if (startDelay) {
        for (int seconds = startDelay; seconds; --seconds) {
            lcd.setCursor(0, 0);
            sprintf(line, "Wait: %d", seconds);
            lcd.print(line);
            delay(1000);
        }
    }
    for (int counter = repeatCount; counter > 0; counter--) {
        lcd.clear();
        lcd.setCursor(0, 1);
        if (menuItem == mTestPatterns) {
            lcd.print(testStrings[nTestNumber]);
        }
        else {
            DisplayCurrentFilename();
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
            sprintf(line, "Count %d", counter);
            lcd.print(line);
            // save this for restoring if cancel is cancelled
            sCurrentLine0 = line;
        }
        if (menuItem == mTestPatterns) {
            // run the test
            (*testFunctions[nTestNumber])();
        }
        else {
            // first see if a folder
            if (first == OPEN_FOLDER_CHAR) {
                if (folderLevel < MAXFOLDERS - 1) {
                    folders[++folderLevel] = CurrentFilename.substring(1);
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
                    GetFileNamesFromSD(folders[--folderLevel]);
                }
                // stop if folder
                bFolderChanged = true;
                break;
            }
            //CurrentFilename = FileNames[CurrentFileIndex];
            // output the file
            SendFile(CurrentFilename);
        }
        if (bCancelRun) {
            bCancelRun = false;
            break;
        }
        if (counter > 1) {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Repeat delay...");
            if (repeatDelay) {
                strip.clear();
                strip.show();
                delay(repeatDelay);
            }
        }
    }
    strip.clear();
    strip.show();
}

// save or restore all the settings that are relevant
// this is used when reading the LWC associated with a file
bool SettingsSaveRestore(bool save)
{
    static void* memptr = NULL;
    if (save) {
        // get some memory and save the values
        if (memptr)
            free(memptr);
        memptr = malloc(sizeof saveValueList);
        if (!memptr)
            return false;
    }
    void* blockptr = memptr;
    for (int ix = 0; ix < (sizeof saveValueList / sizeof * saveValueList); ++ix) {
        if (save) {
            memcpy(blockptr, saveValueList[ix].val, saveValueList[ix].size);
        }
        else {
            memcpy(saveValueList[ix].val, blockptr, saveValueList[ix].size);
        }
        blockptr = (void*)((byte*)blockptr + saveValueList[ix].size);
    }
    if (!save) {
        // if it was saved, restore it and free the memory
        if (memptr) {
            free(memptr);
            memptr = NULL;
        }
    }
    return true;
}

// save some settings in the eeprom
// if autoload is true, check the first flag, and load the rest if it is true
void SaveSettings(bool save, bool autoload)
{
    void* blockpointer = (void*)NULL;
    for (int ix = 0; ix < (sizeof saveValueList / sizeof * saveValueList); ++ix) {
        if (save) {
            eeprom_write_block(saveValueList[ix].val, blockpointer, saveValueList[ix].size);
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
            eeprom_read_block(saveValueList[ix].val, blockpointer, saveValueList[ix].size);
            // if autoload, exit if the save value is not true
            if (autoload && ix == 1) {
                if (!bAutoLoadSettings) {
                    return;
                }
            }
        }
        blockpointer = (void*)((byte*)blockpointer + saveValueList[ix].size);
    }
    if (!save) {
        int savedFileIndex = CurrentFileIndex;
        // we don't know the folder path, so just reset the folder level
        folderLevel = 0;
        setupSDcard();
        CurrentFileIndex = savedFileIndex;
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
    lcd.print("LightWand V5.1");
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
    folders[folderLevel = 0] = String("/");
    lcd.clear();
    lcd.print("Reading SD...   ");
    delay(500);
    GetFileNamesFromSD(folders[folderLevel]);
    CurrentFilename = FileNames[0];
    DisplayCurrentFilename();
}


// read the keys
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
    if (digitalRead(AuxButton) == LOW) {
        return KEYSELECT;
    }
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
        path += folders[ix];
    }
    if (path == "/")
        return path;
    else
        return path + "/";
}

void SendFile(String Filename) {
    char temp[14];
    Filename.toCharArray(temp, 14);
    // see if there is an associated config file
    String cfFile = temp;
    cfFile = MakeLWCFilename(cfFile);
    SettingsSaveRestore(true);
    ProcessConfigFile(cfFile);
    String fn = GetFilePath() + temp;
    dataFile.open(fn.c_str(), O_READ);
    // if the file is available send it to the LED's
    if (dataFile.available()) {
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
    SettingsSaveRestore(false);
}

void DisplayCurrentFilename() {
    CurrentFilename = FileNames[CurrentFileIndex];
    // strip extension
    String sname = CurrentFilename.substring(0, CurrentFilename.lastIndexOf('.'));
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print(sname);
}


// read the files from the card
// look for start.lwc, and process it, but don't add it to the list
bool GetFileNamesFromSD(String dir) {
    String startfile;
    // Directory file.
    SdFile root;
    // Use for files
    SdFile file;
    // start over
    NumberOfFiles = 0;
    CurrentFileIndex = 0;
    String CurrentFilename = "";

    if (!root.open(dir.c_str())) {
        Serial.println("open failed: " + dir);
        return false;
    }
    if (dir != "/") {
        // add an arrow to go back
        FileNames[NumberOfFiles++] = String(OPEN_PARENT_FOLDER_CHAR) + folders[folderLevel - 1];
    }
    while (file.openNext(&root, O_RDONLY)) {
        if (!file.isHidden()) {
            char buf[100];
            file.getName(buf, sizeof buf);
            if (file.isDir()) {
                FileNames[NumberOfFiles] = String(OPEN_FOLDER_CHAR) + buf;
                NumberOfFiles++;
            }
            else if (file.isFile()) {
                CurrentFilename = String(buf);
                String uppername = CurrentFilename;
                uppername.toUpperCase();
                if (uppername.endsWith(".BMP")) { //find files with our extension only
                    FileNames[NumberOfFiles] = CurrentFilename;
                    NumberOfFiles++;
                }
                else if (uppername == "START.LWC") {
                    startfile = CurrentFilename;
                }
            }
        }
        file.close();
    }
    root.close();
    delay(500);
    isort(FileNames, NumberOfFiles);
    // see if we need to process the auto start file
    if (startfile.length())
        ProcessConfigFile(startfile);
    return true;
}

// process the lines in the config file
bool ProcessConfigFile(String filename)
{
    bool retval = true;
    String filepath = GetFilePath() + filename;
    SdFile rdfile(filepath.c_str(), O_RDONLY);
    if (rdfile.available()) {
        String line, command, args;
        char buf[100];
        while (rdfile.fgets(buf, sizeof buf, "\n")) {
            line = String(buf);
            // read the lines and do what they say
            int ix = line.indexOf('=', 0);
            if (ix > 0) {
                command = line.substring(0, ix);
                command.trim();
                command.toUpperCase();
                args = line.substring(ix + 1);
                if (!command.compareTo("PIXELS")) {
                    stripLength = args.toInt();
                    strip.updateLength(stripLength);
                }
                else if (command == "BRIGHTNESS") {
                    nStripBrightness = args.toInt();
                    if (nStripBrightness < 1)
                        nStripBrightness = 1;
                    else if (nStripBrightness > 100)
                        nStripBrightness = 100;
                }
                else if (command == "REPEAT COUNT") {
                    repeatCount = args.toInt();
                }
                else if (command == "REPEAT DELAY") {
                    repeatDelay = args.toInt();
                }
                else if (command == "FRAME TIME") {
                    frameHold = args.toInt();
                }
                else if (command == "START DELAY") {
                    startDelay = args.toInt();
                }
            }
        }
    }
    else
        retval = false;
    return retval;
}

// create the config file, or remove it
bool WriteOrDeleteConfigFile(String filename, bool remove)
{
    bool retval = true;
    String name = MakeLWCFilename(filename);
    String filepath = GetFilePath() + name;
    if (remove) {
        SD.remove(filepath.c_str());
    }
    else {
        SdFile file(filepath.c_str(), O_READ | O_WRITE | O_CREAT | O_TRUNC);
        String line;
        if (file.availableForWrite()) {
            line = "PIXELS=" + String(stripLength);
            file.println(line);
            line = "BRIGHTNESS=" + String(nStripBrightness);
            file.println(line);
            line = "REPEAT COUNT=" + String(repeatCount);
            file.println(line);
            line = "REPEAT DELAY=" + String(repeatDelay);
            file.println(line);
            line = "FRAME TIME=" + String(frameHold);
            file.println(line);
            line = "START DELAY=" + String(startDelay);
            file.println(line);
            file.close();
        }
        else
            retval = false;
    }
    return retval;
}

String MakeLWCFilename(String filename)
{
    String cfFile = filename;
    cfFile = cfFile.substring(0, cfFile.lastIndexOf('.') + 1);
    cfFile += "LWC";
    return cfFile;
}


void ClearStrip() {
    strip.clear();
    strip.show();
}


uint32_t readLong() {
    uint32_t retValue;
    byte incomingbyte;

    incomingbyte = readByte(false);
    retValue = (uint32_t)((byte)incomingbyte);

    incomingbyte = readByte(false);
    retValue += (uint32_t)((byte)incomingbyte) << 8;

    incomingbyte = readByte(false);
    retValue += (uint32_t)((byte)incomingbyte) << 16;

    incomingbyte = readByte(false);
    retValue += (uint32_t)((byte)incomingbyte) << 24;

    return retValue;
}



uint16_t readInt() {
    byte incomingbyte;
    uint16_t retValue;

    incomingbyte = readByte(false);
    retValue += (uint16_t)((byte)incomingbyte);

    incomingbyte = readByte(false);
    retValue += (uint16_t)((byte)incomingbyte) << 8;

    return retValue;
}

byte filebuf[512];
int fileindex = 0;
int filebufsize = 0;
uint32_t filePosition = 0;

int readByte(bool clear) {
    //int retbyte = -1;
    if (clear) {
        filebufsize = 0;
        fileindex = 0;
        return 0;
    }
    // TODO: this needs to align with 512 byte boundaries
    if (filebufsize == 0 || fileindex >= sizeof filebuf) {
        filePosition = dataFile.curPosition();
        //// if not on 512 boundary yet, just return a byte
        //if ((filePosition % 512) && filebufsize == 0) {
        //    //Serial.println("not on 512");
        //    return dataFile.read();
        //}
        // read a block
//        Serial.println("block read");
        filebufsize = dataFile.read(filebuf, sizeof filebuf);
        fileindex = 0;
    }
    return filebuf[fileindex++];
    //while (retbyte < 0) 
    //    retbyte = dataFile.read();
    //return retbyte;
}

// make sure we are the right place
uint32_t FileSeek(uint32_t place)
{
    if (place < filePosition || place >= filePosition + filebufsize) {
        // we need to read some more
        filebufsize = 0;
        dataFile.seekSet(place);
    }
}

void getRGBwithGamma() {
    g = strip.gamma8(readByte(false)) / (101 - nStripBrightness);
    b = strip.gamma8(readByte(false)) / (101 - nStripBrightness);
    r = strip.gamma8(readByte(false)) / (101 - nStripBrightness);
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

    long secondsLeft = 0, lastSeconds = 0;
    char num[8];
    for (int y = imgHeight; y > 0; y--) {
        // approximate time left
        secondsLeft = ((long)y * frameHold / 1000L) + 1;
        if (secondsLeft != lastSeconds) {
            lcd.setCursor(11, 0);
            lastSeconds = secondsLeft;
            sprintf(num, "%3d S", secondsLeft);
            lcd.print(num);
        }
        if ((y % 10) == 0) {
            lcd.setCursor(12, 1);
            sprintf(num, "%4d", y);
            lcd.print(num);
        }
        int bufpos = 0;
        //uint32_t offset = (MYBMP_BF_OFF_BITS + ((y - 1) * lineLength));
        //dataFile.seekSet(offset);
        for (int x = 0; x < displayWidth; x++) {
            // moved this back here because it might make it possible to reverse scan in the future
            FileSeek((uint32_t)MYBMP_BF_OFF_BITS + (((y - 1) * lineLength) + (x * 3)));
            //dataFile.seekSet((uint32_t)MYBMP_BF_OFF_BITS + (((y - 1) * lineLength) + (x * 3)));
            getRGBwithGamma();
            // see if we want this one
            if (bScaleHeight && (x * displayWidth) % imgWidth) {
                continue;
            }
            strip.setPixelColor(x, r, b, g);
        }
        // wait for timer to expire before we show the next frame
        while (bStripWaiting)
            EventTimers.tick();
        bStripWaiting = true;
        // set a timer so we can go ahead and load the next frame
        EventTimers.in(frameHold, StripDelay);
        strip.show();
        // check keys
        if (CheckCancel())
            break;
    }
    readByte(true);
}

// see if they want to cancel
bool CheckCancel()
{
    static long waitForIt;
    static bool bReadyToCancel = false;
    static bool bCancelPending = false;
    // don't run until key released
    if (bWaitForKeyNone && ReadKeypad() != KEYNONE)
        return false;
    bWaitForKeyNone = false;
    bool retflag = false;
    int key = ReadKeypad();
    if (key == KEYSELECT) {
        if (!bCancelPending) {
            lcd.setCursor(0, 0);
            lcd.print("Cancel?   ");
            bCancelPending = true;
            bReadyToCancel = false;
            return false;
        }
        key = ReadKeypad();
        if (bReadyToCancel) {
            bCancelPending = false;
            if (key == KEYSELECT) {
                bCancelRun = true;
                retflag = true;
            }
        }
    }
    else if (bCancelPending && !bReadyToCancel && key == KEYNONE) {
        bReadyToCancel = true;
        waitForIt = millis();
        return false;
    }
    else if (bReadyToCancel && ((key != KEYNONE) || (millis() > waitForIt + 5000))) {
        bReadyToCancel = false;
        bCancelPending = false;
        lcd.setCursor(0, 0);
        lcd.print(sCurrentLine0);
    }
    return retflag;
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

// test builtin patterns

// checkerboard
void CheckerBoard()
{
    byte r, g, b;
    int size = sqrt(stripLength);
    for (int x = 0; x < size * 2; ++x) {
        if (CheckCancel())
            return;
        // one row with BW, and the next WB
        // write pixels alternating white and black
        for (int y = 0; y < stripLength; ++y) {
            if (CheckCancel())
                return;
            r = g = b = ((((y / size) % 2) ^ (x % 2)) & 1) ? 0 : 255;
            fixRGBwithGamma(&r, &g, &b);
            strip.setPixelColor(y, r, g, b);
        }
        strip.show();
        delay(frameHold);
    }
}

// show random bars of lights with blacks between, 50 times
void RandomBars()
{
    byte r, g, b;
    srand(millis());
    char line[] = "                ";
    //    lcd.setCursor(0, 1);
    //    lcd.write(line, 16);
    for (int pass = 0; pass < 50; ++pass) {
        if (CheckCancel())
            return;
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

// show random bars of lights, 50 times
void RandomColors()
{
    byte r, g, b;
    srand(millis());
    char line[] = "                ";
    //    lcd.setCursor(0, 1);
    //    lcd.write(line, 16);
    for (int pass = 0; pass < 50; ++pass) {
        if (CheckCancel())
            return;
        sprintf(line, "%2d/50", pass + 1);
        lcd.setCursor(10, 0);
        lcd.print(line);
        r = random(0, 255);
        g = random(0, 255);
        b = random(0, 255);
        fixRGBwithGamma(&r, &g, &b);
        // fill the strip color
        strip.fill(strip.Color(r, g, b), 0, stripLength);
        strip.show();
        delay(frameHold);
    }
}

// running bits
void RunningDot()
{
    for (int mode = 0; mode <= 3; ++mode) {
        if (CheckCancel())
            return;
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
            if (CheckCancel())
                return;
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
        if (CheckCancel())
            return;
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
            if (CheckCancel())
                return;
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
        if (CheckCancel())
            return;
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
            if (CheckCancel())
                return;
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

#define BARBERSIZE 10
#define BARBERCOUNT 40
void BarberPole()
{
    uint32_t color, red, white, blue;
    byte r, g, b;
    r = 255, g = 0, b = 0;
    fixRGBwithGamma(&r, &g, &b);
    red = strip.Color(r, g, b);
    r = 255, g = 255, b = 255;
    fixRGBwithGamma(&r, &g, &b);
    white = strip.Color(r, g, b);
    r = 0, g = 0, b = 255;
    fixRGBwithGamma(&r, &g, &b);
    blue = strip.Color(r, g, b);
    for (int loop = 0; loop < 4 * BARBERCOUNT; ++loop) {
        if (CheckCancel())
            return;
        for (int ledIx = 0; ledIx < stripLength; ++ledIx) {
            if (CheckCancel())
                return;
            // figure out what color
            switch (((ledIx + loop) % BARBERCOUNT) / BARBERSIZE) {
            case 0: // red
                color = red;
                break;
            case 1: // white
            case 3:
                color = white;
                break;
            case 2: // blue
                color = blue;
                break;
            }
            strip.setPixelColor(ledIx, color);
        }
        strip.show();
        delay(frameHold);
    }
}

void TestCylon()
{
    CylonBounce(255 * nStripBrightness / 100, 0, 0, 4, 10, 50);
}
void CylonBounce(byte red, byte green, byte blue, int EyeSize, int SpeedDelay, int ReturnDelay)
{
    for (int i = 0; i < stripLength - EyeSize - 2; i++) {
        if (CheckCancel())
            return;
        strip.clear();
        strip.setPixelColor(i, red / 10, green / 10, blue / 10);
        for (int j = 1; j <= EyeSize; j++) {
            strip.setPixelColor(i + j, red, green, blue);
        }
        strip.setPixelColor(i + EyeSize + 1, red / 10, green / 10, blue / 10);
        strip.show();
        delay(SpeedDelay);
    }

    delay(ReturnDelay);

    for (int i = stripLength - EyeSize - 2; i > 0; i--) {
        if (CheckCancel())
            return;
        strip.clear();
        strip.setPixelColor(i, red / 10, green / 10, blue / 10);
        for (int j = 1; j <= EyeSize; j++) {
            strip.setPixelColor(i + j, red, green, blue);
        }
        strip.setPixelColor(i + EyeSize + 1, red / 10, green / 10, blue / 10);
        strip.show();
        delay(SpeedDelay);
    }

    delay(ReturnDelay);
}

void TestTwinkle() {
    TwinkleRandom(20, 100, false);
}

void TwinkleRandom(int Count, int SpeedDelay, boolean OnlyOne) {
    strip.clear();
    byte brightness = (255 * nStripBrightness) / 100;

    for (int i = 0; i < Count; i++) {
        if (CheckCancel())
            return;
        strip.setPixelColor(random(stripLength), random(0, brightness), random(0, brightness), random(0, brightness));
        strip.show();
        delay(SpeedDelay);
        if (OnlyOne) {
            strip.clear();
        }
    }

    delay(SpeedDelay);
}

#define BallCount 4
void TestBouncingBalls() {
    byte bright = 255 * nStripBrightness / 100;
    byte colors[BallCount][3] = {
        {bright, 0, 0},
        {bright, bright, bright},
        {0, 0, bright},
        {0, bright, 0}
    };

    BouncingColoredBalls(colors);
}

void BouncingColoredBalls(byte colors[][3]) {
    float Gravity = -9.81;
    int StartHeight = 1;

    float Height[BallCount];
    float ImpactVelocityStart = sqrt(-2 * Gravity * StartHeight);
    float ImpactVelocity[BallCount];
    float TimeSinceLastBounce[BallCount];
    int   Position[BallCount];
    long  ClockTimeSinceLastBounce[BallCount];
    float Dampening[BallCount];

    for (int i = 0; i < BallCount; i++) {
        ClockTimeSinceLastBounce[i] = millis();
        Height[i] = StartHeight;
        Position[i] = 0;
        ImpactVelocity[i] = ImpactVelocityStart;
        TimeSinceLastBounce[i] = 0;
        Dampening[i] = 0.90 - float(i) / pow(BallCount, 2);
    }

    // run for 30 seconds
    long start = millis();
    while (millis() < start + 10000) {
        if (CheckCancel())
            return;
        for (int i = 0; i < BallCount; i++) {
            if (CheckCancel())
                return;
            TimeSinceLastBounce[i] = millis() - ClockTimeSinceLastBounce[i];
            Height[i] = 0.5 * Gravity * pow(TimeSinceLastBounce[i] / 1000, 2.0) + ImpactVelocity[i] * TimeSinceLastBounce[i] / 1000;

            if (Height[i] < 0) {
                Height[i] = 0;
                ImpactVelocity[i] = Dampening[i] * ImpactVelocity[i];
                ClockTimeSinceLastBounce[i] = millis();

                if (ImpactVelocity[i] < 0.01) {
                    ImpactVelocity[i] = ImpactVelocityStart;
                }
            }
            Position[i] = round(Height[i] * (stripLength - 1) / StartHeight);
        }

        for (int i = 0; i < BallCount; i++) {
            if (CheckCancel())
                return;
            strip.setPixelColor(Position[i], colors[i][0], colors[i][1], colors[i][2]);
        }

        strip.show();
        strip.clear();
    }
}

void TestMeteor() {
    byte brightness = (255 * nStripBrightness) / 100;
    meteorRain(brightness, brightness, brightness, 10, 64, true, 30);
}

void meteorRain(byte red, byte green, byte blue, byte meteorSize, byte meteorTrailDecay, boolean meteorRandomDecay, int SpeedDelay) {
    strip.clear();

    for (int i = 0; i < stripLength + stripLength; i++) {
        if (CheckCancel())
            return;
        // fade brightness all LEDs one step
        for (int j = 0; j < stripLength; j++) {
            if (CheckCancel())
                return;
            if ((!meteorRandomDecay) || (random(10) > 5)) {
                fadeToBlack(j, meteorTrailDecay);
            }
        }

        // draw meteor
        for (int j = 0; j < meteorSize; j++) {
            if (CheckCancel())
                return;
            if ((i - j < stripLength) && (i - j >= 0)) {
                strip.setPixelColor(i - j, red, green, blue);
            }
        }

        strip.show();
        delay(SpeedDelay);
    }
}

void fadeToBlack(int ledNo, byte fadeValue) {
#ifdef ADAFRUIT_NEOPIXEL_H
    // NeoPixel
    uint32_t oldColor;
    uint8_t r, g, b;
    int value;

    oldColor = strip.getPixelColor(ledNo);
    r = (oldColor & 0x00ff0000UL) >> 16;
    g = (oldColor & 0x0000ff00UL) >> 8;
    b = (oldColor & 0x000000ffUL);

    r = (r <= 10) ? 0 : (int)r - (r * fadeValue / 256);
    g = (g <= 10) ? 0 : (int)g - (g * fadeValue / 256);
    b = (b <= 10) ? 0 : (int)b - (b * fadeValue / 256);

    strip.setPixelColor(ledNo, r, g, b);
#endif
#ifndef ADAFRUIT_NEOPIXEL_H
    // FastLED
    leds[ledNo].fadeToBlackBy(fadeValue);
#endif
}

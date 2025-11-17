Rolling Shutter "Global" Flash ControllerThis C++ application provides a software-timed flash trigger for rolling shutter cameras (IMX415) to eliminate motion skew and light-banding artifacts, mimicking a global shutter.
The Problem:
Skew: Moving objects appear slanted or distorted.
Banding: Pulsed lighting (LEDs) causes bright/dark bands in the image.
The Solution:
This program uses a software-timed, interrupt-driven approach to fire a single, short flash at the exact moment all sensor rows are simultaneously active.It listens for the camera's XVS (Frame Start) pulse as a hardware interrupt (T=0).It waits for the T1 (Vertical Blanking) and T2 (Roll-Up Time) to pass.It fires the LED flash (FT) the instant the "Magic Window" (T3) opens.The Magic Window (T3) is created by setting the camera's Exposure Time (Texp) to be longer than the sensor's roll-up time (T2).Magic Window (T3) = Texp - T2

Requirements:
Set Camera Exposure:  camera's Texp must be longer than T2 + FlashTime.
Example: If T2=10.6ms and FT=0.05ms, set Texp to 15ms (via V4L2 API) to get a safe 4.4ms window.

Setup and Configuration1.
Dependencies
On a Debian-based system (like Radxa's):sudo apt-get update
sudo apt-get install gpiod libgpiod-dev2. 
Code Configuration
Edit the constants at the top of rolling_shutter_flash.cpp to match hardware and settings:

// --- GPIO Configuration ---
const char *GPIO_CHIP_NAME = "gpiochip0"; 
const int XVS_PIN_LINE = 17;     
const int LED_PIN_LINE = 18;     

// --- Timing Constants (in Nanoseconds) ---
// T1: Vertical Blanking (Time from XVS to Row 1)
constexpr long long T1_VERTICAL_BLANKING_NS = 284'200LL;

// T2: Sensor Roll-Up (Time from Row 1 to Row 2160)
constexpr long long T2_ROLL_UP_TIME_NS = 10'580'000LL;

// T-exp: Total Exposure Time (MUST MATCH YOUR CAMERA SETTING)
constexpr long long TOTAL_EXPOSURE_TIME_NS = 15'000'000LL;

// FT: Flash Duration (How long the LED is on)
constexpr long long FLASH_DURATION_NS = 50'000LL;
T1 and T2 are calculated for the IMX415.TOTAL_EXPOSURE_TIME_NS must match the setting you apply to your camera.

How to Compile and Run
Compile:g++ -o rolling_shutter_flash rolling_shutter_flash.cpp -lgpiodcxx -lrt -pthread
Run:sudo ./rolling_shutter_flash
sudo is required to request real-time priority (SCHED_FIFO) to minimize software jitter and ensure precise timing.

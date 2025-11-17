# Rolling Shutter "Global" Flash Controller

A software-timed flash trigger for rolling shutter cameras (IMX415) that eliminates motion skew and light-banding artifacts, effectively mimicking a global shutter.

## The Problem

Rolling shutter cameras capture images by scanning row-by-row, which creates two common issues:

- **Skew**: Moving objects appear slanted or distorted due to the time difference between when different rows are captured
- **Banding**: Pulsed lighting causes bright/dark bands across the image

## How It Works

This program uses a software-timed approach to fire a single, short flash at the precise moment when all sensor rows are simultaneously active.

### Timing Sequence

1. Listens for the camera's **XVS** (Frame Start) pulse as a hardware interrupt (T=0)
2. Waits for **T1** (Vertical Blanking) and **T2** (Roll-Up Time) to pass
3. Fires the LED flash (**FT**) the instant the "Magic Window" (**T3**) opens

### The Magic Window

The Magic Window (T3) is created by setting the camera's Exposure Time (Texp) to be longer than the sensor's roll-up time (T2):

```
Magic Window (T3) = Texp - T2
```

During this window, all sensor rows are simultaneously exposed, allowing for artifact-free flash photography.

## Requirements

### Values
The camera's exposure time (Texp) must be longer than `T2 + FlashTime`:

**Example**: If T2=10.6ms and FT=0.05ms, set Texp to 15ms (via V4L2 API) to get a safe 4.4ms window.


### Dependencies

On Debian-based systems (Radxa, Raspberry Pi OS, Ubuntu):

```bash
sudo apt-get update
sudo apt-get install gpiod libgpiod-dev
```

### Configuration

Edit the constants at the top of `rolling_shutter_flash.cpp` to match hardware setup:

```cpp
// --- GPIO Configuration ---
const char *GPIO_CHIP_NAME = "gpiochip0"; 
const int XVS_PIN_LINE = 17;      // GPIO pin for XVS input
const int LED_PIN_LINE = 18;      // GPIO pin for LED output

// --- Timing Constants (in Nanoseconds) ---
// T1: Vertical Blanking (Time from XVS to Row 1)
constexpr long long T1_VERTICAL_BLANKING_NS = 284'200LL;

// T2: Sensor Roll-Up (Time from Row 1 to Row 2160)
constexpr long long T2_ROLL_UP_TIME_NS = 10'580'000LL;

// T-exp: Total Exposure Time (MUST MATCH YOUR CAMERA SETTING)
constexpr long long TOTAL_EXPOSURE_TIME_NS = 15'000'000LL;

// FT: Flash Duration (How long the LED is on)
constexpr long long FLASH_DURATION_NS = 50'000LL;
```

**Important**: 
- `T1` and `T2` are pre-calculated for the IMX415 sensor
- `TOTAL_EXPOSURE_TIME_NS` **must match** the exposure time you set on your camera via V4L2

## Usage

### Compile

```bash
g++ -o rolling_shutter_flash rolling_shutter_flash.cpp -lgpiodcxx -lrt -pthread
```

### Run

```bash
sudo ./rolling_shutter_flash
```

**Note**: `sudo` is required to request real-time priority (`SCHED_FIFO`), which minimizes software jitter and ensures precise timing.


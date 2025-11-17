/*
 Logic:
 1. Waits for the XVS (Frame Start) signal as a hardware interrupt.
 2. Starts a high-precision timer at the XVS pulse (T=0).
 3. Waits for a pre-calculated "dead time" (T1 + T2) to pass.
 - T1: Vertical blanking time (delay from XVS to Row 1).
 - T2: Sensor "roll-up" time (delay from Row 1 to Last Row).
 4. At the end of the dead time (T1 + T2), we enter the "Magic Window" (T3).
 5. It immediately fires the LED for a very short Flash Duration (FT).
 6. The flash must complete before the Magic Window closes (FT < T3).

 How to Compile:
 g++ -o rolling_shutter_flash rolling_shutter_flash.cpp -lgpiodcxx -lrt -pthread
 
 How to Run:
 sudo ./rolling_shutter_flash
 (Requires sudo for real-time priority)
 */

#include <iostream>
#include <gpiod.hpp>      
#include <chrono>          
#include <thread>          
#include <csignal>         // For catching Ctrl+C
#include <sched.h>         // For setting real-time priority


// This is gpio config and needs to be changed accordingly
const char *GPIO_CHIP_NAME = "gpiochip0"; // chip number for radxa (Within each chip, GPIO lines are numbered from 0)
const int XVS_PIN_LINE = 17;     // GPIO line number for XVS input
const int LED_PIN_LINE = 18;     // GPIO line number for LED output

// Timing Constants (in Nanoseconds) for IMX415

// T1: Vertical Blanking (Time from XVS to Row 1)
// From datasheet (p.56), "All pixel mode" has ~58 lines of overhead before the "Recording pixel area" starts.
// 58 lines * 4900 ns/line = 284,200 ns
constexpr long long T1_VERTICAL_BLANKING_NS = 284'200LL;

// T2: Sensor Roll-Up (Time from Row 1 to Row 2160)
// (2160 - 1) * 4.9µs = 10,580,000 ns
constexpr long long T2_ROLL_UP_TIME_NS = 10'579'100LL;

// T-exp: Total Exposure Time (Needs to be set later in the code using SHR0 register)
// Currently 15ms, a large exposure
constexpr long long TOTAL_EXPOSURE_TIME_NS = 15'000'000LL;

// T3: The "Magic Window" (All rows are active) :  T3 = T-exp - T2
constexpr long long T3_MAGIC_WINDOW_NS = TOTAL_EXPOSURE_TIME_NS - T2_ROLL_UP_TIME_NS;

// FT: Flash Duration (How long the LED is on)
// This value can be decided later, ideally short???
// Currently 50µs
constexpr long long FLASH_DURATION_NS = 50'000LL;

// --- Compile-Time Safety Check ---
// We must ensure the flash finishes before the window closes.
static_assert(FLASH_DURATION_NS < T3_MAGIC_WINDOW_NS,
              "Flash Duration (FT) is longer than the Magic Window (T3)!");


// This is the time we wait after XVS before firing the flash.
constexpr long long TRIGGER_WAIT_NS = T1_VERTICAL_BLANKING_NS + T2_ROLL_UP_TIME_NS;

// Global flag to handle graceful exit (Ctrl+C)
volatile bool running = true;

void signal_handler(int signum) {
    if (signum == SIGINT) {
        running = false;
    }
}

// Function to set real-time scheduling for this thread
void set_realtime_priority() {
    sched_param sch;
    sch.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (sched_setscheduler(0, SCHED_FIFO, &sch) == -1) {
        std::cerr << "Warning: Failed to set real-time priority. Timing may be imprecise." << std::endl;
        std::cerr << "         Run with 'sudo' to enable real-time." << std::endl;
    } else {
        std::cout << "Real-time priority set." << std::endl;
    }
}

int main() {
    try {
        // Set up real-time priority
        set_realtime_priority();

        // Initialize GPIO chip
        gpiod::chip chip(GPIO_CHIP_NAME);

        // Configure XVS pin as input, waiting for a rising edge
        // EVENT_RISING_EDGE will now correctly trigger on the 0V -> 3.3V transition.
        gpiod::line xvs_line = chip.get_line(XVS_PIN_LINE);
        // set up interrupt
        xvs_line.request(
            {"rolling_shutter_flash", gpiod::line_request::EVENT_RISING_EDGE}, 0);
        
        std::cout << "XVS line requested on chip " << GPIO_CHIP_NAME << ", line " << XVS_PIN_LINE << std::endl;


        // Configure LED pin as output, initial value LOW
        gpiod::line led_line = chip.get_line(LED_PIN_LINE);
        led_line.request(
            {"rolling_shutter_flash", gpiod::line_request::DIRECTION_OUTPUT, 0}, 0);
        
        std::cout << "LED line requested on chip " << GPIO_CHIP_NAME << ", line " << LED_PIN_LINE << std::endl;


        //Register Ctrl+C handler
        std::signal(SIGINT, signal_handler);

        std::cout << "--- Rolling Shutter Flash Controller ---" << std::endl;
        std::cout << "  T1 (Blanking): " << T1_VERTICAL_BLANKING_NS / 1000.0 << " µs" << std::endl;
        std::cout << "  T2 (Roll-Up):  " << T2_ROLL_UP_TIME_NS / 1000.0 << " µs" << std::endl;
        std::cout << "  T3 (Window):   " << T3_MAGIC_WINDOW_NS / 1000.0 << " µs (Set by T-exp)" << std::endl;
        std::cout << "  FT (Flash):    " << FLASH_DURATION_NS / 1000.0 << " µs" << std::endl;
        std::cout << "  TRIGGER WAIT:  " << TRIGGER_WAIT_NS / 1000.0 << " µs" << std::endl;
        std::cout << "------------------------------------------" << std::endl;
        std::cout << "Waiting for the first XVS pulse... (Press Ctrl+C to stop)" << std::endl;

        while (running) {
            // Wait for an XVS event
            if (xvs_line.event_wait(std::chrono::milliseconds(500))) {
                
                // Read the event to clear the event queue 
                //(otherwise the event will stay in the queue and will automatically trigger event_wait to return true the next iteration)
                gpiod::line_event event = xvs_line.event_read();
                 
                // XVS pulse received. T=0.
                auto xvs_time = std::chrono::high_resolution_clock::now();

                //    Spin-wait for the (T1 + T2) dead time to pass.
                //    A spin-wait is used for the highest precision, as `sleep_for` can be inexact.
                while (std::chrono::high_resolution_clock::now() - xvs_time < std::chrono::nanoseconds(TRIGGER_WAIT_NS)) {
                }

                // Fire LED
                led_line.set_value(1); // LED ON

                //   Wait for the exact flash duration
                //    We sleep here, as the critical timing has already happened.
                std::this_thread::sleep_for(std::chrono::nanoseconds(FLASH_DURATION_NS));

                // 5. Turn LED OFF
                led_line.set_value(0);
            }
        }

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << "Please check GPIO chip name and line numbers." << std::endl;
        return 1;
    }

    std::cout << "\nCaught signal. Cleaning up and exiting." << std::endl;
    // led_line and xvs_line are automatically released by their destructors
    return 0;
}
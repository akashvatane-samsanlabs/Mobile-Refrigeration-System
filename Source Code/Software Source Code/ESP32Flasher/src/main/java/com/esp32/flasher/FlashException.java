package com.esp32.flasher;

public class FlashException extends Exception {

    public enum ErrorType {
        NO_PORT_SELECTED(
                "No serial port selected.",
                "Connect the ESP32 via USB data cable.\n"
                        + "Install CP210x / CH340 / FTDI driver if needed.\n"
                        + "Check Device Manager → Ports (COM & LPT)."
        ),
        NO_PORTS_FOUND(
                "ESP32 not detected — no COM ports found.",
                "Issue 1: ESP32 Not Detected\n"
                        + "• Install the USB-to-UART driver (CP210x, CH340, or FTDI).\n"
                        + "• Use a USB data cable (not charge-only).\n"
                        + "• Try another USB port.\n"
                        + "• Check Device Manager."
        ),
        PORT_NOT_AVAILABLE(
                "Selected COM port is no longer available.",
                "Issue 3: Incorrect COM Port\n"
                        + "• Check Device Manager for the correct COM port.\n"
                        + "• Click Refresh Ports and select the correct port.\n"
                        + "• Reconnect the ESP32 USB cable."
        ),
        PORT_IN_USE(
                "Could not open COM port — port may be in use.",
                "Issue 3: Incorrect COM Port\n"
                        + "• Close Serial Monitor, Arduino IDE, or any app using this port.\n"
                        + "• Confirm the correct COM port in Device Manager.\n"
                        + "• Restart the flasher and try again."
        ),
        BIN_FILE_MISSING(
                "Firmware .bin file not selected or not found.",
                "Select a valid firmware .bin file using Browse .bin.\n"
                        + "Ensure the project was built successfully before flashing."
        ),
        BIN_FILE_EMPTY(
                "Firmware .bin file is empty.",
                "The selected .bin file has zero bytes.\n"
                        + "Rebuild the firmware (idf.py build) and select the correct output file."
        ),
        INVALID_OFFSET(
                "Invalid flash offset.",
                "Use a hexadecimal offset such as 0x10000 for application firmware,\n"
                        + "0x1000 for bootloader, or 0x8000 for partition table."
        ),
        ESPTOOL_NOT_FOUND(
                "esptool is not installed.",
                "Install Python and run:\n  pip install esptool\n\n"
                        + "Then restart this application."
        ),
        CONNECTION_FAILED(
                "Failed to connect to ESP32.",
                "Issue 2: Failed to Connect to ESP32\n"
                        + "Manual Boot Mode Procedure (Section 10):\n"
                        + "1. Press and hold the BOOT button.\n"
                        + "2. Start flashing.\n"
                        + "3. Release BOOT when connection starts.\n"
                        + "4. Press EN/RESET if required.\n\n"
                        + "The app will also retry at a lower baud rate automatically."
        ),
        CHIP_MISMATCH(
                "Detected chip does not match selected chip type.",
                "Section 6: Select the correct ESP32 target.\n"
                        + "• Standard ESP32 → esp32\n"
                        + "• ESP32-S3 → esp32s3\n"
                        + "• ESP32-C3 → esp32c3\n\n"
                        + "Ensure the selected target matches your hardware."
        ),
        FLASH_VERIFY_FAILED(
                "Firmware verification failed after writing.",
                "Flashing did not complete correctly.\n"
                        + "• Retry with BOOT button held during connection.\n"
                        + "• Try a lower baud rate (115200).\n"
                        + "• Check USB cable and power supply."
        ),
        FLASH_WRITE_FAILED(
                "Firmware write failed.",
                "• Hold BOOT and retry flashing.\n"
                        + "• Confirm the correct .bin file and flash offset.\n"
                        + "• Try erase flash before writing."
        ),
        INTERRUPTED(
                "Flashing was interrupted.",
                "Do not disconnect the ESP32 during flashing.\n"
                        + "Retry the flash operation."
        ),
        APP_NOT_RUNNING(
                "Firmware flashed but application may not have started.",
                "Issue 5: Firmware Flashed Successfully but Application Does Not Start\n"
                        + "• Open serial monitor and check output.\n"
                        + "• Verify the selected ESP32 target/chip type.\n"
                        + "• Verify power supply.\n"
                        + "• Confirm the correct firmware project was flashed."
        ),
        BUILD_FAILURE(
                "Firmware build failed or build output not found.",
                "Issue 4: Build Failure\n"
                        + "• Resolve compilation errors shown in the log.\n"
                        + "• Use Full Clean + Build (idf.py fullclean && idf.py build).\n"
                        + "• Then flash again: idf.py -p COMx flash"
        ),
        INVALID_PROJECT(
                "Invalid or missing ESP-IDF project folder.",
                "Section 5: Open the ESP-IDF project folder.\n"
                        + "It must contain:\n"
                        + "  • CMakeLists.txt\n"
                        + "  • main/CMakeLists.txt\n"
                        + "  • main/ source files\n"
                        + "  • sdkconfig (after set-target)"
        ),
        IDF_NOT_FOUND(
                "ESP-IDF (idf.py) environment not found.",
                "Section 3: Install ESP-IDF Framework and set IDF_PATH.\n"
                        + "• Install Espressif ESP-IDF or open ESP-IDF Command Prompt.\n"
                        + "• Ensure idf.py is in PATH, or set IDF_PATH environment variable.\n"
                        + "• Alternatively use Manual .bin tab with pre-built firmware."
        ),
        UNKNOWN(
                "An unexpected error occurred during flashing.",
                "Check the flash log for details.\n"
                        + "Refer to Section 15 (Common Issues and Troubleshooting) in the procedure document."
        );

        private final String message;
        private final String solution;

        ErrorType(String message, String solution) {
            this.message = message;
            this.solution = solution;
        }

        public String getMessage() {
            return message;
        }

        public String getSolution() {
            return solution;
        }
    }

    private final ErrorType errorType;

    public FlashException(ErrorType errorType) {
        super(errorType.getMessage());
        this.errorType = errorType;
    }

    public FlashException(ErrorType errorType, String detail) {
        super(errorType.getMessage() + " " + detail);
        this.errorType = errorType;
    }

    public FlashException(ErrorType errorType, Throwable cause) {
        super(errorType.getMessage(), cause);
        this.errorType = errorType;
    }

    public ErrorType getErrorType() {
        return errorType;
    }

    public String getSolution() {
        return errorType.getSolution();
    }

    public String getFullMessage() {
        return getMessage() + "\n\n" + getSolution();
    }
}

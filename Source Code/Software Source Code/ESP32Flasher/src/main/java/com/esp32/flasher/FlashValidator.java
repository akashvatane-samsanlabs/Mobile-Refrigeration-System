package com.esp32.flasher;

import java.io.File;
import java.util.Arrays;
import java.util.Locale;
import java.util.regex.Pattern;

public final class FlashValidator {

    private static final Pattern OFFSET_PATTERN = Pattern.compile("^0[xX][0-9a-fA-F]+$");

    private FlashValidator() {
    }

    public static void validatePortSelected(String port) throws FlashException {
        if (port == null || port.isBlank()) {
            throw new FlashException(FlashException.ErrorType.NO_PORT_SELECTED);
        }
    }

    public static void validatePortsAvailable() throws FlashException {
        if (SerialPortService.listPortNames().length == 0) {
            throw new FlashException(FlashException.ErrorType.NO_PORTS_FOUND);
        }
    }

    public static void validatePortExists(String port) throws FlashException {
        validatePortSelected(port);
        boolean found = Arrays.asList(SerialPortService.listPortNames()).contains(port);
        if (!found) {
            throw new FlashException(FlashException.ErrorType.PORT_NOT_AVAILABLE);
        }
    }

    public static void validateBinFile(File binFile) throws FlashException {
        if (binFile == null || !binFile.isFile()) {
            throw new FlashException(FlashException.ErrorType.BIN_FILE_MISSING);
        }
        if (binFile.length() == 0) {
            throw new FlashException(FlashException.ErrorType.BIN_FILE_EMPTY);
        }
    }

    public static void validateOffset(String offset) throws FlashException {
        if (offset == null || offset.isBlank()) {
            throw new FlashException(FlashException.ErrorType.INVALID_OFFSET);
        }
        if (!OFFSET_PATTERN.matcher(offset.trim()).matches()) {
            throw new FlashException(FlashException.ErrorType.INVALID_OFFSET);
        }
    }

    public static void analyzeLogLine(String line, String selectedChip) throws FlashException {
        if (line == null || line.isBlank()) {
            return;
        }

        String lower = line.toLowerCase(Locale.ROOT);

        if (containsAny(lower,
                "could not open port",
                "failed to connect",
                "no serial data received",
                "timed out waiting for packet header")) {
            throw new FlashException(FlashException.ErrorType.CONNECTION_FAILED, line.trim());
        }

        if (containsAny(lower, "access is denied", "permission denied", "being used by another process")) {
            throw new FlashException(FlashException.ErrorType.PORT_IN_USE, line.trim());
        }

        if (containsAny(lower, "hash of data verified", "hard resetting")) {
            return;
        }

        if (containsAny(lower, "verification failed", "verify failed")) {
            throw new FlashException(FlashException.ErrorType.FLASH_VERIFY_FAILED, line.trim());
        }

        if (containsAny(lower, "invalid head", "invalid chip", "wrong chip")) {
            throw new FlashException(FlashException.ErrorType.CHIP_MISMATCH, line.trim());
        }

        if (line.contains("Chip is ")) {
            String detected = line.substring(line.indexOf("Chip is ") + 8).trim();
            if (!chipMatches(selectedChip, detected)) {
                throw new FlashException(FlashException.ErrorType.CHIP_MISMATCH,
                        "Detected: " + detected + ", selected: " + selectedChip);
            }
        }

        if (containsAny(lower, "a fatal error occurred", "flash write error")) {
            throw new FlashException(FlashException.ErrorType.FLASH_WRITE_FAILED, line.trim());
        }
    }

    public static void analyzeFailureOutput(String output, String selectedChip) throws FlashException {
        if (output != null && containsAny(output.toLowerCase(Locale.ROOT),
                "hash of data verified", "hard resetting via rts pin", "leaving...")) {
            return;
        }
        for (String line : output.split("\\R")) {
            analyzeLogLine(line, selectedChip);
        }
        if (output != null && output.toLowerCase(Locale.ROOT).contains("failed to connect")) {
            throw new FlashException(FlashException.ErrorType.CONNECTION_FAILED);
        }
        throw new FlashException(FlashException.ErrorType.FLASH_WRITE_FAILED);
    }

    public static void analyzeBuildLogLine(String line) {
        // Build errors are evaluated from full output when the process exits.
    }

    public static void analyzeBuildOutput(String output) throws FlashException {
        if (output != null && output.contains("Project build complete")) {
            return;
        }
        String lower = output == null ? "" : output.toLowerCase();
        if (containsAny(lower, "ninja: build stopped", "compilation failed", "cmake error", "failed to build")) {
            throw new FlashException(FlashException.ErrorType.BUILD_FAILURE);
        }
        throw new FlashException(FlashException.ErrorType.BUILD_FAILURE);
    }

    public static boolean isBuildSuccessful(String output) {
        return output != null && output.contains("Project build complete");
    }

    private static boolean chipMatches(String selected, String detected) {
        String normalizedSelected = normalizeChip(selected);
        String normalizedDetected = normalizeChip(detected);
        return normalizedDetected.contains(normalizedSelected)
                || normalizedSelected.contains(normalizedDetected);
    }

    private static String normalizeChip(String chip) {
        return chip == null ? "" : chip.toLowerCase(Locale.ROOT).replace("-", "").replace(" ", "");
    }

    private static boolean containsAny(String text, String... needles) {
        for (String needle : needles) {
            if (text.contains(needle)) {
                return true;
            }
        }
        return false;
    }
}

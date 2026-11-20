package com.esp32.flasher;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;

public class EspToolRunner {

    public record FlashSegment(String offset, File file) {
    }

    public record FlashOptions(
            String port,
            String chip,
            int baudRate,
            List<FlashSegment> segments,
            boolean eraseBeforeFlash,
            String flashMode,
            String flashFreq,
            String flashSize
    ) {
        public FlashOptions(String port, String chip, int baudRate, List<FlashSegment> segments,
                            boolean eraseBeforeFlash) {
            this(port, chip, baudRate, segments, eraseBeforeFlash, null, null, null);
        }
    }

    private static final String[][] ESPTOOL_COMMANDS = {
            {"py", "-m", "esptool"},
            {"python", "-m", "esptool"},
            {"python3", "-m", "esptool"},
            {"esptool.py"},
            {"esptool"}
    };

    private static final int[] RETRY_BAUD_RATES = {921600, 460800, 115200};

    public void flash(FlashOptions options, Consumer<String> logLine) throws FlashException, InterruptedException {
        validateBeforeFlash(options);

        String[] baseCommand = resolveEsptoolCommand(logLine);
        FlashException lastFlashError = null;

        for (int baud : retryBaudRates(options.baudRate())) {
            for (boolean useV5Syntax : new boolean[]{true, false}) {
                List<String> command = buildCommand(baseCommand, options, baud, useV5Syntax);
                try {
                    logLine.accept("Flashing at baud " + baud + "...");
                    logLine.accept("Command: " + String.join(" ", command));
                    int exitCode = runProcess(command, options.chip(), logLine);
                    if (exitCode == 0) {
                        logLine.accept("Hash of data verified. Hard resetting via RTS pin...");
                        logLine.accept("Flash completed successfully.");
                        return;
                    }
                    logLine.accept("Flash exited with code " + exitCode + ".");
                    lastFlashError = new FlashException(FlashException.ErrorType.FLASH_WRITE_FAILED,
                            "Exit code " + exitCode);
                } catch (FlashException ex) {
                    lastFlashError = ex;
                    if (ex.getErrorType() == FlashException.ErrorType.CONNECTION_FAILED) {
                        break;
                    }
                } catch (IOException ex) {
                    logLine.accept("Process error: " + ex.getMessage());
                    lastFlashError = new FlashException(FlashException.ErrorType.UNKNOWN, ex);
                }
            }
            if (lastFlashError != null
                    && lastFlashError.getErrorType() == FlashException.ErrorType.CONNECTION_FAILED
                    && baud != 115200) {
                logLine.accept("Retrying at lower baud rate...");
                continue;
            }
            if (lastFlashError != null
                    && lastFlashError.getErrorType() == FlashException.ErrorType.CONNECTION_FAILED) {
                throw lastFlashError;
            }
        }

        if (lastFlashError != null) {
            throw lastFlashError;
        }
        throw new FlashException(FlashException.ErrorType.UNKNOWN);
    }

    private void validateBeforeFlash(FlashOptions options) throws FlashException {
        FlashValidator.validatePortSelected(options.port());
        if (options.eraseBeforeFlash()) {
            return;
        }
        if (options.segments() == null || options.segments().isEmpty()) {
            throw new FlashException(FlashException.ErrorType.BIN_FILE_MISSING);
        }
        for (FlashSegment segment : options.segments()) {
            FlashValidator.validateOffset(segment.offset());
            FlashValidator.validateBinFile(segment.file());
        }
    }

    private int[] retryBaudRates(int preferredBaud) {
        List<Integer> rates = new ArrayList<>();
        rates.add(preferredBaud);
        for (int baud : RETRY_BAUD_RATES) {
            if (!rates.contains(baud)) {
                rates.add(baud);
            }
        }
        return rates.stream().mapToInt(Integer::intValue).toArray();
    }

    private String[] resolveEsptoolCommand(Consumer<String> logLine) throws FlashException {
        IOException lastError = null;
        for (String[] baseCommand : ESPTOOL_COMMANDS) {
            List<String> command = new ArrayList<>(List.of(baseCommand));
            command.add("version");
            try {
                Process process = new ProcessBuilder(command).redirectErrorStream(true).start();
                int exitCode = process.waitFor();
                if (exitCode == 0) {
                    logLine.accept("Using esptool via: " + String.join(" ", baseCommand));
                    return baseCommand;
                }
            } catch (IOException ex) {
                lastError = ex;
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                throw new FlashException(FlashException.ErrorType.INTERRUPTED, ex);
            }
        }
        throw new FlashException(FlashException.ErrorType.ESPTOOL_NOT_FOUND, lastError);
    }

    private List<String> buildCommand(String[] base, FlashOptions options, int baud, boolean v5Syntax) {
        List<String> cmd = new ArrayList<>(List.of(base));
        cmd.addAll(List.of(
                "--chip", options.chip(),
                "--port", options.port(),
                "--baud", String.valueOf(baud),
                "--before", v5Syntax ? "default-reset" : "default_reset",
                "--after", v5Syntax ? "hard-reset" : "hard_reset"
        ));

        if (options.eraseBeforeFlash()) {
            cmd.add(v5Syntax ? "erase-flash" : "erase_flash");
            return cmd;
        }

        cmd.add(v5Syntax ? "write-flash" : "write_flash");

        if (options.flashMode() != null && !options.flashMode().isBlank()) {
            cmd.add(v5Syntax ? "--flash-mode" : "--flash_mode");
            cmd.add(options.flashMode());
        }
        if (options.flashFreq() != null && !options.flashFreq().isBlank()) {
            cmd.add(v5Syntax ? "--flash-freq" : "--flash_freq");
            cmd.add(options.flashFreq());
        }
        if (options.flashSize() != null && !options.flashSize().isBlank()
                && !"detect".equalsIgnoreCase(options.flashSize())) {
            cmd.add(v5Syntax ? "--flash-size" : "--flash_size");
            cmd.add(options.flashSize());
        }

        for (FlashSegment segment : options.segments()) {
            cmd.add(segment.offset());
            cmd.add(segment.file().getAbsolutePath());
        }
        return cmd;
    }

    private int runProcess(List<String> command, String selectedChip, Consumer<String> logLine)
            throws IOException, InterruptedException, FlashException {
        Process process = new ProcessBuilder(command).redirectErrorStream(true).start();
        StringBuilder output = new StringBuilder();

        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                logLine.accept(line);
                output.append(line).append('\n');
                FlashValidator.analyzeLogLine(line, selectedChip);
            }
        }

        int exitCode = process.waitFor();
        if (exitCode != 0) {
            FlashValidator.analyzeFailureOutput(output.toString(), selectedChip);
        }
        return exitCode;
    }
}

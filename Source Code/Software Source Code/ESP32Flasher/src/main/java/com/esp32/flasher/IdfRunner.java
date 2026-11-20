package com.esp32.flasher;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;

public class IdfRunner {

    public record RunResult(int exitCode, String output) {
    }

    public RunResult setTarget(File projectDir, String target, Consumer<String> log)
            throws FlashException, InterruptedException {
        EspIdfProjectService.validateProject(projectDir);
        log.accept("Section 6: idf.py set-target " + target);
        return run(projectDir, log, null, "set-target", target);
    }

    public RunResult build(File projectDir, Consumer<String> log) throws FlashException, InterruptedException {
        EspIdfProjectService.validateProject(projectDir);
        log.accept("Section 7: idf.py build");
        RunResult result = run(projectDir, log, null, "build");
        if (result.exitCode() != 0 || !FlashValidator.isBuildSuccessful(result.output())) {
            throw new FlashException(FlashException.ErrorType.BUILD_FAILURE);
        }
        log.accept("Project build complete.");
        return result;
    }

    public RunResult fullCleanAndBuild(File projectDir, Consumer<String> log)
            throws FlashException, InterruptedException {
        EspIdfProjectService.validateProject(projectDir);
        log.accept("Section 15 Issue 4: idf.py fullclean");
        run(projectDir, log, null, "fullclean");
        return build(projectDir, log);
    }

    public RunResult flash(File projectDir, String port, String chip, Consumer<String> log)
            throws FlashException, InterruptedException {
        EspIdfProjectService.validateProject(projectDir);
        FlashValidator.validatePortSelected(port);
        FlashValidator.validatePortExists(port);
        if (!EspIdfProjectService.isBuildComplete(projectDir)) {
            throw new FlashException(FlashException.ErrorType.BUILD_FAILURE,
                    "Build the project first (Section 7).");
        }
        log.accept("Section 9: idf.py -p " + port + " flash");
        RunResult result = run(projectDir, log, chip, "-p", port, "flash");
        if (result.exitCode() != 0) {
            throw new FlashException(FlashException.ErrorType.FLASH_WRITE_FAILED);
        }
        log.accept("Flash complete. ESP32 reset automatically (Section 9).");
        return result;
    }

    public void flashWithEsptoolFallback(File projectDir, String port, String chip, int baud,
                                         EspToolRunner esptool, Consumer<String> log)
            throws FlashException, InterruptedException {
        File buildDir = new File(projectDir, "build");
        File bootloader = new File(buildDir, "bootloader/bootloader.bin");
        File partition = new File(buildDir, "partition_table/partition-table.bin");
        File application = EspIdfProjectService.findApplicationBin(buildDir);

        List<EspToolRunner.FlashSegment> segments = new ArrayList<>();
        if (bootloader.isFile()) {
            segments.add(new EspToolRunner.FlashSegment("0x1000", bootloader));
        }
        if (partition.isFile()) {
            segments.add(new EspToolRunner.FlashSegment("0x8000", partition));
        }
        if (application != null && application.isFile()) {
            segments.add(new EspToolRunner.FlashSegment("0x10000", application));
        }
        if (segments.isEmpty()) {
            throw new FlashException(FlashException.ErrorType.BIN_FILE_MISSING,
                    "No build artifacts found. Run Build first.");
        }

        log.accept("Flashing bootloader, partition table, and application (Section 9).");
        esptool.flash(new EspToolRunner.FlashOptions(port, chip, baud, segments, false), log);
    }

    private RunResult run(File projectDir, Consumer<String> log, String chip, String... idfArgs)
            throws FlashException, InterruptedException {
        IOException lastError = null;
        for (ProcessBuilder builder : createProcessBuilders(projectDir, idfArgs)) {
            try {
                log.accept("Running: " + String.join(" ", builder.command()));
                return execute(builder, chip, log);
            } catch (IOException ex) {
                lastError = ex;
                log.accept("Could not start: " + ex.getMessage());
            }
        }
        throw new FlashException(FlashException.ErrorType.IDF_NOT_FOUND, lastError);
    }

    private List<ProcessBuilder> createProcessBuilders(File projectDir, String[] idfArgs) {
        List<ProcessBuilder> builders = new ArrayList<>();
        String argLine = String.join(" ", idfArgs);

        String idfPath = System.getenv("IDF_PATH");
        if (idfPath != null && !idfPath.isBlank()) {
            File exportBat = new File(idfPath, "export.bat");
            if (exportBat.isFile()) {
                builders.add(new ProcessBuilder("cmd.exe", "/c",
                        "\"" + exportBat.getAbsolutePath() + "\" && idf.py " + argLine));
            }
        }

        List<String> direct = new ArrayList<>();
        direct.add("idf.py");
        direct.addAll(List.of(idfArgs));
        builders.add(new ProcessBuilder(direct));

        for (ProcessBuilder builder : builders) {
            builder.directory(projectDir);
            builder.redirectErrorStream(true);
        }
        return builders;
    }

    private RunResult execute(ProcessBuilder builder, String chip, Consumer<String> log)
            throws IOException, InterruptedException, FlashException {
        Process process = builder.start();
        StringBuilder output = new StringBuilder();

        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                log.accept(line);
                output.append(line).append('\n');
                if (chip != null) {
                    FlashValidator.analyzeLogLine(line, chip);
                } else {
                    FlashValidator.analyzeBuildLogLine(line);
                }
            }
        }

        int exitCode = process.waitFor();
        String text = output.toString();
        if (exitCode != 0 && chip == null) {
            FlashValidator.analyzeBuildOutput(text);
        }
        return new RunResult(exitCode, text);
    }
}

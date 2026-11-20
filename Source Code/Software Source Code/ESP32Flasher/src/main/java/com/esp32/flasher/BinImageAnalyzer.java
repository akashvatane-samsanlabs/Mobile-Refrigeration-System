package com.esp32.flasher;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public final class BinImageAnalyzer {

    public record BinImageInfo(
            String chipType,
            String recommendedOffset,
            String flashMode,
            String flashFreq,
            String flashSize,
            String projectName,
            String appVersion
    ) {
        public String summary() {
            return "Detected: " + chipType + " app \"" + projectName + "\" @ "
                    + recommendedOffset + " (flash " + flashSize + " " + flashMode + " " + flashFreq + ")";
        }
    }

    private static final String[][] ESPTOOL = {
            {"py", "-m", "esptool"},
            {"python", "-m", "esptool"},
            {"python3", "-m", "esptool"}
    };

    private BinImageAnalyzer() {
    }

    public static BinImageInfo analyze(File binFile) throws FlashException {
        FlashValidator.validateBinFile(binFile);
        IOExceptionHolder lastError = new IOExceptionHolder();

        for (String[] base : ESPTOOL) {
            List<String> cmd = new ArrayList<>(List.of(base));
            cmd.add("image-info");
            cmd.add(binFile.getAbsolutePath());
            try {
                Process process = new ProcessBuilder(cmd).redirectErrorStream(true).start();
                StringBuilder output = new StringBuilder();
                try (BufferedReader reader = new BufferedReader(
                        new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
                    String line;
                    while ((line = reader.readLine()) != null) {
                        output.append(line).append('\n');
                    }
                }
                if (process.waitFor() == 0) {
                    return parse(output.toString());
                }
            } catch (Exception ex) {
                lastError.value = ex;
            }
        }

        return guessFromSize(binFile);
    }

    private static BinImageInfo parse(String output) {
        String chip = extract(output, "Detected image type:\\s*(\\S+)", "esp32").toLowerCase(Locale.ROOT);
        if (chip.contains("esp8266")) {
            chip = "esp8266";
        } else if (chip.contains("s3")) {
            chip = "esp32s3";
        } else if (chip.contains("s2")) {
            chip = "esp32s2";
        } else if (chip.contains("c3")) {
            chip = "esp32c3";
        } else {
            chip = "esp32";
        }

        String flashMode = extract(output, "Flash mode:\\s*(\\S+)", "dio").toLowerCase(Locale.ROOT);
        String flashFreq = extract(output, "Flash freq:\\s*(\\S+)", "40m").toLowerCase(Locale.ROOT);
        String flashSize = extract(output, "Flash size:\\s*(\\S+)", "detect").toUpperCase(Locale.ROOT);
        String project = extract(output, "Project name:\\s*(\\S+)", "firmware");
        String version = extract(output, "App version:\\s*(\\S+)", "");

        return new BinImageInfo(
                chip,
                "0x10000",
                flashMode,
                flashFreq,
                flashSize,
                project,
                version
        );
    }

    private static BinImageInfo guessFromSize(File binFile) {
        return new BinImageInfo("esp32", "0x10000", "dio", "40m", "detect", binFile.getName(), "");
    }

    private static String extract(String text, String regex, String fallback) {
        Matcher matcher = Pattern.compile(regex).matcher(text);
        return matcher.find() ? matcher.group(1).trim() : fallback;
    }

    private static final class IOExceptionHolder {
        private Exception value;
    }
}

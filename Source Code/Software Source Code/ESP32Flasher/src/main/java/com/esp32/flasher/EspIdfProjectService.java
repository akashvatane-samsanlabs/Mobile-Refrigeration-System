package com.esp32.flasher;

import java.io.File;

public final class EspIdfProjectService {

    private EspIdfProjectService() {
    }

    public static void validateProject(File projectDir) throws FlashException {
        if (projectDir == null || !projectDir.isDirectory()) {
            throw new FlashException(FlashException.ErrorType.INVALID_PROJECT);
        }
        File mainDir = new File(projectDir, "main");
        File rootCmake = new File(projectDir, "CMakeLists.txt");
        File mainCmake = new File(mainDir, "CMakeLists.txt");
        if (!mainDir.isDirectory() || !rootCmake.isFile() || !mainCmake.isFile()) {
            throw new FlashException(FlashException.ErrorType.INVALID_PROJECT,
                    "Expected main/, CMakeLists.txt, and main/CMakeLists.txt (Section 5).");
        }
    }

    public static boolean isBuildComplete(File projectDir) {
        File buildDir = new File(projectDir, "build");
        if (!buildDir.isDirectory()) {
            return false;
        }
        return new File(buildDir, "bootloader").isDirectory()
                || findApplicationBin(buildDir) != null;
    }

    public static File findApplicationBin(File buildDir) {
        File[] bins = buildDir.listFiles((dir, name) -> name.endsWith(".bin"));
        if (bins == null) {
            return null;
        }
        for (File bin : bins) {
            if (!bin.getName().contains("bootloader") && !bin.getName().contains("partition")) {
                return bin;
            }
        }
        return null;
    }
}

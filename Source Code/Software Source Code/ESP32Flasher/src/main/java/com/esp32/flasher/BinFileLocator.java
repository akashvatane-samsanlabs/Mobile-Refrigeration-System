package com.esp32.flasher;

import java.io.File;
import java.util.Locale;

public final class BinFileLocator {

    private static final String[] BOOTLOADER_NAMES = {
            "bootloader.bin", "bootloader/bootloader.bin", "boot.bin"
    };

    private static final String[] PARTITION_NAMES = {
            "partition-table.bin", "partition_table.bin",
            "partition-table/partition-table.bin"
    };

    private BinFileLocator() {
    }

    public record RelatedBins(File bootloader, File partitionTable) {
        public boolean isComplete() {
            return bootloader != null && bootloader.isFile()
                    && partitionTable != null && partitionTable.isFile();
        }
    }

    public static RelatedBins findRelatedBins(File appBin) {
        if (appBin == null || !appBin.isFile()) {
            return new RelatedBins(null, null);
        }

        File dir = appBin.getParentFile();
        File buildDir = new File(dir, "build");

        File bootloader = findFirstExisting(dir, buildDir, BOOTLOADER_NAMES);
        if (bootloader == null) {
            bootloader = findFirstExisting(new File(dir, "bootloader"), buildDir, "bootloader.bin");
        }

        File partition = findFirstExisting(dir, buildDir, PARTITION_NAMES);
        if (partition == null) {
            partition = findFirstExisting(new File(dir, "partition_table"), buildDir, "partition-table.bin");
        }

        return new RelatedBins(bootloader, partition);
    }

    private static File findFirstExisting(File dir, File buildDir, String... names) {
        for (String name : names) {
            File direct = new File(dir, name);
            if (direct.isFile()) {
                return direct;
            }
            if (buildDir.isDirectory()) {
                File inBuild = new File(buildDir, name);
                if (inBuild.isFile()) {
                    return inBuild;
                }
            }
        }
        return null;
    }

    private static File findFirstExisting(File dir, File unused, String fileName) {
        if (dir == null || !dir.isDirectory()) {
            return null;
        }
        File candidate = new File(dir, fileName);
        return candidate.isFile() ? candidate : null;
    }

    public static boolean looksLikeAppOnlyImage(File appBin) {
        if (appBin == null) {
            return false;
        }
        String name = appBin.getName().toLowerCase(Locale.ROOT);
        return !name.contains("merged") && !name.contains("factory") && !name.startsWith("flash");
    }
}

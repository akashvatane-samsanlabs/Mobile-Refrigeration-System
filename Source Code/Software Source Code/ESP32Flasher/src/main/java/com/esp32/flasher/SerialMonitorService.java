package com.esp32.flasher;

import com.fazecast.jSerialComm.SerialPort;

import java.nio.charset.StandardCharsets;
import java.util.function.Consumer;

public class SerialMonitorService {

    private SerialPort port;
    private volatile boolean running;

    public void start(String portName, int baudRate, Consumer<String> onLine) throws FlashException {
        stop();

        FlashValidator.validatePortSelected(portName);
        FlashValidator.validatePortExists(portName);

        port = SerialPort.getCommPort(portName);
        port.setComPortParameters(baudRate, 8, SerialPort.ONE_STOP_BIT, SerialPort.NO_PARITY);
        port.setComPortTimeouts(SerialPort.TIMEOUT_READ_SEMI_BLOCKING, 100, 0);

        if (!port.openPort()) {
            throw new FlashException(FlashException.ErrorType.PORT_IN_USE);
        }

        running = true;
        Thread monitorThread = new Thread(() -> readLoop(onLine), "esp32-serial-monitor");
        monitorThread.setDaemon(true);
        monitorThread.start();
    }

    public void stop() {
        running = false;
        if (port != null && port.isOpen()) {
            port.closePort();
        }
        port = null;
    }

    public boolean isRunning() {
        return running;
    }

    private void readLoop(Consumer<String> onLine) {
        StringBuilder line = new StringBuilder();
        byte[] buffer = new byte[256];

        while (running && port != null && port.isOpen()) {
            int read = port.readBytes(buffer, buffer.length);
            if (read <= 0) {
                continue;
            }

            String chunk = new String(buffer, 0, read, StandardCharsets.UTF_8);
            for (char ch : chunk.toCharArray()) {
                if (ch == '\n' || ch == '\r') {
                    if (line.length() > 0) {
                        onLine.accept(line.toString());
                        line.setLength(0);
                    }
                } else {
                    line.append(ch);
                }
            }
        }
    }
}

package com.esp32.flasher;

import com.fazecast.jSerialComm.SerialPort;

import java.util.Arrays;

public final class SerialPortService {

    private SerialPortService() {
    }

    public static String[] listPortNames() {
        return Arrays.stream(SerialPort.getCommPorts())
                .map(SerialPort::getSystemPortName)
                .sorted()
                .toArray(String[]::new);
    }
}

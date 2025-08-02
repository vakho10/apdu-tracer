package com.github.vakho10.apdutracer;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.function.Consumer;

public class TracerService {

    private String interfaceName;

    private Consumer<String> capturedLineConsumer;

    private Process process;

    public void start() {
        ProcessBuilder builder = new ProcessBuilder(
                "tshark",
                "-i", interfaceName,
                "-Y", "\"_ws.col.info matches \\\"(CCID Packet|URB_BULK)\\\"\"",
                "-T", "fields", // Output format
                "-e", "_ws.col.info",
                "-e", "data",
                "-E", "separator=,",
                "-l"
        );

        // Also, output errors
        builder.redirectErrorStream(true);

        // Start APDU capture...
        new Thread(() -> {
            try {
                process = builder.start();
                try (var inputStream = process.getInputStream();
                     var inputStreamReader = new InputStreamReader(inputStream);
                     var reader = new BufferedReader(inputStreamReader)
                ) {
                    String line;
                    while ((line = reader.readLine()) != null) {
                        String csvLine = line.trim();
                        if (!csvLine.isEmpty()) {
                            capturedLineConsumer.accept(csvLine);
                        }
                    }
                    process.waitFor();
                }
            } catch (Exception e) {
                e.printStackTrace(); // TODO log somewhere?
            }
        }).start();
    }

    public void stop() {
        if (process != null) {
            process.destroy();
        }
    }

    public String getInterfaceName() {
        return interfaceName;
    }

    public void setInterfaceName(String interfaceName) {
        this.interfaceName = interfaceName;
    }

    public Consumer<String> getCapturedLineConsumer() {
        return capturedLineConsumer;
    }

    public void setCapturedLineConsumer(Consumer<String> capturedLineConsumer) {
        this.capturedLineConsumer = capturedLineConsumer;
    }
}

package com.github.vakho10.apdutracer;

import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.function.Consumer;

@Slf4j
@Service
public class TracerService {

    @Setter
    @Getter
    private String interfaceName;

    @Setter
    @Getter
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
                log.error(e.getMessage(), e);
            }
        }).start();
    }

    public void stop() {
        if (process != null) {
            process.destroy();
        }
    }
}

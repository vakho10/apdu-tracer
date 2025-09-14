package com.github.vakho10.apdutracer;

import lombok.extern.slf4j.Slf4j;
import org.cef.CefApp;
import org.springframework.boot.CommandLineRunner;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.Bean;

@Slf4j
@SpringBootApplication
public class TracerApplication {

    public static void main(String[] args) {
        // ensure AWT is allowed to use a display
        System.setProperty("java.awt.headless", "false");

        SpringApplication.run(TracerApplication.class, args);
    }

    @Bean
    CommandLineRunner runAfterStartup() {
        return args -> {
            // Perform startup initialization on platforms that require it.
            if (!CefApp.startup(args)) {
                log.error("Startup initialization failed!");
            }
        };
    }
}

package com.github.vakho10.apdutracer.swing;

import jakarta.annotation.PostConstruct;
import lombok.extern.slf4j.Slf4j;
import org.cef.CefApp;
import org.cef.browser.CefBrowser;
import org.springframework.stereotype.Component;

import javax.swing.*;
import java.awt.event.WindowAdapter;
import java.awt.event.WindowEvent;

@Slf4j
@Component
public class MainFrame extends JFrame {

    private final CefApp cefApp;
    private final CefBrowser cefBrowser;

    public MainFrame(CefApp cefApp, CefBrowser cefBrowser) {
        this.cefApp = cefApp;
        this.cefBrowser = cefBrowser;
    }

    @PostConstruct
    public void init() {
        // Add browser UI to JFrame
        getContentPane().add(cefBrowser.getUIComponent());
        setSize(800, 600);
        setLocationRelativeTo(null); // Center the window on screen
        setVisible(true);

        // Exit CEF and Spring Boot when the window closes
        addWindowListener(new WindowAdapter() {
            @Override
            public void windowClosing(WindowEvent e) {
                cefApp.dispose();
            }
        });
    }
}

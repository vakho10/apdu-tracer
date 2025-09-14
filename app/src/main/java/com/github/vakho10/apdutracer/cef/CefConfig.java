package com.github.vakho10.apdutracer.cef;

import com.github.vakho10.apdutracer.util.AppPortFinder;
import lombok.extern.slf4j.Slf4j;
import org.cef.CefApp;
import org.cef.CefClient;
import org.cef.CefSettings;
import org.cef.browser.CefBrowser;
import org.cef.browser.CefFrame;
import org.cef.browser.CefMessageRouter;
import org.cef.callback.CefContextMenuParams;
import org.cef.callback.CefMenuModel;
import org.cef.handler.CefAppHandlerAdapter;
import org.cef.handler.CefContextMenuHandlerAdapter;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.core.env.Environment;

@Slf4j
@Configuration
public class CefConfig {

    @Bean
    public CefSettings cefSettings() {
        CefSettings settings = new CefSettings();
        settings.windowless_rendering_enabled = false; // no OSR
        return settings;
    }

    @Bean
//    @DependsOn("cefSettings")
    public CefApp cefApp(CefSettings cefSettings) {
        // Initialize CefApp and handle termination
        CefApp.addAppHandler(new CefAppHandlerAdapter(null) {
            @Override
            public void stateHasChanged(CefApp.CefAppState state) {
                if (state == CefApp.CefAppState.TERMINATED) {
                    System.exit(0);
                }
            }
        });
        return CefApp.getInstance(cefSettings);
    }

    @Bean
//    @DependsOn("cefApp")
    public CefClient cefClient(CefApp cefApp) {
        return cefApp.createClient();
    }

    @Bean
    public CefMessageRouter cefMessageRouter() {
        return CefMessageRouter.create();
    }

    @Bean
//    @DependsOn({"cefClient", "cefMessageRouter"})
    public CefBrowser cefBrowser(CefClient cefClient, Environment env, AppPortFinder portFinder) {

        // The production environment won't have a right-click menu
        if (env.matchesProfiles("prod")) {
            cefClient.addContextMenuHandler(new CefContextMenuHandlerAdapter() {
                @Override
                public void onBeforeContextMenu(CefBrowser browser, CefFrame frame, CefContextMenuParams params, CefMenuModel model) {
                    model.clear(); // disables the context menu
                }
            });
        }

        // Create a browser that connects to the server
        String url = "localhost:%d".formatted(portFinder.getPort());
        return cefClient.createBrowser(url, false, false);
    }
}

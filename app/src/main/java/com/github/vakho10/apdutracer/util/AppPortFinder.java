package com.github.vakho10.apdutracer.util;

import lombok.Getter;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.web.context.WebServerInitializedEvent;
import org.springframework.context.ApplicationListener;
import org.springframework.stereotype.Component;

@Component
@Getter
public class AppPortFinder implements ApplicationListener<WebServerInitializedEvent> {

    @Value("${angular.port}")
    private int port;

    @Override
    public void onApplicationEvent(WebServerInitializedEvent event) {
        if (port == 0) {
            // Gets dynamic port number for production
            port = event.getWebServer().getPort();
        }
        ;
    }
}

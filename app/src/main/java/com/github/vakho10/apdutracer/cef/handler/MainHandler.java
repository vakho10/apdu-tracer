package com.github.vakho10.apdutracer.cef.handler;

import lombok.extern.slf4j.Slf4j;
import org.cef.browser.CefBrowser;
import org.cef.browser.CefFrame;
import org.cef.callback.CefQueryCallback;
import org.springframework.stereotype.Controller;

import javax.swing.*;

@Slf4j
@Controller
public class MainHandler extends AbstractHandler {

    @Override
    public boolean onQuery(CefBrowser browser, CefFrame frame, long query_id, String request, boolean persistent,
                           CefQueryCallback callback) {
        if ("closeApp".equals(request)) {
            log.debug("Closing app via Angular");
            SwingUtilities.invokeLater(() -> browser.close(true));
            callback.success("App closed");
            return true;
        }
        // Not handled.
        return false;
    }

    @Override
    public boolean isFirst() {
        return true;
    }
}

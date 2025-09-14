package com.github.vakho10.apdutracer.cef.handler;

import org.cef.handler.CefMessageRouterHandlerAdapter;

public abstract class AbstractHandler extends CefMessageRouterHandlerAdapter {

    public abstract boolean isFirst();
}

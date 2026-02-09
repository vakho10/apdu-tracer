package com.github.vakho10.apdutracer.apdu.response;

import javafx.scene.control.TextField;

public class Controller {
    public TextField fieldResponseData;
    public TextField fieldStatus;

    public void setResponseData(String value) {
        fieldResponseData.setText(value);
    }

    public void setStatus(String value) {
        fieldStatus.setText(value);
    }
}

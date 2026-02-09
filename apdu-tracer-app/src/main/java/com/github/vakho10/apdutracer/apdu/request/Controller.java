package com.github.vakho10.apdutracer.apdu.request;

import javafx.fxml.Initializable;
import javafx.scene.control.TextField;

import java.net.URL;
import java.util.ResourceBundle;

public class Controller implements Initializable {
    public TextField fieldCLA;
    public TextField fieldINS;
    public TextField fieldP1;
    public TextField fieldP2;
    public TextField fieldLc;
    public TextField fieldRequestData;
    public TextField fieldLe;

    public void setCLA(String value) {
        fieldCLA.setText(value);
    }

    public void setINS(String value) {
        fieldINS.setText(value);
    }

    public void setP1(String value) {
        fieldP1.setText(value);
    }

    public void setP2(String value) {
        fieldP2.setText(value);
    }

    public void setLc(String value) {
        fieldLc.setText(value);
        fieldLc.setVisible(value != null);
    }

    public void setRequestData(String value) {
        fieldRequestData.setText(value);
        fieldRequestData.setVisible(value != null);
    }

    public void setLe(String value) {
        fieldLe.setText(value);
        fieldRequestData.setVisible(value != null);
    }

    @Override
    public void initialize(URL location, ResourceBundle resources) {
        // Bind them so they always match
        fieldLc.managedProperty().bind(fieldLc.visibleProperty());
        fieldRequestData.managedProperty().bind(fieldRequestData.visibleProperty());
        fieldLe.managedProperty().bind(fieldLe.visibleProperty());
    }
}

package com.github.vakho10.apdutracer.apdu.request;

import javafx.fxml.Initializable;
import javafx.scene.control.Label;
import javafx.scene.control.TextField;
import javafx.scene.layout.VBox;

import java.net.URL;
import java.util.ResourceBundle;

public class Controller implements Initializable {

    // Required fields
    public Label labelType;
    public TextField fieldCLA;
    public TextField fieldINS;
    public TextField fieldP1;
    public TextField fieldP2;

    // Optional fields
    public TextField fieldLc;
    public VBox vboxLc;

    public TextField fieldRequestData;
    public VBox vboxRequestData;

    public TextField fieldLe;
    public VBox vboxLe;

    public void setType(String type) {
        labelType.setText(type);
    }

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
        vboxLc.setVisible(value != null);
    }

    public void setRequestData(String value) {
        fieldRequestData.setText(value);
        vboxRequestData.setVisible(value != null);
    }

    public void setLe(String value) {
        fieldLe.setText(value);
        vboxLe.setVisible(value != null);
    }

    @Override
    public void initialize(URL location, ResourceBundle resources) {
        // Bind them so they always match
        vboxLc.managedProperty().bind(vboxLc.visibleProperty());
        vboxRequestData.managedProperty().bind(vboxRequestData.visibleProperty());
        vboxLe.managedProperty().bind(vboxLe.visibleProperty());
    }
}

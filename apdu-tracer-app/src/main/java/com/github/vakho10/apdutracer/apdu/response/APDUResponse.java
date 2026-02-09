package com.github.vakho10.apdutracer.apdu.response;

import javafx.fxml.FXMLLoader;
import javafx.scene.Node;
import javafx.scene.Parent;

import java.io.IOException;
import java.net.URL;
import java.util.Arrays;
import java.util.HexFormat;

public class APDUResponse {

    public static HexFormat HEX_FORMAT = HexFormat.of();

    private byte[] data;
    private int sw1;
    private int sw2;

    public static APDUResponse from(String apduHexString) {
        byte[] apdu = HEX_FORMAT.parseHex(apduHexString);
        var apduResponse = new APDUResponse();

        if (apdu.length > 4) {
            // Populate data
            apduResponse.data = Arrays.copyOfRange(apdu, 0, apdu.length - 4);
        }
        apduResponse.sw1 = Byte.toUnsignedInt(apdu[apdu.length - 2]);
        apduResponse.sw2 = Byte.toUnsignedInt(apdu[apdu.length - 1]);
        return apduResponse;
    }

    public byte[] getData() {
        return data;
    }

    public String getDataAsHexString() {
        if (data == null) {
            return "";
        }
        return HEX_FORMAT.formatHex(data);
    }

    public int getSw1() {
        return sw1;
    }

    public int getSw2() {
        return sw2;
    }

    public String getSwAsHexString() {
        return HEX_FORMAT.formatHex(new byte[]{(byte) sw1, (byte) sw2});
    }

    @Override
    public String toString() {
        return "APDUResponse{" +
                "data=" + Arrays.toString(data) +
                ", sw1=" + sw1 +
                ", sw2=" + sw2 +
                '}';
    }

    public Node toNode() {
        // Convert data array to hex, or use empty string if no data is returned
        String responseDataHex = (data != null && data.length > 0) ? getDataAsHexString().toUpperCase() : "";

        // Get the 4-character status word (e.g., "9000")
        String statusHex = getSwAsHexString().toUpperCase();

        try {
            URL url = APDUResponse.class.getResource("view.fxml");
            FXMLLoader fxmlLoader = new FXMLLoader(url);
            Parent parent = fxmlLoader.load();
            Controller controller = fxmlLoader.getController();
            controller.setResponseData(responseDataHex);
            controller.setStatus(statusHex);
            return parent;
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }
}

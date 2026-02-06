package com.github.vakho10.apdutracer.apdu;

import javafx.geometry.Insets;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;

import java.util.Arrays;
import java.util.HexFormat;

import static com.github.vakho10.apdutracer.apdu.Utils.createFieldBlock;

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

    public HBox toNode() {
        // Convert data array to hex, or use empty string if no data is returned
        String dataHex = (data != null && data.length > 0) ? getDataAsHexString().toUpperCase() : "";

        // Get the 4-character status word (e.g., "9000")
        String statusHex = getSwAsHexString().toUpperCase();

        return new Node(dataHex, statusHex);
    }

    public class Node extends HBox {

        public Node(String dataHex, String statusHex) {

            // 1. Setup the main HBox (The root container)
            this.setSpacing(4.0);
            this.setPadding(new Insets(8, 8, 8, 8));

            // 2. Create the Response Data segment (HGrow ALWAYS)
            VBox dataBlock = createFieldBlock("Response data", dataHex, null, true);
            HBox.setHgrow(dataBlock, Priority.ALWAYS);

            // 3. Create the Status segment (Fixed width for 4-character hex like '9000')
            VBox statusBlock = createFieldBlock("Status", statusHex, 48.0, false);

            // 4. Add children
            this.getChildren().addAll(dataBlock, statusBlock);
        }
    }
}

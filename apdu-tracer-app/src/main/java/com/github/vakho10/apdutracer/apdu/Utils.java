package com.github.vakho10.apdutracer.apdu;

import javafx.geometry.Pos;
import javafx.scene.control.Label;
import javafx.scene.control.TextField;
import javafx.scene.layout.VBox;

public class Utils {

    /**
     * Helper method to replicate the repetitive VBox + Label + TextField structure
     */
    public static VBox createFieldBlock(String labelText, String initialValue, Double prefWidth, boolean grow) {
        VBox container = new VBox(2.0);

        Label label = new Label(labelText);
        label.setAlignment(Pos.CENTER);
        label.setMaxWidth(Double.MAX_VALUE);

        TextField textField = new TextField(initialValue);
        textField.setAlignment(Pos.CENTER);
        textField.setEditable(false);
        textField.setStyle("-fx-background-color: white; -fx-background-insets: 0; -fx-padding: 0;");

        if (prefWidth != null) {
            textField.setPrefWidth(prefWidth);
        }

        if (grow) {
            textField.setMaxWidth(Double.MAX_VALUE);
        }

        container.getChildren().addAll(label, textField);
        return container;
    }
}

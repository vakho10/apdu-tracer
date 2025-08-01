package com.github.vakho.apdutracer;

import atlantafx.base.theme.*;
import javafx.application.Application;
import javafx.application.Platform;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.scene.control.Alert;
import javafx.scene.control.Alert.AlertType;
import javafx.scene.image.Image;
import javafx.stage.Stage;

import java.io.IOException;

public class TracerApplication extends Application {

    private TracerController controller;

    @Override
    public void start(Stage stage) throws IOException {
        // find more themes in 'atlantafx.base.theme' package
        Application.setUserAgentStylesheet(new NordLight().getUserAgentStylesheet());

        // Add application icon
        try (var logoInputStream = TracerApplication.class.getResourceAsStream("/logo/icon_32.png")) {
            if (logoInputStream != null) {
                stage.getIcons().add(new Image(logoInputStream));
            }
        }
        FXMLLoader fxmlLoader = new FXMLLoader(TracerApplication.class.getResource("tracer-view.fxml"));
        Parent parent = fxmlLoader.load();
        controller = fxmlLoader.getController();
        controller.initStage(stage);
        Scene scene = new Scene(parent);
        stage.setTitle("APDU Tracer");
        stage.setMinWidth(900);
        stage.setMinHeight(600);
        stage.setScene(scene);
        stage.show();
    }

    @Override
    public void stop() {
        controller.shutdown();
    }

    public static void main(String[] args) {
        Thread.setDefaultUncaughtExceptionHandler((thread, throwable) -> {
            showErrorDialog(throwable);
        });
        launch();
    }

    private static void showErrorDialog(Throwable throwable) {
        Platform.runLater(() -> {
            Alert alert = new Alert(AlertType.ERROR);
            alert.setTitle("Error Dialog");
            alert.setHeaderText("An Exception Occurred");
            if (throwable.getMessage() != null) {
                alert.setContentText(throwable.getMessage());
            }
            // Print stack trace for debugging (optional)
            throwable.printStackTrace(); // TODO log somewhere?
            alert.showAndWait();
        });
    }
}

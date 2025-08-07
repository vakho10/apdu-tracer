package com.github.vakho10.apdutracer;

import atlantafx.base.theme.NordLight;
import javafx.application.Application;
import javafx.application.Platform;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.scene.control.Alert;
import javafx.scene.control.Alert.AlertType;
import javafx.scene.image.Image;
import javafx.stage.Stage;
import lombok.extern.slf4j.Slf4j;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;

import java.io.IOException;

@Slf4j
@SpringBootApplication
public class TracerApplication extends Application {

    private static ConfigurableApplicationContext context;
    private TracerController controller;

    @Override
    public void init() {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(TracerApplication.class);
        context = builder.run(getParameters().getRaw().toArray(new String[0]));
    }

    @Override
    public void start(Stage stage) throws IOException {
        log.info("Called start(..) with primaryStage");

        // find more themes in 'atlantafx.base.theme' package
        Application.setUserAgentStylesheet(new NordLight().getUserAgentStylesheet());

        // Add application icon
        try (var logoInputStream = TracerApplication.class.getResourceAsStream("/logo/icon_32.png")) {
            if (logoInputStream != null) {
                stage.getIcons().add(new Image(logoInputStream));
            }
        }
        FXMLLoader fxmlLoader = new FXMLLoader(TracerApplication.class.getResource("tracer-view.fxml"));

        // Sets the JavaFX controller factory to use Spring Boot's controller factory,
        // ensuring that Spring Boot manages the JavaFX controller's lifecycle.
        fxmlLoader.setControllerFactory(context::getBean);

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
        log.info("Called stop()");
        controller.shutdown();
        context.stop();
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
            log.error("An Exception Occurred", throwable);
            alert.showAndWait();
        });
    }
}

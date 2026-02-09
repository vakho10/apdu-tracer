package com.github.vakho10.apdutracer;

import com.github.vakho10.apdutracer.apdu.CommandType;
import com.github.vakho10.apdutracer.apdu.request.APDURequest;
import com.github.vakho10.apdutracer.apdu.response.APDUResponse;
import javafx.application.Platform;
import javafx.event.ActionEvent;
import javafx.fxml.Initializable;
import javafx.geometry.Insets;
import javafx.scene.control.*;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;
import javafx.scene.paint.Color;
import javafx.scene.text.Text;
import javafx.scene.text.TextAlignment;
import javafx.stage.FileChooser;
import javafx.stage.FileChooser.ExtensionFilter;
import jfx.incubator.scene.control.richtext.RichTextArea;
import jfx.incubator.scene.control.richtext.model.StyleAttributeMap;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Component;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.URL;
import java.nio.file.Files;
import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.List;
import java.util.ResourceBundle;
import java.util.StringJoiner;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

@Slf4j
@Component
public class TracerController extends AbstractController implements Initializable {

    private ExecutorService executorService;

    public ComboBox<String> interfaceComboBox;
    public CheckBox checkBoxBulk;
    public CheckBox checkBoxUnknown;
    public ListView<Object> modernListView; // Modern container
    public VBox textualVBox; // Textual container
    public ToggleButton btnAutoScroll;
    public ToggleButton btnStartStop;
    public Button btnClear;

    private TracerService tracerService;

    private RichTextArea richTextArea;

    private FileChooser fileChooser;

    @Override
    public void initialize(URL url, ResourceBundle resourceBundle) {
        executorService = Executors.newSingleThreadExecutor();
        tracerService = new TracerService();
        tracerService.setCapturedLineConsumer(this::consumeLine);

        refreshInterfaces();

        interfaceComboBox.setOnAction(e -> {
            tracerService.setInterfaceName(interfaceComboBox.getValue());
        });

        // RichTextArea setup
        richTextArea = new RichTextArea();
        richTextArea.setEditable(false);
        VBox.setVgrow(richTextArea, Priority.ALWAYS);
        textualVBox.getChildren().add(richTextArea);

        // File chooser
        fileChooser = new FileChooser();
        fileChooser.getExtensionFilters().addAll(new ExtensionFilter("Text Files", "*.txt"));

        // This disables the ability to select items entirely
        modernListView.setSelectionModel(new NoSelectionModel<>());

        // Render list view items
        modernListView.setCellFactory(lv -> new ListCell<>() {
            @Override
            protected void updateItem(Object item, boolean empty) {
                super.updateItem(item, empty);
                if (empty || item == null) {
                    setGraphic(null);
                } else {
                    // See if we have atomic object
                    if (item instanceof AtomicReference<?> atomicReference) {
                        item = atomicReference.get();
                    }

                    // Construct specific node (APDU request or response)
                    if (item instanceof APDURequest request) {
                        var node = request.toNode();
                        node.setStyle("-fx-background-color: lightblue;");
                        setGraphic(node);
                    } else if (item instanceof APDUResponse response) {
                        var node = response.toNode();
                        node.setStyle("-fx-background-color: lightgreen;");
                        setGraphic(node);
                    } else if (item instanceof String itemText) {
                        var container = new HBox();
                        container.setSpacing(4.0);
                        container.setPadding(new Insets(8, 8, 8, 8));
                        container.setStyle("-fx-background-color: black;");
                        Text text = new Text(itemText);
                        text.setTextAlignment(TextAlignment.CENTER);
                        text.maxWidth(Double.MAX_VALUE);
                        text.setStyle("-fx-fill: white;");
                        HBox.setHgrow(text, Priority.ALWAYS);
                        container.getChildren().add(text);
                        setGraphic(container);
                    } else {
                        Text errorText = new Text("Can't display the node!");
                        errorText.setStyle("-fx-fill: red;");
                        setGraphic(errorText);
                    }
                }
            }
        });
    }

    public void startStop(ActionEvent event) {
        ToggleButton btn = (ToggleButton) event.getTarget();
        if (btn.isSelected()) {
            tracerService.start();
        } else {
            tracerService.stop();
        }
    }

    public void clear() {
        modernListView.getItems().clear();
        richTextArea.clear();
    }

    public void refreshInterfaces() {
        executorService.submit(() -> {
            List<String> interfaces = new ArrayList<>();
            AtomicInteger selectByDefaultIndex = new AtomicInteger(0);
            try {
                Process process = new ProcessBuilder("tshark", "-D").start();
                try (var inputStream = process.getInputStream();
                     var inputStreamReader = new InputStreamReader(inputStream);
                     var reader = new BufferedReader(inputStreamReader)
                ) {
                    String line;
                    int idx = 0;
                    while ((line = reader.readLine()) != null) {
                        // Example: "1. eth0 (Ethernet)"
                        int dotIndex = line.indexOf('.');
                        if (dotIndex != -1 && dotIndex + 2 < line.length()) {
                            String namePart = line.substring(dotIndex + 2).trim(); // "eth0 (Ethernet)"
                            int parenIndex = namePart.indexOf(" (");
                            String cleanName = (parenIndex != -1) ? namePart.substring(0, parenIndex) : namePart;
                            interfaces.add(cleanName.trim());
                            if (cleanName.contains("USB")) {
                                selectByDefaultIndex.set(idx);
                            }
                        }
                        ++idx;
                    }
                    process.waitFor();
                }
            } catch (Exception e) {
                log.error(e.getMessage(), e);
            }
            Platform.runLater(() -> {
                interfaceComboBox.getItems().setAll(interfaces);
                interfaceComboBox.getSelectionModel().select(selectByDefaultIndex.get());
            });
        });
    }

    @Override
    public void shutdown() {
        tracerService.stop();
        executorService.shutdownNow();
    }

    public void saveToFile() throws IOException {
        File file = fileChooser.showSaveDialog(stage);
        if (file != null) {
            Files.writeString(file.toPath(), getAllRichText());
        }
    }

    public void close() {
        Platform.exit();
    }

    private void consumeLine(String line) {
        if (line == null) {
            return;
        }

        // Parse and determine the command type
        String[] values = line.split(",");
        CommandType commandType = CommandType.from(values[0]);

        // Skip UNKNOWN commands?
        if (checkBoxUnknown.isSelected() && commandType.equals(CommandType.UNKNOWN)) {
            return;
        }
        // Skip BULK commands?
        if (checkBoxBulk.isSelected()
                && (commandType.equals(CommandType.URB_BULK_IN) || commandType.equals(CommandType.URB_BULK_OUT))) {
            return;
        }

        var time = LocalTime.now().format(DateTimeFormatter.ofPattern("HH:mm:ss.SSS"));

        final AtomicReference<Object> commandObject = new AtomicReference<>(commandType.name());
        StringJoiner sj = new StringJoiner("] [", "[", "]");
        sj.add(commandType.name());
        if (values.length > 1) {
            // APDU Request & Response
            switch (commandType) {
                case TRANSFER_BLOCK:
                    sj.add("APDU_REQUEST");
                    APDURequest apduRequest = APDURequest.from(values[1]);
                    APDURequest.Type apduRequestType = apduRequest.getType();
                    sj.add(apduRequestType.name());
                    sj.add(values[1].toUpperCase());
                    // Also provide APDU data for some select commands
                    // TODO Add parsing all data infos?!
                    if (apduRequestType.equals(APDURequest.Type.SELECT_BY_FILE_ID)
                            || apduRequestType.equals(APDURequest.Type.SELECT_ELEMENTARY_FILE)
                            || apduRequestType.equals(APDURequest.Type.SELECT_EF_OR_DF_UNDER_CURRENT_DF)
                            || apduRequestType.equals(APDURequest.Type.SELECT_CHILD_DF_BY_ID)) {
                        sj.add(apduRequest.getDataAsHexString().toUpperCase());
                    }
                    commandObject.set(apduRequest);
                    break;
                case DATA_BLOCK:
                    sj.add("APDU_RESPONSE");
                    APDUResponse apduResponse = APDUResponse.from(values[1]);
                    if (apduResponse.getData() != null && apduResponse.getData().length > 0) {
                        sj.add(apduResponse.getDataAsHexString().toUpperCase());
                    }
                    sj.add(apduResponse.getSwAsHexString().toUpperCase());
                    commandObject.set(apduResponse);
                    break;
                default:
                    sj.add(commandType.name());
                    for (String value : values) {
                        sj.add(value);
                    }
                    commandObject.set(sj.toString());
                    break;
            }
        }

        Platform.runLater(() -> {
            // Modern
            modernListView.getItems().add(commandObject);

            // Auto-scroll logic is built-in to ListView
            if (btnAutoScroll.isSelected()) {
                modernListView.scrollTo(modernListView.getItems().size() - 1);
            }

            // Textual
            // 1. Prepare the text
            String logEntry = String.format("[%s] %s\n", time, sj);

            // 2. Get the style map (using the helper we discussed earlier)
            StyleAttributeMap richStyle = determineStyle(commandType);

            // 3. Append text and style in one shot
            richTextArea.appendText(logEntry, richStyle);

            // 4. Auto-scroll logic
            if (btnAutoScroll.isSelected()) {
                richTextArea.select(richTextArea.getDocumentEnd());
            }
        });
    }

    private StyleAttributeMap determineStyle(CommandType commandType) {
        var builder = StyleAttributeMap.builder();
        switch (commandType) {
            case TRANSFER_BLOCK -> builder.setTextColor(Color.CORNFLOWERBLUE);
            case DATA_BLOCK -> builder.setTextColor(Color.LIMEGREEN);
            case UNKNOWN -> builder.setTextColor(Color.ORANGERED);
            default -> builder.setTextColor(Color.BLACK);
        }
        return builder.build();
    }

    private String getAllRichText() {
        StringBuilder sb = new StringBuilder();
        int count = richTextArea.getParagraphCount();
        for (int i = 0; i < count; i++) {
            sb.append(richTextArea.getPlainText(i));
            sb.append("\n");
        }
        return sb.toString();
    }
}

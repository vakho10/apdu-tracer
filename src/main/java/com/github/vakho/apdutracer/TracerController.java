package com.github.vakho.apdutracer;

import com.github.vakho.apdutracer.apdu.APDURequest;
import com.github.vakho.apdutracer.apdu.APDUResponse;
import com.github.vakho.apdutracer.apdu.CommandType;
import javafx.application.Platform;
import javafx.event.ActionEvent;
import javafx.fxml.Initializable;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.ComboBox;
import javafx.scene.control.ToggleButton;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.stage.FileChooser;
import javafx.stage.FileChooser.ExtensionFilter;
import org.fxmisc.flowless.VirtualizedScrollPane;
import org.fxmisc.richtext.CodeArea;

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

public class TracerController extends AbstractController implements Initializable {

    private ExecutorService executorService;

    public ComboBox<String> interfaceComboBox;
    public CheckBox checkBoxBulk;
    public CheckBox checkBoxUnknown;
    public HBox hbox;
    public ToggleButton btnAutoScroll;
    public ToggleButton btnStartStop;
    public Button btnClear;

    private TracerService tracerService;

    private CodeArea codeArea;
    private VirtualizedScrollPane<CodeArea> vsPane;

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

        // Code area and scroll pane
        codeArea = new CodeArea();
        codeArea.setEditable(false);
        vsPane = new VirtualizedScrollPane<>(codeArea);
        codeArea.getStyleClass().add("vs-pane");
        HBox.setHgrow(vsPane, Priority.ALWAYS);
        hbox.getChildren().add(vsPane);

        // File chooser
        fileChooser = new FileChooser();
        fileChooser.getExtensionFilters().addAll(new ExtensionFilter("Text Files", "*.txt"));
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
        codeArea.clear();
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
                e.printStackTrace(); // TODO log somewhere?
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
            Files.writeString(file.toPath(), codeArea.getText());
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
                    break;
                case DATA_BLOCK:
                    sj.add("APDU_RESPONSE");
                    APDUResponse apduResponse = APDUResponse.from(values[1]);
                    if (apduResponse.getData() != null && apduResponse.getData().length > 0) {
                        sj.add(apduResponse.getDataAsHexString().toUpperCase());
                    }
                    sj.add(apduResponse.getSwAsHexString().toUpperCase());
                    break;
                default:
                    sj.add(commandType.name());
                    for (String value : values) {
                        sj.add(value);
                    }
                    break;
            }
        }

        var time = LocalTime.now().format(DateTimeFormatter.ofPattern("HH:mm:ss.SSS"));
        Platform.runLater(() -> {
            int start = codeArea.getLength();
            codeArea.appendText(String.format("[%s] %s\n", time, sj));
            int end = codeArea.getLength();

            String style = determineStyle(commandType);
            codeArea.setStyle(start, end, List.of(style));

            // Check if we should auto-scroll to the end
            if (btnAutoScroll.isSelected()) {
                codeArea.moveTo(codeArea.getLength());
                codeArea.requestFollowCaret();
            }
        });
    }

    private String determineStyle(CommandType commandType) {
        return switch (commandType) {
            case TRANSFER_BLOCK -> "apdu-request";
            case DATA_BLOCK -> "apdu-response";
            case UNKNOWN -> "unknown";
            default -> "default";
        };
    }
}
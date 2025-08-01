package com.github.vakho.apdutracer;

import javafx.stage.Stage;

import java.util.Optional;
import java.util.function.Consumer;

public abstract class AbstractController {

    private Optional<Consumer<Stage>> afterStageInit;

    protected Stage stage;

    public AbstractController() {
        afterStageInit = Optional.empty();
    }

    public AbstractController(Consumer<Stage> afterStageInit) {
        this.afterStageInit = Optional.of(afterStageInit);
    }

    public void setAfterStageInit(Consumer<Stage> afterStageInit) {
        this.afterStageInit = Optional.of(afterStageInit);
    }

    public void initStage(Stage stage) {
        this.stage = stage;
        this.afterStageInit.ifPresent((i) -> i.accept(stage));
    }

    public void shutdown() {
    }
}
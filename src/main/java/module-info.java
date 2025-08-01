module com.github.vakho.apdutracer {
    requires javafx.controls;
    requires javafx.fxml;
    requires org.fxmisc.richtext;
    requires org.fxmisc.flowless;
    requires org.kordamp.ikonli.core;
    requires org.kordamp.ikonli.javafx;
    requires org.kordamp.ikonli.fontawesome;
    requires atlantafx.base;

    opens com.github.vakho.apdutracer to javafx.fxml;
    exports com.github.vakho.apdutracer;
    exports com.github.vakho.apdutracer.apdu;
    opens com.github.vakho.apdutracer.apdu to javafx.fxml;
}
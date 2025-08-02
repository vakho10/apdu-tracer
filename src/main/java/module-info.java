module com.github.vakho10.apdutracer {
    requires javafx.controls;
    requires javafx.fxml;
    requires org.fxmisc.richtext;
    requires org.fxmisc.flowless;
    requires org.kordamp.ikonli.core;
    requires org.kordamp.ikonli.javafx;
    requires org.kordamp.ikonli.fontawesome;
    requires atlantafx.base;

    opens com.github.vakho10.apdutracer to javafx.fxml;
    exports com.github.vakho10.apdutracer;
    exports com.github.vakho10.apdutracer.apdu;
    opens com.github.vakho10.apdutracer.apdu to javafx.fxml;
}
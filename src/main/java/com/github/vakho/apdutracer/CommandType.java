package com.github.vakho.apdutracer;

public enum CommandType {
    ESCAPE("Escape"),                   // Vendor-specific Escape commands (e.g. [0200], [0c23], etc.)
    ICC_POWER_ON("ICC Power On"),       // Powers on the smart card
    GET_PARAMETERS("Get Parameters"),   // Requests current reader/card communication parameters
    SET_PARAMETERS("Set Parameters"),   // Sets communication parameters
    TRANSFER_BLOCK("Transfer Block"),   // Contains actual APDUs to be sent to the card
    PARAMETERS("Parameters"),           // Response from Get/Set Parameters
    DATA_BLOCK("Data Block"),           // Response data from the card (e.g. APDU response)
    URB_BULK_OUT("URB_BULK out"),       // USB Bulk transfer from PC to reader (transport layer)
    URB_BULK_IN("URB_BULK in"),         // USB Bulk transfer from reader to PC (transport layer)
    UNKNOWN(null);                      // Catch-all for unclassified or malformed lines

    private final String commandString;

    CommandType(String commandString) {
        this.commandString = commandString;
    }

    public static CommandType from(String commandString) {
        if (commandString == null) {
            return UNKNOWN;
        }
        for (CommandType type : values()) {
            if (type.commandString != null && commandString.contains(type.commandString)) {
                return type;
            }
        }
        return UNKNOWN;
    }
}

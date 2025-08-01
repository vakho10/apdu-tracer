package com.github.vakho.apdutracer.apdu;

import java.util.Arrays;
import java.util.HexFormat;

public class APDURequest {

    public static HexFormat HEX_FORMAT = HexFormat.of();

    private int cla;
    private int ins;
    private int p1;
    private int p2;
    private Integer lc;
    private byte[] data;
    private Integer le;

    private Type type;

    public static APDURequest from(String apduHexString) {
        byte[] apdu = HEX_FORMAT.parseHex(apduHexString);
        var apduRequest = new APDURequest();

        if (apdu.length < 4) {
            // Not enough bytes for header — fill minimal and empty data
            apduRequest.cla = apdu.length > 0 ? Byte.toUnsignedInt(apdu[0]) : 0;
            apduRequest.ins = apdu.length > 1 ? Byte.toUnsignedInt(apdu[1]) : 0;
            apduRequest.p1 = apdu.length > 2 ? Byte.toUnsignedInt(apdu[2]) : 0;
            apduRequest.p2 = 0;
            apduRequest.type = Type.from(apduRequest.ins, apduRequest.p1, apduRequest.p2);
            apduRequest.lc = null;
            apduRequest.data = new byte[0];
            apduRequest.le = null;
            return apduRequest;
        }
        apduRequest.cla = Byte.toUnsignedInt(apdu[0]);
        apduRequest.ins = Byte.toUnsignedInt(apdu[1]);
        apduRequest.p1 = Byte.toUnsignedInt(apdu[2]);
        apduRequest.p2 = Byte.toUnsignedInt(apdu[3]);
        apduRequest.type = Type.from(apduRequest.ins, apduRequest.p1, apduRequest.p2);

        if (apdu.length == 4) {
            // Case 1: no Lc, no Le
            apduRequest.lc = null;
            apduRequest.data = new byte[0];
            apduRequest.le = null;
            return apduRequest;
        }
        if (apdu.length == 5) {
            // Case 2s: short Le only
            apduRequest.lc = null;
            apduRequest.data = new byte[0];
            apduRequest.le = Byte.toUnsignedInt(apdu[4]);
            return apduRequest;
        }
        if (apdu[4] != 0x00) {
            // Short Lc format
            int lc = Byte.toUnsignedInt(apdu[4]);
            apduRequest.lc = lc;

            // Check if enough length for data
            if (apdu.length >= 5 + lc) {
                apduRequest.data = Arrays.copyOfRange(apdu, 5, 5 + lc);
            } else {
                apduRequest.data = new byte[0]; // fallback empty if missing
            }

            if (apdu.length == 5 + lc + 1) {
                apduRequest.le = Byte.toUnsignedInt(apdu[5 + lc]);
            } else {
                apduRequest.le = null; // No Le or unknown
            }
            return apduRequest;
        } else {
            // Extended Lc format
            if (apdu.length < 7) {
                // Too short for extended Lc, fallback
                apduRequest.lc = null;
                apduRequest.data = new byte[0];
                apduRequest.le = null;
                return apduRequest;
            }

            int lc = ((Byte.toUnsignedInt(apdu[5]) << 8) | Byte.toUnsignedInt(apdu[6]));
            apduRequest.lc = lc;

            int dataStart = 7;
            int dataEnd = dataStart + lc;

            if (apdu.length >= dataEnd) {
                apduRequest.data = Arrays.copyOfRange(apdu, dataStart, Math.min(dataEnd, apdu.length));
            } else {
                apduRequest.data = new byte[0];
            }

            if (apdu.length == dataEnd + 2) {
                apduRequest.le = ((Byte.toUnsignedInt(apdu[dataEnd]) << 8) | Byte.toUnsignedInt(apdu[dataEnd + 1]));
            } else {
                apduRequest.le = null;
            }
            return apduRequest;
        }
    }

    public int getCla() {
        return cla;
    }

    public int getIns() {
        return ins;
    }

    public int getP1() {
        return p1;
    }

    public int getP2() {
        return p2;
    }

    public Type getType() {
        return type;
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

    public Integer getLc() {
        return lc;
    }

    public Integer getLe() {
        return le;
    }

    @Override
    public String toString() {
        return "APDURequest{" +
                "cla=" + cla +
                ", ins=" + ins +
                ", p1=" + p1 +
                ", p2=" + p2 +
                ", lc=" + lc +
                ", data=" + Arrays.toString(data) +
                ", le=" + le +
                ", type=" + type +
                '}';
    }

    public enum Type {
        // SELECT variants
        SELECT_BY_FILE_ID,                // Select file by 2-byte File ID (FID)
        SELECT_CHILD_DF_BY_ID,            // Select child Dedicated File (DF) by File ID
        SELECT_ELEMENTARY_FILE,           // Select Elementary File (EF) under current DF
        SELECT_EF_OR_DF_UNDER_CURRENT_DF, // Select EF or DF under current DF
        SELECT_BY_DF_NAME_OR_AID,         // Select by DF name or Application ID (AID)
        SELECT_PARENT_DF,                 // Select parent DF (one level up)
        SELECT_ANCESTOR_DF,               // Select ancestor DF (multiple levels up)
        SELECT_DF_BY_PATH_FROM_MF,        // Select DF by path from Master File (MF)
        SELECT_UNKNOWN,                   // Unknown SELECT command (should not happen!)

        // Common commands
        GET_RESPONSE,
        GET_DATA,
        READ_BINARY,
        WRITE_BINARY,
        UPDATE_BINARY,
        READ_RECORD,
        UPDATE_RECORD,
        VERIFY,
        EXTERNAL_AUTHENTICATE,
        INTERNAL_AUTHENTICATE,
        GENERATE_ASYMMETRIC_KEYPAIR,
        GET_CHALLENGE,

        // MSE (Manage Security Environment) variants
        MANAGE_SECURITY_ENVIRONMENT,
        MSE_SET_AT,   // Set Authentication Template
        MSE_SET_DST,  // Set Digital Signature Template
        MSE_SET_KAT,  // Set Key Agreement Template

        // PSO (Perform Security Operation) variants
        PERFORM_SECURITY_OPERATION,
        PSO_COMPUTE_DIGITAL_SIGNATURE,
        PSO_VERIFY_CERTIFICATE,
        PSO_DECIPHER,

        // Change/Unblock/Reset PIN or reference data
        CHANGE_REFERENCE_DATA,
        CHANGE_PIN,
        UNBLOCK_PIN,
        SET_PIN,
        RESET_REFERENCE_DATA,

        // Other
        GENERAL_AUTHENTICATE,
        PUT_DATA,
        DELETE_FILE,
        ACTIVATE_FILE,
        DEACTIVATE_FILE,

        // Fallback
        UNKNOWN;

        public static Type from(int ins, int p1, int p2) {
            return switch (ins) {
                case 0xA4 -> switch (p1) {
                    case 0x00 -> SELECT_BY_FILE_ID;
                    case 0x01 -> SELECT_CHILD_DF_BY_ID;
                    case 0x02 -> SELECT_ELEMENTARY_FILE;
                    case 0x03 -> SELECT_EF_OR_DF_UNDER_CURRENT_DF;
                    case 0x04 -> SELECT_BY_DF_NAME_OR_AID;
                    case 0x08 -> SELECT_PARENT_DF;
                    case 0x09 -> SELECT_ANCESTOR_DF;
                    case 0x0C -> SELECT_DF_BY_PATH_FROM_MF;
                    default -> SELECT_UNKNOWN;
                };
                case 0xC0 -> GET_RESPONSE;
                case 0xCA -> GET_DATA;
                case 0xB0 -> READ_BINARY;
                case 0xD0 -> WRITE_BINARY;
                case 0xD6 -> UPDATE_BINARY;
                case 0xB2 -> READ_RECORD;
                case 0xDC -> UPDATE_RECORD;
                case 0x20 -> VERIFY;
                case 0x82 -> EXTERNAL_AUTHENTICATE;
                case 0x88 -> INTERNAL_AUTHENTICATE;
                case 0x46 -> GENERATE_ASYMMETRIC_KEYPAIR;
                case 0x84 -> GET_CHALLENGE;

                case 0x22 -> switch (p1) {
                    case 0x41 -> MSE_SET_AT;
                    case 0x01 -> MSE_SET_DST;
                    case 0xC1, 0xB6 -> MSE_SET_KAT;
                    default -> MANAGE_SECURITY_ENVIRONMENT;
                };

                case 0x2A -> switch (p1) {
                    case 0x9E -> switch (p2) {
                        case 0x9A -> PSO_COMPUTE_DIGITAL_SIGNATURE;
                        case 0x80 -> PSO_DECIPHER;
                        default -> PERFORM_SECURITY_OPERATION;
                    };
                    case 0x00 -> switch (p2) {
                        case 0xB8 -> PSO_VERIFY_CERTIFICATE;
                        default -> PERFORM_SECURITY_OPERATION;
                    };
                    default -> PERFORM_SECURITY_OPERATION;
                };

                case 0x86 -> GENERAL_AUTHENTICATE;
                case 0xDA -> PUT_DATA;
                case 0xCB -> PUT_DATA; // used in many proprietary PUT-DATA commands
                case 0xE4 -> DELETE_FILE;
                case 0x44 -> ACTIVATE_FILE;
                case 0x04 -> DEACTIVATE_FILE;
                case 0x24, 0x2C -> switch (p1) {
                    case 0x01 -> SET_PIN;
                    case 0x02 -> CHANGE_PIN;
                    case 0x03 -> UNBLOCK_PIN;
                    case 0x04 -> RESET_REFERENCE_DATA;
                    default -> CHANGE_REFERENCE_DATA;
                };
                default -> UNKNOWN;
            };
        }
    }
}

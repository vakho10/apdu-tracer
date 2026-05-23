// ISO 7816-4 APDU parsing and decoding, used by the detailed view.

export interface CommandApdu {
  kind: 'command'
  cla: number
  ins: number
  p1: number
  p2: number
  lc: number | null
  data: string // spaced uppercase hex, '' when absent
  le: number | null
  name: string
  note: string // '' when there is nothing extra to say
  apduCase: 1 | 2 | 3 | 4 // ISO 7816-4 command case
  secureMessaging: string // CLA-derived secure messaging note, '' when none
}

export interface ResponseApdu {
  kind: 'response'
  data: string // spaced uppercase hex, '' when absent
  sw1: number
  sw2: number
  status: StatusKind
  meaning: string
}

export interface MalformedApdu {
  kind: 'malformed'
  message: string
}

export type StatusKind = 'success' | 'warning' | 'error'
export type ParsedCommand = CommandApdu | MalformedApdu
export type ParsedResponse = ResponseApdu | MalformedApdu

export function byteHex(value: number): string {
  return value.toString(16).toUpperCase().padStart(2, '0')
}

export function toBytes(hex: string): number[] {
  const clean = hex.replace(/[^0-9a-fA-F]/g, '')
  const out: number[] = []
  for (let i = 0; i + 2 <= clean.length; i += 2) {
    out.push(parseInt(clean.slice(i, i + 2), 16))
  }
  return out
}

function spacedHex(bytes: number[]): string {
  return bytes.map(byteHex).join(' ')
}

// ISO 7816-4 (and common GlobalPlatform) instruction bytes.
const INSTRUCTIONS: Record<number, string> = {
  0x04: 'DEACTIVATE FILE',
  0x0c: 'ERASE RECORD',
  0x0e: 'ERASE BINARY',
  0x20: 'VERIFY',
  0x21: 'VERIFY',
  0x22: 'MANAGE SECURITY ENVIRONMENT',
  0x24: 'CHANGE REFERENCE DATA',
  0x26: 'DISABLE VERIFICATION REQUIREMENT',
  0x28: 'ENABLE VERIFICATION REQUIREMENT',
  0x2a: 'PERFORM SECURITY OPERATION',
  0x2c: 'RESET RETRY COUNTER',
  0x44: 'ACTIVATE FILE',
  0x46: 'GENERATE ASYMMETRIC KEY PAIR',
  0x47: 'GENERATE ASYMMETRIC KEY PAIR',
  0x70: 'MANAGE CHANNEL',
  0x82: 'EXTERNAL AUTHENTICATE',
  0x84: 'GET CHALLENGE',
  0x86: 'GENERAL AUTHENTICATE',
  0x87: 'GENERAL AUTHENTICATE',
  0x88: 'INTERNAL AUTHENTICATE',
  0xa2: 'SEARCH RECORD',
  0xa4: 'SELECT FILE',
  0xb0: 'READ BINARY',
  0xb1: 'READ BINARY',
  0xb2: 'READ RECORD',
  0xb3: 'READ RECORD',
  0xc0: 'GET RESPONSE',
  0xc2: 'ENVELOPE',
  0xc3: 'ENVELOPE',
  0xca: 'GET DATA',
  0xcb: 'GET DATA',
  0xd0: 'WRITE BINARY',
  0xd2: 'WRITE RECORD',
  0xd6: 'UPDATE BINARY',
  0xd7: 'UPDATE BINARY',
  0xda: 'PUT DATA',
  0xdb: 'PUT DATA',
  0xdc: 'UPDATE RECORD',
  0xdd: 'UPDATE RECORD',
  0xe0: 'CREATE FILE',
  0xe2: 'APPEND RECORD',
  0xe4: 'DELETE FILE',
  0xe6: 'TERMINATE DF',
  0xe8: 'TERMINATE EF',
  0xfe: 'TERMINATE CARD USAGE'
}

// SELECT FILE — meaning of the P1 selection method.
const SELECT_P1: Record<number, string> = {
  0x00: 'Select MF, DF or EF by file identifier',
  0x01: 'Select child DF',
  0x02: 'Select EF under the current DF',
  0x03: 'Select parent DF of the current DF',
  0x04: 'Select by DF name (AID)',
  0x08: 'Select by path from the MF',
  0x09: 'Select by path from the current DF'
}

// GlobalPlatform card-management instructions (used with a proprietary CLA).
const GP_INSTRUCTIONS: Record<number, string> = {
  0x50: 'INITIALIZE UPDATE',
  0x70: 'MANAGE CHANNEL',
  0x78: 'END R-MAC SESSION',
  0x7a: 'BEGIN R-MAC SESSION',
  0x82: 'EXTERNAL AUTHENTICATE',
  0x84: 'GET CHALLENGE',
  0x88: 'INTERNAL AUTHENTICATE',
  0xc0: 'GET RESPONSE',
  0xca: 'GET DATA',
  0xcb: 'GET DATA',
  0xd8: 'PUT KEY',
  0xe2: 'STORE DATA',
  0xe4: 'DELETE',
  0xe6: 'INSTALL',
  0xe8: 'LOAD',
  0xf0: 'SET STATUS',
  0xf2: 'GET STATUS'
}

// GET STATUS — the card subset reported, selected by P1.
const GP_GET_STATUS_SUBSET: Record<number, string> = {
  0x80: 'Issuer Security Domain',
  0x40: 'Applications and Supplementary Security Domains',
  0x20: 'Executable Load Files',
  0x10: 'Executable Load Files and Executable Modules'
}

// Exact ISO 7816-4 status words. Ranges (61xx, 6Cxx, 63Cx) are handled in code.
const STATUS_WORDS: Record<number, string> = {
  0x9000: 'Success',
  0x6200: 'Warning — no information given, non-volatile memory unchanged',
  0x6281: 'Warning — part of returned data may be corrupted',
  0x6282: 'Warning — end of file or record reached before reading Le bytes',
  0x6283: 'Warning — selected file deactivated',
  0x6284: 'Warning — file control information not formatted per ISO 7816-4',
  0x6285: 'Warning — selected file in termination state',
  0x6286: 'Warning — no input data available from a sensor on the card',
  0x6300: 'Warning — no information given, non-volatile memory changed',
  0x6381: 'Warning — file filled up by the last write',
  0x6400: 'Error — execution error, non-volatile memory unchanged',
  0x6401: 'Error — immediate response required by the card',
  0x6500: 'Error — execution error, non-volatile memory changed',
  0x6581: 'Error — memory failure',
  0x6700: 'Error — wrong length',
  0x6800: 'Error — function in CLA not supported',
  0x6881: 'Error — logical channel not supported',
  0x6882: 'Error — secure messaging not supported',
  0x6883: 'Error — last command of the chain expected',
  0x6884: 'Error — command chaining not supported',
  0x6900: 'Error — command not allowed',
  0x6981: 'Error — command incompatible with file structure',
  0x6982: 'Error — security status not satisfied',
  0x6983: 'Error — authentication method blocked',
  0x6984: 'Error — referenced data invalidated',
  0x6985: 'Error — conditions of use not satisfied',
  0x6986: 'Error — command not allowed, no current EF',
  0x6987: 'Error — expected secure messaging data objects missing',
  0x6988: 'Error — secure messaging data objects incorrect',
  0x6a00: 'Error — wrong parameters P1-P2',
  0x6a80: 'Error — incorrect parameters in the command data field',
  0x6a81: 'Error — function not supported',
  0x6a82: 'Error — file or application not found',
  0x6a83: 'Error — record not found',
  0x6a84: 'Error — not enough memory space in the file',
  0x6a85: 'Error — Lc inconsistent with TLV structure',
  0x6a86: 'Error — incorrect parameters P1-P2',
  0x6a87: 'Error — Lc inconsistent with P1-P2',
  0x6a88: 'Error — referenced data not found',
  0x6a89: 'Error — file already exists',
  0x6a8a: 'Error — DF name already exists',
  0x6b00: 'Error — wrong parameters P1-P2 (offset outside the EF)',
  0x6d00: 'Error — instruction code not supported or invalid',
  0x6e00: 'Error — class not supported',
  0x6f00: 'Error — no precise diagnosis'
}

/** GlobalPlatform-specific P1 note for INSTALL and GET STATUS. */
function describeGpNote(ins: number, p1: number): string {
  if (ins === 0xf2) return GP_GET_STATUS_SUBSET[p1] ?? ''
  if (ins === 0xe6) {
    const phases: string[] = []
    if (p1 & 0x04) phases.push('load')
    if (p1 & 0x08) phases.push('install')
    if (p1 & 0x10) phases.push('make selectable')
    if (p1 & 0x20) phases.push('extradition')
    if (p1 & 0x40) phases.push('registry update')
    if (p1 & 0x80) phases.push('personalization')
    return phases.length > 0 ? `For ${phases.join(', ')}` : ''
  }
  return ''
}

function describeCommand(
  cla: number,
  ins: number,
  p1: number,
  p2: number
): { name: string; note: string } {
  // GlobalPlatform card-management commands use a proprietary class (b8 = 1).
  if ((cla & 0x80) !== 0 && GP_INSTRUCTIONS[ins]) {
    return { name: GP_INSTRUCTIONS[ins], note: describeGpNote(ins, p1) }
  }

  const name = INSTRUCTIONS[ins] ?? `Unknown instruction (INS 0x${byteHex(ins)})`
  let note = ''

  switch (ins) {
    case 0xa4:
      note = SELECT_P1[p1] ?? `Selection method P1 = 0x${byteHex(p1)}`
      break
    case 0xb0:
    case 0xb1:
    case 0xd0:
    case 0xd6:
    case 0xd7:
      note = (p1 & 0x80) !== 0
        ? `Short EF id 0x${byteHex(p1 & 0x1f)}, offset ${p2}`
        : `Offset 0x${byteHex(p1)}${byteHex(p2)}`
      break
    case 0xb2:
    case 0xb3:
    case 0xdc:
    case 0xdd: {
      const sfi = p2 >> 3
      note = p1 === 0 ? 'Current record' : `Record ${p1}`
      if (sfi !== 0 && sfi !== 0x1f) note += `, short EF id 0x${byteHex(sfi)}`
      break
    }
    case 0x20:
    case 0x21:
    case 0x24:
      note = `Reference data 0x${byteHex(p2)}`
      break
    case 0xc0:
      note = 'Retrieve the response data left pending by a previous command'
      break
  }

  return { name, note }
}

function describeStatus(sw1: number, sw2: number): { status: StatusKind; meaning: string } {
  const sw = (sw1 << 8) | sw2

  if (sw === 0x9000) return { status: 'success', meaning: 'Success' }
  if (sw1 === 0x61) {
    return { status: 'success', meaning: `Success — ${sw2} more byte(s) available, use GET RESPONSE` }
  }
  if (sw1 === 0x6c) {
    return { status: 'warning', meaning: `Wrong Le — retry the command with Le = ${sw2}` }
  }
  if (sw1 === 0x63 && (sw2 & 0xf0) === 0xc0) {
    return { status: 'warning', meaning: `Verification failed — ${sw2 & 0x0f} attempt(s) remaining` }
  }

  const exact = STATUS_WORDS[sw]
  if (exact) {
    const status: StatusKind = exact.startsWith('Success')
      ? 'success'
      : exact.startsWith('Warning')
        ? 'warning'
        : 'error'
    return { status, meaning: exact }
  }

  if (sw1 === 0x90) return { status: 'success', meaning: 'Success' }
  if (sw1 === 0x62 || sw1 === 0x63) return { status: 'warning', meaning: 'Warning — no precise diagnosis' }
  return { status: 'error', meaning: `Unknown status word ${byteHex(sw1)}${byteHex(sw2)}` }
}

/** Reads the secure-messaging indication from the CLA byte (ISO 7816-4). */
function describeSecureMessaging(cla: number): string {
  // First interindustry class (CLA = 0000 xxxx): bits b4-b3 carry the SM mode.
  if ((cla & 0xf0) === 0x00) {
    switch (cla & 0x0c) {
      case 0x04:
        return 'proprietary'
      case 0x08:
        return 'header not authenticated'
      case 0x0c:
        return 'header authenticated'
      default:
        return ''
    }
  }
  // Further interindustry class (CLA = 010x xxxx): bit b5 flags secure messaging.
  if ((cla & 0xe0) === 0x40) {
    return (cla & 0x10) !== 0 ? 'enabled' : ''
  }
  // Proprietary class (b8 = 1): ISO leaves SM undefined; GlobalPlatform uses b3.
  if ((cla & 0x80) !== 0) {
    return (cla & 0x04) !== 0 ? 'proprietary class' : ''
  }
  return ''
}

/** Parses a command APDU: CLA INS P1 P2 [Lc Data] [Le], short and extended length. */
export function parseCommand(hex: string): ParsedCommand {
  const bytes = toBytes(hex)
  if (bytes.length < 4) {
    return { kind: 'malformed', message: `Command APDU too short — ${bytes.length} byte(s), need at least 4` }
  }

  const [cla, ins, p1, p2] = bytes
  const body = bytes.slice(4)
  let lc: number | null = null
  let data = ''
  let le: number | null = null

  if (body.length === 1) {
    // Case 2 (short): Le only.
    le = body[0] === 0 ? 256 : body[0]
  } else if (body.length === 3 && body[0] === 0x00) {
    // Case 2 (extended): Le only.
    const value = (body[1] << 8) | body[2]
    le = value === 0 ? 65536 : value
  } else if (body.length >= 2) {
    const extended = body[0] === 0x00 && body.length > 3
    const lcSize = extended ? 3 : 1
    const declared = extended ? (body[1] << 8) | body[2] : body[0]
    const after = body.slice(lcSize)
    const leSize = extended ? 2 : 1

    lc = declared
    if (after.length === declared + leSize) {
      data = spacedHex(after.slice(0, declared))
      const tail = after.slice(declared)
      const value = leSize === 2 ? (tail[0] << 8) | tail[1] : tail[0]
      le = value === 0 ? (leSize === 2 ? 65536 : 256) : value
    } else {
      // Case 3, or a length that does not add up — show the payload best-effort.
      data = spacedHex(after)
    }
  }

  // ISO 7816-4 case: 1 = no data/no Le, 2 = Le only, 3 = data only, 4 = data + Le.
  const apduCase: 1 | 2 | 3 | 4 = lc === null ? (le === null ? 1 : 2) : le === null ? 3 : 4

  const { name, note } = describeCommand(cla, ins, p1, p2)
  const secureMessaging = describeSecureMessaging(cla)
  return { kind: 'command', cla, ins, p1, p2, lc, data, le, name, note, apduCase, secureMessaging }
}

/** Parses a response APDU: [Data] SW1 SW2. */
export function parseResponse(hex: string): ParsedResponse {
  const bytes = toBytes(hex)
  if (bytes.length < 2) {
    return { kind: 'malformed', message: `Response APDU too short — ${bytes.length} byte(s), need at least 2 for SW` }
  }

  const sw1 = bytes[bytes.length - 2]
  const sw2 = bytes[bytes.length - 1]
  const data = spacedHex(bytes.slice(0, -2))
  const { status, meaning } = describeStatus(sw1, sw2)
  return { kind: 'response', data, sw1, sw2, status, meaning }
}

/**
 * Whether an instruction's response data may carry BER-TLV — SELECT (FCI),
 * GET DATA, READ RECORD, and GET RESPONSE. Used to avoid decoding raw data
 * (e.g. from READ BINARY) as TLV.
 */
export function commandReturnsTlv(ins: number): boolean {
  return ins === 0xa4 || ins === 0xca || ins === 0xcb || ins === 0xb2 || ins === 0xb3 || ins === 0xc0
}

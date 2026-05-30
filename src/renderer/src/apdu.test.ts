import { describe, it, expect } from 'vitest'
import { parseCommand, parseResponse, commandReturnsTlv, byteHex, toBytes } from './apdu'

describe('byteHex / toBytes', () => {
  it('formats a byte as two uppercase hex digits', () => {
    expect(byteHex(0)).toBe('00')
    expect(byteHex(0xa4)).toBe('A4')
    expect(byteHex(255)).toBe('FF')
  })

  it('parses a hex string into bytes, ignoring separators', () => {
    expect(toBytes('00 A4 04')).toEqual([0x00, 0xa4, 0x04])
    expect(toBytes('00a404')).toEqual([0x00, 0xa4, 0x04])
    // A trailing half-byte is dropped rather than mis-parsed.
    expect(toBytes('00A')).toEqual([0x00])
  })
})

describe('parseCommand', () => {
  it('decodes a SELECT by AID (case 3)', () => {
    const apdu = parseCommand('00 A4 04 00 07 A0 00 00 00 03 00 00')
    expect(apdu).toMatchObject({
      kind: 'command',
      cla: 0x00,
      ins: 0xa4,
      p1: 0x04,
      p2: 0x00,
      lc: 7,
      data: 'A0 00 00 00 03 00 00',
      le: null,
      apduCase: 3,
      name: 'SELECT FILE',
      note: 'Select by DF name (AID)',
      secureMessaging: ''
    })
  })

  it('decodes a case-4 command and maps Le = 0 to 256', () => {
    const apdu = parseCommand('00 A4 04 00 02 3F 00 00')
    expect(apdu).toMatchObject({
      kind: 'command',
      lc: 2,
      data: '3F 00',
      le: 256,
      apduCase: 4
    })
  })

  it('decodes a header-only command (case 1)', () => {
    const apdu = parseCommand('00 20 00 81')
    expect(apdu).toMatchObject({
      kind: 'command',
      lc: null,
      le: null,
      apduCase: 1,
      name: 'VERIFY',
      note: 'Reference data 0x81'
    })
  })

  it('decodes a short Le-only command (case 2)', () => {
    const apdu = parseCommand('00 B0 00 00 10')
    expect(apdu).toMatchObject({
      kind: 'command',
      lc: null,
      le: 16,
      apduCase: 2,
      name: 'READ BINARY',
      note: 'Offset 0x0000'
    })
  })

  it('names GlobalPlatform instructions under a proprietary class', () => {
    const apdu = parseCommand('80 F2 40 00 00')
    expect(apdu).toMatchObject({
      kind: 'command',
      name: 'GET STATUS',
      note: 'Applications and Supplementary Security Domains'
    })
  })

  it('flags a command shorter than four bytes as malformed', () => {
    expect(parseCommand('00 A4')).toMatchObject({ kind: 'malformed' })
  })
})

describe('parseResponse', () => {
  it('decodes 9000 as success', () => {
    expect(parseResponse('90 00')).toMatchObject({
      kind: 'response',
      data: '',
      sw1: 0x90,
      sw2: 0x00,
      status: 'success',
      meaning: 'Success'
    })
  })

  it('separates response data from the status word', () => {
    expect(parseResponse('01 02 90 00')).toMatchObject({
      data: '01 02',
      sw1: 0x90,
      sw2: 0x00,
      status: 'success'
    })
  })

  it('decodes a known error status word', () => {
    expect(parseResponse('6A 82')).toMatchObject({
      status: 'error',
      meaning: 'Error — file or application not found'
    })
  })

  it('decodes the 61xx "more data" range', () => {
    expect(parseResponse('61 0A')).toMatchObject({
      status: 'success',
      meaning: 'Success — 10 more byte(s) available, use GET RESPONSE'
    })
  })

  it('decodes the 6Cxx "wrong Le" range', () => {
    expect(parseResponse('6C 15')).toMatchObject({
      status: 'warning',
      meaning: 'Wrong Le — retry the command with Le = 21'
    })
  })

  it('decodes the 63Cx "tries remaining" range', () => {
    expect(parseResponse('63 C3')).toMatchObject({
      status: 'warning',
      meaning: 'Verification failed — 3 attempt(s) remaining'
    })
  })

  it('flags a response shorter than two bytes as malformed', () => {
    expect(parseResponse('00')).toMatchObject({ kind: 'malformed' })
  })
})

describe('commandReturnsTlv', () => {
  it('is true for SELECT, GET DATA, READ RECORD and GET RESPONSE', () => {
    expect(commandReturnsTlv(0xa4)).toBe(true)
    expect(commandReturnsTlv(0xca)).toBe(true)
    expect(commandReturnsTlv(0xb2)).toBe(true)
    expect(commandReturnsTlv(0xc0)).toBe(true)
  })

  it('is false for READ BINARY (raw data, not TLV)', () => {
    expect(commandReturnsTlv(0xb0)).toBe(false)
  })
})

import { describe, it, expect } from 'vitest'
import { parseTlv, detectProfile } from './tlv'

describe('parseTlv', () => {
  it('parses a nested FCI template with ISO tag names', () => {
    const result = parseTlv('6F 08 84 02 3F 00 A5 02 88 00')
    expect(result.ok).toBe(true)
    expect(result.nodes).toHaveLength(1)

    const fci = result.nodes[0]
    expect(fci).toMatchObject({
      tag: '6F',
      name: 'FCI template',
      constructed: true,
      length: 8
    })
    expect(fci.children).toHaveLength(2)
    expect(fci.children[0]).toMatchObject({
      tag: '84',
      name: 'DF name',
      constructed: false,
      value: '3F 00'
    })
    expect(fci.children[1]).toMatchObject({
      tag: 'A5',
      name: 'FCI proprietary template',
      constructed: true
    })
    expect(fci.children[1].children[0]).toMatchObject({ tag: '88', length: 0 })
  })

  it('uses EMV tag names under the EMV profile', () => {
    const result = parseTlv('82 02 19 80', 'emv')
    expect(result.ok).toBe(true)
    expect(result.nodes[0]).toMatchObject({
      tag: '82',
      name: 'Application Interchange Profile'
    })
  })

  it('decodes the EMV AIP bit field to its set-flag meanings', () => {
    const result = parseTlv('82 02 19 80', 'emv')
    expect(result.nodes[0].decoded).toEqual([
      'Cardholder verification supported',
      'Terminal risk management to be performed',
      'CDA supported'
    ])
  })

  it('decodes a known primitive value (PAN as digits)', () => {
    // 5A = Application PAN, packed BCD terminated by an F pad nibble.
    const result = parseTlv('5A 08 12 34 56 78 90 12 34 5F', 'emv')
    expect(result.nodes[0].decoded).toEqual(['123456789012345'])
  })

  it('rejects truncated TLV (length runs past the data)', () => {
    const result = parseTlv('6F 05 84 02 3F')
    expect(result.ok).toBe(false)
  })

  it('rejects an empty input', () => {
    expect(parseTlv('').ok).toBe(false)
  })
})

describe('detectProfile', () => {
  it('detects EMV from a payment AID prefix', () => {
    expect(detectProfile('A0000000031010')).toBe('emv')
  })

  it('detects PIV from its AID prefix', () => {
    expect(detectProfile('A000000308000010000100')).toBe('piv')
  })

  it('detects OpenPGP from its AID prefix', () => {
    expect(detectProfile('D2760001240102000000000000010000')).toBe('openpgp')
  })

  it('returns null for an unknown AID', () => {
    expect(detectProfile('11223344556677')).toBeNull()
  })
})

import { describe, it, expect } from 'vitest'
import { parseAtr } from './atr'

describe('parseAtr', () => {
  it('decodes the direct convention and historical bytes', () => {
    const atr = parseAtr('3B 03 12 34 56')
    expect(atr).toMatchObject({
      ok: true,
      convention: 'Direct',
      protocols: 'T=0',
      historicalCount: 3,
      historical: '12 34 56'
    })
  })

  it('decodes the inverse convention', () => {
    expect(parseAtr('3F 00')).toMatchObject({ ok: true, convention: 'Inverse' })
  })

  it('reads the offered protocol from a TD byte', () => {
    expect(parseAtr('3B 80 01')).toMatchObject({
      ok: true,
      convention: 'Direct',
      protocols: 'T=1',
      historicalCount: 0
    })
  })

  it('decodes COMPACT-TLV historical data objects', () => {
    const atr = parseAtr('3B 05 80 73 AA BB CC')
    expect(atr.ok).toBe(true)
    expect(atr.historical).toBe('80 73 AA BB CC')
    expect(atr.historicalNotes).toEqual([
      'Category: COMPACT-TLV data objects',
      'Card capabilities: AA BB CC'
    ])
  })

  it('rejects an invalid initial character (TS)', () => {
    const atr = parseAtr('3C 00')
    expect(atr.ok).toBe(false)
    expect(atr.message).toContain('TS')
  })

  it('rejects an ATR that is too short', () => {
    expect(parseAtr('3B').ok).toBe(false)
  })

  it('reports truncation within the historical bytes', () => {
    // T0 promises 5 historical bytes but only one follows.
    const atr = parseAtr('3B 05 12')
    expect(atr.ok).toBe(false)
    expect(atr.message).toContain('historical')
  })
})

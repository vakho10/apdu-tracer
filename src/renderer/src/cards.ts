import type { ApduRecord } from '../../preload'
import {
  byteHex,
  commandReturnsTlv,
  type CommandApdu,
  type MalformedApdu,
  type ResponseApdu
} from './apdu'
import { parseTlv, type TlvNode, type TlvProfile } from './tlv'
import type { AtrInfo } from './atr'
import { el, spaceHex } from './dom'

/** A measured command/response round-trip, attached to a response card. */
export interface ResponsePairing {
  latencyMs: number
  commandIns: number | null
}

function chip(label: string, value: string): HTMLElement {
  const node = el('span', 'chip')
  node.append(el('b', undefined, label), document.createTextNode(value))
  return node
}

function dataRow(label: string, hex: string): HTMLElement {
  const row = el('div', 'card-data')
  row.append(el('span', 'data-label', label), el('code', undefined, hex))
  return row
}

function cardHead(
  badge: string,
  badgeClass: string,
  title: string,
  titleClass: string,
  time: string
): HTMLElement {
  const head = el('div', 'card-head')
  head.append(
    el('span', `badge ${badgeClass}`, badge),
    el('span', `card-title ${titleClass}`.trim(), title),
    el('time', undefined, time)
  )
  return head
}

export function buildCommandCard(apdu: CommandApdu, time: string): HTMLElement {
  const card = el('article', 'card card--command')
  card.append(cardHead('Command', 'badge--command', apdu.name, '', time))

  const chips = el('div', 'chips')
  chips.append(
    chip('CLA', byteHex(apdu.cla)),
    chip('INS', byteHex(apdu.ins)),
    chip('P1', byteHex(apdu.p1)),
    chip('P2', byteHex(apdu.p2)),
    chip('Lc', apdu.lc === null ? '—' : String(apdu.lc)),
    chip('Le', apdu.le === null ? '—' : String(apdu.le)),
    chip('Case', String(apdu.apduCase))
  )
  if (apdu.secureMessaging) chips.append(chip('SM', apdu.secureMessaging))
  card.append(chips)

  if (apdu.data) card.append(dataRow('Data', apdu.data))
  if (apdu.note) card.append(el('p', 'card-note', apdu.note))
  return card
}

function renderTlvNode(node: TlvNode): HTMLElement {
  const header = el('div', 'tlv-header')
  header.append(el('span', 'tlv-tag', node.tag))
  if (node.name) header.append(el('span', 'tlv-name', node.name))
  header.append(el('span', 'tlv-len', `${node.length} B`))

  if (node.constructed) {
    const details = el('details', 'tlv-node tlv-constructed')
    details.open = true
    const summary = el('summary', 'tlv-summary')
    summary.append(header)
    details.append(summary)

    const childWrap = el('div', 'tlv-children')
    for (const child of node.children) childWrap.append(renderTlvNode(child))
    details.append(childWrap)
    return details
  }

  const row = el('div', 'tlv-node tlv-primitive')
  row.append(header)
  if (node.value) row.append(el('code', 'tlv-value', node.value))
  for (const line of node.decoded) row.append(el('span', 'tlv-decoded', `→ ${line}`))
  return row
}

export function buildResponseCard(
  apdu: ResponseApdu,
  time: string,
  pairing: ResponsePairing | null,
  profile: TlvProfile
): HTMLElement {
  const card = el('article', 'card card--response')
  card.append(cardHead('Response', 'badge--response', apdu.meaning, `status-${apdu.status}`, time))

  const chips = el('div', 'chips')
  chips.append(chip('SW', `${byteHex(apdu.sw1)} ${byteHex(apdu.sw2)}`))
  if (pairing) chips.append(chip('Latency', `${pairing.latencyMs} ms`))
  card.append(chips)

  if (apdu.data) {
    card.append(dataRow('Data', apdu.data))
    // Decode as TLV only when the command is unknown or one that returns TLV.
    const tlvExpected =
      !pairing || pairing.commandIns === null || commandReturnsTlv(pairing.commandIns)
    if (tlvExpected) {
      const tlv = parseTlv(apdu.data, profile)
      if (tlv.ok) {
        const tree = el('div', 'tlv-tree')
        for (const node of tlv.nodes) tree.append(renderTlvNode(node))
        card.append(tree)
      }
    }
  }
  return card
}

export function buildMalformedCard(
  record: ApduRecord,
  apdu: MalformedApdu,
  time: string
): HTMLElement {
  const card = el('article', 'card card--malformed')
  const label = record.direction === 'command' ? 'Command' : 'Response'
  card.append(cardHead(label, 'badge--malformed', 'Malformed APDU', 'status-error', time))
  card.append(el('p', 'card-note', apdu.message))
  card.append(dataRow('Raw', spaceHex(record.hex)))
  return card
}

/** Builds a card for a decoded Answer-To-Reset. */
export function buildAtrCard(atr: AtrInfo, rawHex: string, time: string): HTMLElement {
  const card = el('article', 'card card--atr')
  card.append(cardHead('ATR', 'badge--atr', 'Answer To Reset', '', time))

  if (atr.ok) {
    const chips = el('div', 'chips')
    chips.append(
      chip('Convention', atr.convention),
      chip('Protocols', atr.protocols),
      chip('Historical', String(atr.historicalCount))
    )
    card.append(chips)
    if (atr.historical) card.append(dataRow('Historical', atr.historical))
    for (const note of atr.historicalNotes) card.append(el('p', 'card-note', note))
  } else {
    card.append(el('p', 'card-note', atr.message))
  }

  card.append(dataRow('ATR', rawHex))
  return card
}

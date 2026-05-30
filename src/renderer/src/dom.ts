// Small DOM and formatting helpers shared by the renderer modules.

/** Creates an element with an optional class name and text content. */
export function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  className?: string,
  text?: string
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag)
  if (className) node.className = className
  if (text !== undefined) node.textContent = text
  return node
}

/** "00A40400" -> "00 A4 04 00" */
export function spaceHex(hex: string): string {
  return hex.replace(/(.{2})(?=.)/g, '$1 ')
}

/** Formats the millisecond gap since the previous APDU. */
export function formatDelta(ms: number): string {
  return ms < 1000 ? `+${ms}ms` : `+${(ms / 1000).toFixed(1)}s`
}

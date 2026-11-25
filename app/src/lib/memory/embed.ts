/**
 * 嵌入用文本的构造
 *
 * 正文保留原文供展示与全文检索；嵌入时在前面补一段人类可读的时间参考，
 * 让向量带上时间感知。
 */

const WEEKDAYS = ['星期日', '星期一', '星期二', '星期三', '星期四', '星期五', '星期六']

/**
 * 构造嵌入用文本
 *
 * @param text       正文
 * @param occurredAt 事实发生时间（毫秒时间戳）；为空则不加时间前缀
 */
export function buildEmbedText(text: string, occurredAt: number | null | undefined): string {
  if (occurredAt == null || !Number.isFinite(occurredAt)) return text
  const d = new Date(occurredAt)
  const y = d.getFullYear()
  const m = String(d.getMonth() + 1).padStart(2, '0')
  const day = String(d.getDate()).padStart(2, '0')
  const wd = WEEKDAYS[d.getDay()]
  return `${text}（${y}年${m}月${day}日 ${wd}）`
}

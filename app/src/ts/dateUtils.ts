/**
 * 本地时间格式化工具（避免 toISOString() 的 UTC 偏移导致日期回退一天）
 *
 * 后端 FromStr() 使用 std::mktime() 将 naive datetime 按本地时区转为 Unix 时间戳，
 * 前端必须用本地时间方法（getFullYear/getMonth 等）还原，禁止 toISOString()（UTC）。
 */

const pad = (n: number) => String(n).padStart(2, '0')

/** Unix 时间戳 / Date → "YYYY-MM-DD"（本地日期） */
export function formatLocalDate(d: Date): string {
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`
}

/** Unix 时间戳 / Date → "YYYY-MM-DD HH:mm"（本地日期时间） */
export function formatLocalDatetime(d: Date): string {
  return `${formatLocalDate(d)} ${pad(d.getHours())}:${pad(d.getMinutes())}`
}

/** 后端返回的 datetime（number | string）→ 本地日期字符串 "YYYY-MM-DD" */
export function parseBackendDate(v: unknown): string {
  if (typeof v === 'number') return formatLocalDate(new Date(v * 1000))
  return String(v).slice(0, 10)
}

/** 后端返回的 datetime（number | string）→ 本地日期时间字符串 "YYYY-MM-DD HH:mm" */
export function parseBackendDatetime(v: unknown): string {
  if (typeof v === 'number') return formatLocalDatetime(new Date(v * 1000))
  return String(v)
}

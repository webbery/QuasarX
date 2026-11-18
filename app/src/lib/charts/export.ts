/**
 * UI 产物导出（PNG / CSV）
 *
 * 导出的是**当前参数状态**下的内容 —— 用户调过滑块或换了基准，导出的就是调完的。
 */

import * as echarts from 'echarts'
import type { UiSpec } from './types'

/* ============================================================================
 * CSV
 * ========================================================================== */

/** CSV 字段转义（逗号、引号、换行） */
function csvCell(value: unknown): string {
  if (value === null || value === undefined) return ''
  const s = String(value)
  if (/[",\n\r]/.test(s)) {
    return `"${s.replace(/"/g, '""')}"`
  }
  return s
}

/** 一行转 CSV 文本 */
function csvRow(cells: unknown[]): string {
  return cells.map(csvCell).join(',')
}

/**
 * 把 UI 数据拍平成 CSV 文本。
 * 按类型分别处理：时序图每列一列，表格按列定义，其余按通用键值。
 */
export function toCsv(spec: UiSpec, data: Record<string, unknown>): string {
  const lines: string[] = []

  switch (spec.type) {
    case 'timeseries': {
      const x = (data.x as number[]) ?? []
      const cols = (data.cols as Array<{ n: string; v: unknown[] }>) ?? []
      lines.push(csvRow([(data.x_label as string) ?? 'x', ...cols.map((c) => c.n)]))
      for (let i = 0; i < x.length; i++) {
        lines.push(csvRow([x[i], ...cols.map((c) => c.v[i])]))
      }
      break
    }

    case 'histogram': {
      const bins = (data.bins as number[]) ?? []
      const counts = (data.counts as number[]) ?? []
      lines.push(csvRow(['bin_start', 'bin_end', 'count']))
      for (let i = 0; i < counts.length; i++) {
        lines.push(csvRow([bins[i], bins[i + 1] ?? bins[i], counts[i]]))
      }
      break
    }

    case 'matrix': {
      const rows = (data.rows as string[]) ?? []
      const cols = (data.cols as string[]) ?? []
      const values = (data.values as number[][]) ?? []
      lines.push(csvRow(['', ...cols]))
      rows.forEach((rowLabel, r) => {
        lines.push(csvRow([rowLabel, ...(values[r] ?? [])]))
      })
      break
    }

    case 'table': {
      const columns = (data.columns as Array<{ k: string; label: string }>) ?? []
      const rows = (data.rows as Array<Record<string, unknown>>) ?? []
      lines.push(csvRow(columns.map((c) => c.label)))
      for (const row of rows) {
        lines.push(csvRow(columns.map((c) => row[c.k])))
      }
      break
    }

    case 'metric': {
      const items = (data.items as Array<Record<string, unknown>>) ?? []
      lines.push(csvRow(['label', 'value', 'delta', 'delta_label']))
      for (const item of items) {
        lines.push(csvRow([item.label, item.value, item.delta, item.deltaLabel]))
      }
      break
    }

    default: {
      // 未知类型：把 data 整体拍平成键值对
      lines.push(csvRow(['key', 'value']))
      for (const [k, v] of Object.entries(data)) {
        lines.push(csvRow([k, typeof v === 'object' ? JSON.stringify(v) : v]))
      }
    }
  }

  return lines.join('\n')
}

/* ============================================================================
 * 下载
 * ========================================================================== */

/** 触发浏览器下载 */
export function downloadBlob(filename: string, blob: Blob): void {
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = filename
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
  // 立刻 revoke 会让部分浏览器下载失败，延迟一拍
  setTimeout(() => URL.revokeObjectURL(url), 1000)
}

/** 文件名净化（去掉路径分隔符与非法字符） */
export function safeFilename(title: string, ext: string): string {
  const base = title.replace(/[\\/:*?"<>|]/g, '_').trim() || 'chart'
  return `${base}.${ext}`
}

/** 导出 CSV 文件 */
export function exportCsv(spec: UiSpec, data: Record<string, unknown>): void {
  const csv = toCsv(spec, data)
  // BOM：让 Excel 正确识别 UTF-8
  const blob = new Blob(['﻿' + csv], { type: 'text/csv;charset=utf-8' })
  downloadBlob(safeFilename(spec.title, 'csv'), blob)
}

/**
 * 导出 PNG。
 * @param container 图区容器（里面要有 ECharts 实例）
 */
export function exportPng(container: HTMLElement | null, spec: UiSpec): boolean {
  if (!container) return false
  const chart = echarts.getInstanceByDom(container)
  if (!chart) return false

  const url = chart.getDataURL({
    type: 'png',
    pixelRatio: 2,
    backgroundColor: '#1a2236',
  })

  const a = document.createElement('a')
  a.href = url
  a.download = safeFilename(spec.title, 'png')
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
  return true
}

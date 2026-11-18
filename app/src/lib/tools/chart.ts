/**
 * 图表 Tool
 *
 * 让 Agent 能把「数据」变成「图」：
 * - 工具层负责取数与组装 payload（LLM 不碰数值数组）
 * - 产物走事件通道进 UI，LLM 只拿到一段文字摘要
 *
 * 数据集全部来自最近一次回测结果（historyStore），不额外打后端。
 * 依赖方向：chart → backtest（复用 buildChartFromResult），单向无环。
 *
 * 产物是 `ui` 类型：payload 是一份平铺的 UiSpec，
 * 数据用列式结构（每列 { n: 名称, v: 值数组 }），由 UI 注册表分发渲染。
 */

import { tool } from '@langchain/core/tools'
import { z } from 'zod'
import { useHistoryStore } from '@/stores/history'
import {
  makeArtifact,
  type ToolArtifactResult,
} from '@/lib/agent/artifacts'
import { buildChartFromResult } from './backtest'

/* ============================================================================
 * 数据集定义
 * ========================================================================== */

export const CHART_DATASETS = [
  'performance',  // 累计收益（策略 vs 基准）
  'price',        // 价格趋势（含买卖信号）
  'drawdown',     // 回撤曲线
  'distribution', // 收益率分布
  'daily_returns',// 日收益率序列
] as const

export type ChartDataset = (typeof CHART_DATASETS)[number]

const DATASET_LABEL: Record<ChartDataset, string> = {
  performance: '累计收益',
  price: '价格趋势',
  drawdown: '回撤曲线',
  distribution: '收益率分布',
  daily_returns: '日收益率',
}

/** 每个数据集对应的 UI 类型 */
const DATASET_UI_TYPE: Record<ChartDataset, 'timeseries' | 'histogram'> = {
  performance: 'timeseries',
  price: 'timeseries',
  drawdown: 'timeseries',
  distribution: 'histogram',
  daily_returns: 'timeseries',
}

/** 每个数据集默认的图形（呈现层参数） */
const DATASET_CHART_TYPE: Record<ChartDataset, string> = {
  performance: 'line',
  price: 'line',
  drawdown: 'area',
  distribution: 'bar',
  daily_returns: 'bar',
}

/** 时刻标注输入 */
interface MomentInput {
  date: string
  title: string
  detail?: string
  severity?: 'info' | 'positive' | 'warning' | 'critical'
}

/* ============================================================================
 * 工具
 * ========================================================================== */

export const chartTool = tool(
  async (params): Promise<ToolArtifactResult> => {
    const {
      dataset,
      title,
      subtitle,
      chartType,
      yFormat,
      secondarySeries,
      moments: momentInputs,
    } = params

    const historyStore = useHistoryStore()
    const result = await historyStore.getLatestBacktestResult()

    if (!result) {
      return {
        text:
          '当前没有回测结果，无法生成图表。请先在主界面手动触发回测（流程图编辑器 → 回测按钮），' +
          '完成后 Agent 才能读取结果绘制图表。',
      }
    }

    // 优先用回测结果里已算好的 chartData，否则现场构建
    const chartData = result.chartData ?? buildChartFromResult(result)
    const ds = dataset as ChartDataset

    const built = buildUiPayload(ds, chartData, momentInputs)
    if (!built) {
      return {
        text: `回测结果中没有「${DATASET_LABEL[ds] ?? dataset}」数据集，无法生成图表。`,
      }
    }

    // 呈现层参数
    const uiParams: Record<string, unknown> = {
      chart_type: chartType ?? DATASET_CHART_TYPE[ds],
      legend: true,
    }
    if (secondarySeries && built.colLabels) {
      const idx = built.colLabels.indexOf(secondarySeries)
      if (idx >= 0) {
        uiParams.secondary_axis = idx
        uiParams.secondary_axis_label = secondarySeries
      }
    }

    const spec = {
      type: DATASET_UI_TYPE[ds],
      title: title || `${DATASET_LABEL[ds] ?? dataset}（最近一次回测）`,
      subtitle: subtitle ?? '数据源：最近一次回测结果',
      source: `chart · dataset=${dataset}`,
      params: uiParams,
      data: built.data,
    }

    const artifact = makeArtifact('ui', spec.title, spec, {
      subtitle: spec.subtitle,
      source: spec.source,
      ui: { height: 300, collapsible: true },
    })

    return {
      text: describeChart(ds, built),
      artifact,
    }
  },
  {
    name: 'chart',
    description:
      '图表生成工具。把最近一次回测结果中的数据渲染成图表产物（对话流中直接出图）。' +
      `dataset 可选：${CHART_DATASETS.join(' / ')}。` +
      '可指定 moments 标出关键时刻（如回撤极值、变点、交易事件），会在图上画竖线并在图下生成时刻列表。' +
      '本工具只读取已有回测结果，不会触发回测。',
    schema: z.object({
      dataset: z.enum(CHART_DATASETS).describe('数据集：performance 累计收益 / price 价格趋势 / drawdown 回撤 / distribution 收益分布 / daily_returns 日收益率'),
      title: z.string().optional().describe('图表标题'),
      subtitle: z.string().optional().describe('图表副标题'),
      chartType: z.enum(['line', 'area', 'bar', 'scatter']).optional().describe('图表类型，缺省按数据集推断'),
      yFormat: z.enum(['number', 'percent', 'currency']).optional().describe('Y 轴格式'),
      secondarySeries: z.string().optional().describe('挂到右侧独立 Y 轴的序列名'),
      moments: z
        .array(
          z.object({
            date: z.string().describe('时刻，YYYY-MM-DD'),
            title: z.string().describe('该时刻的标题，如「变点检测触发」'),
            detail: z.string().optional().describe('详细说明'),
            severity: z.enum(['info', 'positive', 'warning', 'critical']).optional(),
          }),
        )
        .optional()
        .describe('需要标注的关键时刻列表'),
    }),
  },
)

/* ============================================================================
 * 组装
 * ========================================================================== */

/** 把 YYYY-MM-DD（或 YYYY-MM-DD HH:mm）解析成当日 15:00 的 Unix 秒 */
function parseMomentTimestamp(date: string): number | null {
  const m = date.match(/^(\d{4})-(\d{2})-(\d{2})/)
  if (!m) return null
  const ts = new Date(Number(m[1]), Number(m[2]) - 1, Number(m[3]), 15, 0, 0).getTime()
  return Number.isNaN(ts) ? null : Math.floor(ts / 1000)
}

/** 在时间序列中就近匹配某个时刻的下标 */
function nearestIndex(timestamps: number[], ts: number): number {
  if (!timestamps.length) return -1
  let best = 0
  let bestDiff = Infinity
  for (let i = 0; i < timestamps.length; i++) {
    const diff = Math.abs(timestamps[i] - ts)
    if (diff < bestDiff) {
      bestDiff = diff
      best = i
    }
  }
  return best
}

interface BuiltPayload {
  /** 列式数据 */
  data: Record<string, unknown>
  /** 列名（用于双轴定位） */
  colLabels?: string[]
}

/**
 * 从 chartData 组装 UI 规格的 data 部分（列式）。
 * 返回 null 表示该数据集不存在。
 */
function buildUiPayload(
  dataset: ChartDataset,
  chartData: any,
  momentInputs?: MomentInput[],
): BuiltPayload | null {
  /* ---------- 分布直方图 ---------- */
  if (dataset === 'distribution') {
    if (!chartData?.distribution) return null
    return {
      data: {
        bins: chartData.distribution.bins,
        counts: chartData.distribution.counts,
        x_label: '收益率区间',
        y_label: '频次',
      },
    }
  }

  /* ---------- 时序类 ---------- */
  let x: number[] = []
  let cols: Array<{ n: string; v: number[] }> = []
  let yLabel = ''
  let yFormat: 'number' | 'percent' | 'currency' = 'number'

  switch (dataset) {
    case 'performance': {
      const ts = chartData?.performance
      if (!ts) return null
      x = ts.timestamps
      cols = ts.labels.map((n: string, i: number) => ({ n, v: ts.values[i] ?? [] }))
      yLabel = '累计收益 (%)'
      yFormat = 'percent'
      break
    }
    case 'price': {
      const ts = chartData?.price
      if (!ts) return null
      x = ts.timestamps
      cols = ts.labels.map((n: string, i: number) => ({ n, v: ts.values[i] ?? [] }))
      yLabel = '价格'
      yFormat = 'number'
      break
    }
    case 'drawdown': {
      const ts = chartData?.drawdown
      if (!ts) return null
      x = ts.timestamps
      cols = ts.labels.map((n: string, i: number) => ({ n, v: ts.values[i] ?? [] }))
      yLabel = '回撤 (%)'
      yFormat = 'percent'
      break
    }
    case 'daily_returns': {
      const rets: number[] = chartData?.dailyReturns
      if (!rets) return null
      const dates: number[] | undefined = chartData?.performance?.timestamps
      x = dates && dates.length === rets.length ? dates : rets.map((_, i) => i)
      cols = [{ n: '日收益率', v: rets }]
      yLabel = '日收益率 (%)'
      yFormat = 'percent'
      break
    }
  }

  if (!x.length) return null

  const data: Record<string, unknown> = {
    x,
    cols,
    x_label: '日期',
    y_label: yLabel,
    y_format: yFormat,
  }

  // 时刻标注：转成 marker + moment 双向关联
  if (momentInputs?.length) {
    const markers: Array<{ x: number; kind: string; label: string }> = []
    const moments: Array<Record<string, unknown>> = []

    momentInputs.forEach((input, i) => {
      const ts = parseMomentTimestamp(input.date)
      if (ts === null) return
      const idx = nearestIndex(x, ts)
      if (idx < 0) return
      markers.push({ x: idx, kind: 'point', label: input.title })
      moments.push({
        id: `moment-${i}`,
        timestamp: x[idx],
        title: input.title,
        detail: input.detail,
        severity: input.severity,
      })
    })

    if (moments.length) {
      data.markers = markers
      data.moments = moments
    }
  }

  return { data, colLabels: cols.map((c) => c.n) }
}

/** 生成给 LLM 的文字摘要 */
function describeChart(dataset: ChartDataset, built: BuiltPayload): string {
  const lines: string[] = []
  const data = built.data

  const cols = data.cols as Array<{ n: string; v: number[] }> | undefined
  const x = (data.x as number[]) ?? []

  if (cols) {
    lines.push(
      `已生成「${DATASET_LABEL[dataset]}」图表：${x.length} 个数据点，${cols.length} 条序列（${cols.map((c) => c.n).join(' / ')}）。`,
    )
    const isPct = data.y_format === 'percent'
    cols.forEach((col) => {
      if (!col.v.length) return
      const fmt = (v: number) => (isPct ? `${(v * 100).toFixed(2)}%` : v.toFixed(4))
      lines.push(`  ${col.n}: ${fmt(col.v[0])} → ${fmt(col.v[col.v.length - 1])}`)
    })
  } else {
    const counts = (data.counts as number[]) ?? []
    lines.push(`已生成「${DATASET_LABEL[dataset]}」直方图：${counts.length} 个区间。`)
  }

  const moments = (data.moments as Array<{ title: string }>) ?? []
  if (moments.length) {
    lines.push(`已标注 ${moments.length} 个关键时刻：${moments.map((m) => m.title).join('、')}。`)
  }

  lines.push('图表已在对话流中渲染，可直接查看。')
  return lines.join('\n')
}

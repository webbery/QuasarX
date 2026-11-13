/**
 * 图表 Tool
 *
 * 让 Agent 能把「数据」变成「图」：
 * - 工具层负责取数与组装 payload（LLM 不碰数值数组）
 * - 产物走事件通道进 UI，LLM 只拿到一段文字摘要
 *
 * 数据集全部来自最近一次回测结果（historyStore），不额外打后端。
 * 依赖方向：chart → backtest（复用 buildChartFromResult），单向无环。
 */

import { tool } from '@langchain/core/tools'
import { z } from 'zod'
import { useHistoryStore } from '@/stores/history'
import {
  makeArtifact,
  type ChartMoment,
  type ChartPayload,
  type ChartType,
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

const DATASET_CHART_TYPE: Record<ChartDataset, ChartType> = {
  performance: 'line',
  price: 'line',
  drawdown: 'area',
  distribution: 'histogram',
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

    const payload = buildChartPayload(dataset as ChartDataset, chartData, momentInputs)

    if (!payload) {
      return {
        text: `回测结果中没有「${DATASET_LABEL[dataset as ChartDataset] ?? dataset}」数据集，无法生成图表。`,
      }
    }

    payload.chartType = chartType ?? DATASET_CHART_TYPE[dataset as ChartDataset] ?? 'line'

    if (!payload.yFormat) {
      payload.yFormat = inferYFormat(dataset as ChartDataset, payload)
    }

    // 双轴：把指定序列挂到独立 Y 轴
    if (secondarySeries) {
      const idx = payload.timeSeries?.labels.indexOf(secondarySeries) ?? -1
      if (idx >= 0) {
        payload.secondaryAxis = { seriesIndex: idx, yAxisLabel: secondarySeries }
      }
    }

    const artifact = makeArtifact(
      'chart',
      title || `${DATASET_LABEL[dataset as ChartDataset] ?? dataset}（最近一次回测）`,
      payload,
      {
        subtitle: subtitle ?? '数据源：最近一次回测结果',
        source: `chart · dataset=${dataset}`,
        ui: { height: 300, collapsible: true },
      },
    )

    return {
      text: describeChart(dataset as ChartDataset, payload),
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
      chartType: z.enum(['line', 'area', 'bar', 'scatter', 'candlestick', 'histogram', 'heatmap']).optional().describe('图表类型，缺省按数据集推断'),
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

/**
 * 从 chartData 组装图表 payload。
 * 返回 null 表示该数据集不存在。
 */
function buildChartPayload(
  dataset: ChartDataset,
  chartData: any,
  momentInputs?: MomentInput[],
): ChartPayload | null {
  const payload: ChartPayload = { chartType: 'line' }

  switch (dataset) {
    case 'performance': {
      if (!chartData?.performance) return null
      payload.timeSeries = chartData.performance
      payload.xAxisLabel = '日期'
      payload.yAxisLabel = '累计收益 (%)'
      break
    }
    case 'price': {
      if (!chartData?.price) return null
      payload.timeSeries = chartData.price
      payload.xAxisLabel = '日期'
      payload.yAxisLabel = '价格'
      break
    }
    case 'drawdown': {
      if (!chartData?.drawdown) return null
      payload.timeSeries = chartData.drawdown
      payload.xAxisLabel = '日期'
      payload.yAxisLabel = '回撤 (%)'
      payload.chartType = 'area'
      break
    }
    case 'distribution': {
      if (!chartData?.distribution) return null
      payload.histogram = chartData.distribution
      payload.chartType = 'histogram'
      payload.xAxisLabel = '收益率区间'
      payload.yAxisLabel = '频次'
      break
    }
    case 'daily_returns': {
      if (!chartData?.dailyReturns) return null
      const rets: number[] = chartData.dailyReturns
      const dates: number[] | undefined = chartData.performance?.timestamps
      payload.timeSeries = {
        timestamps: dates && dates.length === rets.length ? dates : rets.map((_, i) => i),
        values: [rets],
        labels: ['日收益率'],
      }
      payload.xAxisLabel = '日期'
      payload.yAxisLabel = '日收益率 (%)'
      payload.chartType = 'bar'
      break
    }
  }

  // 时刻标注：转成 marker + moment 双向关联
  if (momentInputs?.length && payload.timeSeries?.timestamps.length) {
    const timestamps = payload.timeSeries.timestamps
    const markers: NonNullable<ChartPayload['markers']> = []
    const moments: ChartMoment[] = []

    momentInputs.forEach((input, i) => {
      const ts = parseMomentTimestamp(input.date)
      if (ts === null) return
      const idx = nearestIndex(timestamps, ts)
      if (idx < 0) return
      markers.push({
        type: 'vline',
        timestamp: timestamps[idx],
        label: input.title,
        style: 'dashed',
      })
      moments.push({
        id: `moment-${i}`,
        timestamp: timestamps[idx],
        title: input.title,
        detail: input.detail,
        severity: input.severity,
        markerIndex: markers.length - 1,
      })
    })

    if (moments.length) {
      payload.markers = markers
      payload.moments = moments
    }
  }

  return payload
}

/** Y 轴格式推断：回撤与收益按百分比，其余保留显式指定或按数值 */
function inferYFormat(
  dataset: ChartDataset,
  payload: ChartPayload,
): 'number' | 'percent' | 'currency' {
  if (dataset === 'performance' || dataset === 'drawdown' || dataset === 'daily_returns') {
    return 'percent'
  }
  return payload.yFormat ?? 'number'
}

/** 生成给 LLM 的文字摘要 */
function describeChart(dataset: ChartDataset, payload: ChartPayload): string {
  const lines: string[] = []

  if (payload.timeSeries) {
    const ts = payload.timeSeries
    lines.push(
      `已生成「${DATASET_LABEL[dataset]}」图表：${ts.timestamps.length} 个数据点，${ts.labels.length} 条序列（${ts.labels.join(' / ')}）。`,
    )
    ts.labels.forEach((label, i) => {
      const values = ts.values[i] || []
      if (!values.length) return
      const first = values[0]
      const last = values[values.length - 1]
      const isPct = payload.yFormat === 'percent'
      const fmt = (v: number) => (isPct ? `${(v * 100).toFixed(2)}%` : v.toFixed(4))
      lines.push(`  ${label}: ${fmt(first)} → ${fmt(last)}`)
    })
  } else if (payload.histogram) {
    lines.push(`已生成「${DATASET_LABEL[dataset]}」直方图：${payload.histogram.bins.length} 个区间。`)
  } else if (payload.matrix) {
    lines.push(`已生成热力图：${payload.matrix.rows.length} × ${payload.matrix.cols.length}。`)
  }

  if (payload.moments?.length) {
    lines.push(`已标注 ${payload.moments.length} 个关键时刻：${payload.moments.map((m) => m.title).join('、')}。`)
  }

  lines.push('图表已在对话流中渲染，可直接查看。')
  return lines.join('\n')
}

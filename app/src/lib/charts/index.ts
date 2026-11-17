/**
 * UI 规格系统统一导出
 *
 * 分层：
 *   types.ts      契约（UiSpec / UiControlDef / UiTypeDefinition / UiResolver）
 *   schemas.ts    各类型的输入约束（= 工具约束）
 *   registry.ts   类型注册表
 *   resolvers.ts  数据层回源
 *   options/      纯 ECharts option builder
 */

export * from './types'
export * from './registry'
export * from './schemas'
export { getResolver, listResolverIds, BENCHMARK_OPTIONS, DEFAULT_BENCHMARK } from './resolvers'
export { buildTimeseriesOption } from './options/timeseries'
export type { TimeSeriesData, TimeSeriesColumn, TimeSeriesMarker, TimeSeriesOptions } from './options/timeseries'
export { buildHistogramOption, buildMatrixOption } from './options/histogram'
export type { HistogramData, MatrixData } from './options/histogram'

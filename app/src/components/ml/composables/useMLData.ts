// app/src/components/ml/composables/useMLData.ts
// ML 训练分析 API 调用

import axios from 'axios'
import { ElMessage } from 'element-plus'
import { fetchEventSource } from '@microsoft/fetch-event-source'
import type { ShapResult, TrainResult, FeatureReport, LabelAnalysisResult, BatchLabelStat, TrainStep, TrainLog } from './useMLState'
import { convertLabelsToKeys, normalizeCodeParams } from '@/lib/nodes'
import { parseBackendDate } from '@/ts/dateUtils'

export function useMLData() {
  /**
   * 触发训练
   * @param script 策略图 JSON（字符串形式）
   * @param config 训练配置
   * @param csvPath 可选：预收集的特征 CSV 路径（跳过数据收集阶段）
   */
  async function train(
    script: string,
    config: {
      labelSource: string
      labelPeriod: number
      labelType: string
      labelShape: string
      volK: number
      objective: string
      numClass: number
      testRatio: number
      valRatio: number
      learningRate: number
      maxDepth: number
      nEstimators: number
      earlyStoppingRounds: number
      subsample: number
      colsampleBytree: number
      gamma: number
      minChildWeight: number
      scalePosWeight: number
      regAlpha: number
      regLambda: number
      startDate: string
      endDate: string
      frequency: string
    },
    onEvent?: (type: string, data: any) => void,
    csvPath?: string,
    nodeLabel?: string,
  ): Promise<TrainResult | null> {
    try {
      const body: Record<string, any> = {
        action: 'train',
        script: normalizeCodeParams(convertLabelsToKeys(script)),
        label: {
          source: config.labelSource,
          period: config.labelPeriod,
          type: config.labelType,
          shape: config.labelShape,
          vol_k: config.volK,
        },
        date_range: {
          start: config.startDate,
          end: config.endDate,
          frequency: config.frequency,
        },
        objective: config.objective,
        num_class: config.numClass,
        test_ratio: config.testRatio,
        val_ratio: config.valRatio,
        params: {
          learning_rate: config.learningRate,
          max_depth: config.maxDepth,
          n_estimators: config.nEstimators,
          early_stopping_rounds: config.earlyStoppingRounds,
          subsample: config.subsample,
          colsample_bytree: config.colsampleBytree,
          gamma: config.gamma,
          min_child_weight: config.minChildWeight,
          scale_pos_weight: config.scalePosWeight,
          reg_alpha: config.regAlpha,
          reg_lambda: config.regLambda,
        },
      }
      if (csvPath) body.csv_path = csvPath
      const plainBody = JSON.parse(JSON.stringify(body))
      const server = localStorage.getItem('remote') || 'localhost:19107'
      const token = localStorage.getItem('token') || ''

      // Step 1: POST 触发训练（幂等：已有训练在跑则返回现有 session_id）
      const postResp = await axios.post('/v0/ml', plainBody)
      const sessionId = postResp.data?.session_id
      if (!sessionId) {
        ElMessage.error('训练启动失败：未返回 session_id')
        return null
      }

      // Step 2: GET SSE 订阅进度（可安全重连，自动回放历史事件）
      let result: TrainResult | null = null
      await fetchEventSource(
        `https://${server}/v0/ml?action=train&session_id=${sessionId}`,
        {
          method: 'GET',
          headers: { 'Authorization': token },
          // 页面隐藏时保持连接（Electron 切换 Tab 不断 SSE）
          openWhenHidden: true,

          onopen: async (response) => {
            if (response.ok) return
            const text = await response.text()
            throw new Error(text || `HTTP ${response.status}`)
          },

          onmessage: (event) => {
            if (!event.data) return
            try {
              const data = JSON.parse(event.data)
              onEvent?.(event.event || 'message', data)
              if (event.event === 'result') {
                result = data as TrainResult
                if (nodeLabel) result.node_label = nodeLabel
              } else if (event.event === 'error') {
                ElMessage.error(`训练失败: ${data.msg || data.message || '未知错误'}`)
              }
            } catch { /* skip malformed */ }
          },

          onclose: () => {},
          onerror: (err) => {
            // 已收到 result → 训练正常完成，连接关闭是 httplib chunked 正常行为
            // （Chromium 对 chunked SSE 关闭报 ERR_INCOMPLETE_CHUNKED_ENCODING，可忽略）
            if (result) return
            throw err
          },
        }
      )

      return result
    } catch (err: any) {
      const msg = err.response?.data?.message || err.message || '训练失败'
      ElMessage.error(`ML 训练失败: ${msg}`)
      console.error('[ML] train error:', err)
      return null
    }
  }


  /**
   * 特征收集：收集上游子图数据并计算统计信息
   */
  async function collect(
    script: string,
    config: {
      labelSource: string
      startDate: string
      endDate: string
      frequency: string
    },
    onEvent?: (type: string, data: any) => void,
  ): Promise<FeatureReport | null> {
    try {
      const body = {
        action: 'collect',
        script: normalizeCodeParams(convertLabelsToKeys(script)),
        label: { source: config.labelSource },
        date_range: {
          start: config.startDate,
          end: config.endDate,
          frequency: config.frequency,
        },
      }
      const plainBody = JSON.parse(JSON.stringify(body))
      const server = localStorage.getItem('remote') || 'localhost:19107'
      const token = localStorage.getItem('token') || ''

      const postResp = await axios.post('/v0/ml', plainBody)
      const sessionId = postResp.data?.session_id
      if (!sessionId) {
        ElMessage.error('特征收集启动失败')
        return null
      }

      let report: FeatureReport | null = null
      await fetchEventSource(
        `https://${server}/v0/ml?action=train&session_id=${sessionId}`,
        {
          method: 'GET',
          headers: { 'Authorization': token },
          openWhenHidden: true,
          onopen: async (response) => {
            if (response.ok) return
            throw new Error(`HTTP ${response.status}`)
          },
          onmessage: (event) => {
            if (!event.data) return
            try {
              const data = JSON.parse(event.data)
              onEvent?.(event.event || 'message', data)
              if (event.event === 'feature_stats') {
                report = data as FeatureReport
              } else if (event.event === 'error') {
                ElMessage.error(`特征收集失败: ${data.msg || data.error || '未知错误'}`)
              }
            } catch { /* skip */ }
          },
          onclose: () => {},
          onerror: (err) => {
            if (report) return
            throw err
          },
        }
      )
      return report
    } catch (err: any) {
      const msg = err.response?.data?.message || err.message || '特征收集失败'
      ElMessage.error(`特征收集失败: ${msg}`)
      console.error('[ML] collect error:', err)
      return null
    }
  }

  /** 计算 SHAP 值（可选日期过滤） */
  async function shap(modelId: number, startDate?: string, endDate?: string): Promise<ShapResult | null> {
    try {
      const body: Record<string, any> = { action: 'shap', model_id: modelId }
      if (startDate) body.start_date = startDate
      if (endDate) body.end_date = endDate
      const resp = await axios.post('/v0/ml', body)
      return resp.data as ShapResult
    } catch (err: any) {
      const msg = err.response?.data?.message || err.message || 'SHAP 计算失败'
      ElMessage.error(`SHAP 计算失败: ${msg}`)
      return null
    }
  }

  /** 删除已注册的模型 */
  async function deleteModel(modelId: number): Promise<boolean> {
    try {
      await axios.post('/v0/ml', { action: 'delete', model_id: modelId })
      return true
    } catch (err: any) {
      const msg = err.response?.data?.message || err.message || '删除失败'
      ElMessage.error(`模型删除失败: ${msg}`)
      return false
    }
  }

  /** 按文件路径删除磁盘上的模型 */
  async function deleteModelFile(modelPath: string): Promise<boolean> {
    try {
      await axios.post('/v0/ml', { action: 'delete_file', model_path: modelPath })
      return true
    } catch (err: any) {
      const msg = err.response?.data?.message || err.message || '删除失败'
      ElMessage.error(`模型删除失败: ${msg}`)
      return false
    }
  }

  /** 列出实验/生产模型 */
  async function listModels(strategyId?: string): Promise<{
    experiments: Array<{ path: string; filename: string; meta: any }>
    production: { path: string; filename: string; meta: any } | null
  } | null> {
    try {
      const params: Record<string, string> = { action: 'list' }
      if (strategyId) params.strategy_id = strategyId
      const resp = await axios.get('/v0/ml', { params })
      return resp.data
    } catch (err: any) {
      const msg = err.response?.data?.message || err.message || '模型列表获取失败'
      ElMessage.error(`模型列表获取失败: ${msg}`)
      return null
    }
  }

  /** 已废弃：模型绑定由 useModelBinding.bindModel 处理 */
  async function publishModel(_modelPath: string): Promise<null> {
    return null
  }

  /** 获取价格数据并计算标签（UP/FLAT/DOWN） */
  async function fetchLabelAnalysis(
    symbol: string,
    field: string,
    startDate: string,
    endDate: string,
    frequency: string,
    labelPeriod: number,
    volK: number,
    labelType: string,
    priceCache?: { dates: string[]; prices: number[]; symbol: string; field: string },
  ): Promise<LabelAnalysisResult | null> {
    try {
      // 提取纯数字代码: "600000.SH" → "600000"
      const code = symbol.includes('.') ? symbol.split('.')[0] : symbol

      const resp = await axios.get('/v0/stocks/history', {
        params: { id: code, type: frequency, start: startDate, end: endDate, right: 1 },
      })
      const raw: any[] = resp.data
      if (!Array.isArray(raw) || raw.length === 0) {
        ElMessage.warning('无价格数据')
        return null
      }

      const dates: string[] = []
      const prices: number[] = []
      for (const item of raw) {
        const dt = parseBackendDate(item.datetime)
        dates.push(dt)
        prices.push(Number(item[field]) || 0)
      }

      // 缓存价格数据（供滑块拖动时快速重算标签）
      if (priceCache) {
        priceCache.dates = dates
        priceCache.prices = prices
        priceCache.symbol = symbol
        priceCache.field = field
      }

      // 计算标签（与 xgboost_train.py compute_label 一致）
      const n = prices.length
      const labels = new Array<number>(n).fill(-1)
      let threshold = 0.015

      if (labelType === 'classification') {
        // 对数收益率标准差
        const logRets: number[] = []
        for (let i = 1; i < n; i++) {
          if (prices[i] > 0 && prices[i - 1] > 0) {
            logRets.push(Math.log(prices[i] / prices[i - 1]))
          }
        }
        const mean = logRets.reduce((a, b) => a + b, 0) / logRets.length
        const variance = logRets.reduce((a, b) => a + (b - mean) ** 2, 0) / logRets.length
        const sigma = Math.sqrt(variance)

        if (logRets.length >= 20 && sigma >= 1e-12) {
          threshold = volK * sigma * Math.sqrt(labelPeriod)
          threshold = Math.max(0.005, Math.min(0.10, threshold))
        }

        for (let i = 0; i < n; i++) {
          const futureIdx = i + labelPeriod
          if (futureIdx < n && prices[i] > 0) {
            const futureRet = prices[futureIdx] / prices[i] - 1
            if (futureRet > threshold) labels[i] = 0   // UP
            else if (futureRet < -threshold) labels[i] = 2 // DOWN
            else labels[i] = 1                           // FLAT
          }
        }
      } else {
        // 回归模式：标签 = 未来收益率
        for (let i = 0; i < n; i++) {
          const futureIdx = i + labelPeriod
          if (futureIdx < n && prices[i] > 0) {
            labels[i] = prices[futureIdx] / prices[i] - 1
          }
        }
        threshold = volK * 0.01 // 回归模式无阈值，仅用于显示
      }

      return { symbol, field, dates, prices, labels, threshold }
    } catch (err: any) {
      const msg = err.response?.data?.message || err.message || '标签分析失败'
      ElMessage.error(`标签分析失败: ${msg}`)
      console.error('[ML] fetchLabelAnalysis error:', err)
      return null
    }
  }

  /** 批量标签分析：对每个标的计算标签分布 */
  async function runBatchLabelAnalysis(
    symbols: string[],
    field: string,
    startDate: string,
    endDate: string,
    frequency: string,
    labelPeriod: number,
    volK: number,
    priceCache: { dates: string[]; prices: number[]; symbol: string; field: string },
    onProgress?: (current: number, total: number, symbol: string) => void,
  ): Promise<BatchLabelStat[]> {
    const results: BatchLabelStat[] = []

    for (let idx = 0; idx < symbols.length; idx++) {
      const symbol = symbols[idx]
      onProgress?.(idx + 1, symbols.length, symbol)

      let prices: number[]
      try {
        // 如果缓存匹配则复用，否则重新请求
        if (priceCache.symbol === symbol && priceCache.field === field && priceCache.dates.length > 0) {
          prices = priceCache.prices
        } else {
          const code = symbol.includes('.') ? symbol.split('.')[0] : symbol
          const resp = await axios.get('/v0/stocks/history', {
            params: { id: code, type: frequency, start: startDate, end: endDate, right: 1 },
          })
          const raw: any[] = resp.data
          if (!Array.isArray(raw) || raw.length === 0) continue
          prices = raw.map((item: any) => Number(item[field]) || 0)
        }

        const n = prices.length
        // 计算阈值
        const logRets: number[] = []
        for (let i = 1; i < n; i++) {
          if (prices[i] > 0 && prices[i - 1] > 0) {
            logRets.push(Math.log(prices[i] / prices[i - 1]))
          }
        }
        let threshold = 0.015
        if (logRets.length >= 20) {
          const mean = logRets.reduce((a, b) => a + b, 0) / logRets.length
          const variance = logRets.reduce((a, b) => a + (b - mean) ** 2, 0) / logRets.length
          const sigma = Math.sqrt(variance)
          if (sigma >= 1e-12) {
            threshold = volK * sigma * Math.sqrt(labelPeriod)
            threshold = Math.max(0.005, Math.min(0.10, threshold))
          }
        }
        // 统计标签
        let up = 0, flat = 0, down = 0
        for (let i = 0; i < n; i++) {
          const futureIdx = i + labelPeriod
          if (futureIdx < n && prices[i] > 0) {
            const ret = prices[futureIdx] / prices[i] - 1
            if (ret > threshold) up++
            else if (ret < -threshold) down++
            else flat++
          }
        }
        const total = up + flat + down
        if (total > 0) {
          results.push({
            symbol, total, up, flat, down,
            upPct: up / total * 100,
            flatPct: flat / total * 100,
            downPct: down / total * 100,
            threshold,
          })
        }
      } catch (err) {
        console.warn(`[ML] batch: ${symbol} 失败`, err)
      }
    }
    return results
  }

  /**
   * 参数优化（Optuna TPE 搜索）
   */
  async function optimize(
    script: string,
    config: {
      labelSource: string
      labelPeriod: number
      labelType: string
      labelShape: string
      volK: number
      objective: string
      numClass: number
      testRatio: number
      valRatio: number
      startDate: string
      endDate: string
      frequency: string
      csvPath?: string
      optimizeMetric: string
      nTrials: number
      paramDomains: Record<string, { min: number; max: number; step?: number; log?: boolean; enabled: boolean }>
    },
    onEvent?: (type: string, data: any) => void,
  ): Promise<any | null> {
    try {
      // 构建 param_domains：只包含 enabled 的参数
      const domains: Record<string, any> = {}
      for (const [k, v] of Object.entries(config.paramDomains)) {
        if (!v.enabled) continue
        const d: any = { min: v.min, max: v.max }
        if (v.step != null) d.step = v.step
        if (v.log) d.log = true
        domains[k] = d
      }

      const body: any = {
        action: 'optimize',
        script: normalizeCodeParams(convertLabelsToKeys(script)),
        label: {
          source: config.labelSource,
          period: config.labelPeriod,
          type: config.labelType,
          shape: config.labelShape,
          vol_k: config.volK,
        },
        date_range: {
          start: config.startDate,
          end: config.endDate,
          frequency: config.frequency,
        },
        objective: config.objective,
        num_class: config.numClass,
        test_ratio: config.testRatio,
        val_ratio: config.valRatio,
        optimize_metric: config.optimizeMetric,
        n_trials: config.nTrials,
        param_domains: JSON.stringify(domains),
      }
      if (config.csvPath) body.feature_cache = config.csvPath
      const plainBody = JSON.parse(JSON.stringify(body))
      const server = localStorage.getItem('remote') || 'localhost:19107'
      const token = localStorage.getItem('token') || ''

      const postResp = await axios.post('/v0/ml', plainBody)
      const sessionId = postResp.data?.session_id
      if (!sessionId) {
        ElMessage.error('参数优化启动失败：未返回 session_id')
        return null
      }

      let result: any = null
      await fetchEventSource(
        `https://${server}/v0/ml?action=train&session_id=${sessionId}`,
        {
          method: 'GET',
          headers: { 'Authorization': token },
          openWhenHidden: true,
          onopen: async (response) => {
            if (response.ok) return
            const text = await response.text()
            throw new Error(text || `HTTP ${response.status}`)
          },
          onmessage: (event) => {
            if (!event.data) return
            try {
              const data = JSON.parse(event.data)
              onEvent?.(event.event || 'message', data)
              if (event.event === 'result') {
                result = data
              } else if (event.event === 'error') {
                ElMessage.error(`优化失败: ${data.msg || data.message || '未知错误'}`)
              }
            } catch { /* skip malformed */ }
          },
          onclose: () => {},
          onerror: (err) => {
            if (result) return
            throw err
          },
        }
      )

      return result
    } catch (err: any) {
      const msg = err.response?.data?.message || err.message || '参数优化失败'
      ElMessage.error(`ML 优化失败: ${msg}`)
      console.error('[ML] optimize error:', err)
      return null
    }
  }

  return { train, collect, shap, deleteModel, deleteModelFile, listModels, publishModel, fetchLabelAnalysis, runBatchLabelAnalysis, optimize }
}

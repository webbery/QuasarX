// app/src/components/xgboost/composables/useXGBoostData.ts
// XGBoost 训练分析 API 调用

import axios from 'axios'
import { ElMessage } from 'element-plus'
import type { ShapResult, TrainResult } from './useXGBoostState'

export function useXGBoostData() {
  /**
   * 触发训练
   * @param script 策略图 JSON（字符串形式）
   * @param config 训练配置
   */
  async function train(
    script: string,
    config: {
      labelSource: string
      labelPeriod: number
      labelType: string
      threshold: number
      objective: string
      numClass: number
      testRatio: number
      learningRate: number
      maxDepth: number
      nEstimators: number
      earlyStoppingRounds: number
    },
  ): Promise<TrainResult | null> {
    try {
      const body = {
        action: 'train',
        script,
        label: {
          source: config.labelSource,
          period: config.labelPeriod,
          type: config.labelType,
          threshold: config.threshold,
        },
        objective: config.objective,
        num_class: config.numClass,
        test_ratio: config.testRatio,
        params: {
          learning_rate: config.learningRate,
          max_depth: config.maxDepth,
          n_estimators: config.nEstimators,
          early_stopping_rounds: config.earlyStoppingRounds,
        },
      }
      const resp = await axios.post('/v0/xgboost', body, { timeout: 600_000 })
      return resp.data as TrainResult
    } catch (err: any) {
      const msg = err.response?.data?.message || err.message || '训练失败'
      ElMessage.error(`XGBoost 训练失败: ${msg}`)
      console.error('[XGBoost] train error:', err)
      return null
    }
  }

  /** 计算 SHAP 值 */
  async function shap(modelId: number): Promise<ShapResult | null> {
    try {
      const resp = await axios.post('/v0/xgboost', { action: 'shap', model_id: modelId })
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
      await axios.post('/v0/xgboost', { action: 'delete', model_id: modelId })
      return true
    } catch (err: any) {
      const msg = err.response?.data?.message || err.message || '删除失败'
      ElMessage.error(`模型删除失败: ${msg}`)
      return false
    }
  }

  /** 解析策略图，提取所有节点输出名作为可选特征 */
  async function listFeatureColumns(script: string): Promise<string[]> {
    try {
      const parsed = JSON.parse(script)
      const names: string[] = []
      if (parsed.nodes && Array.isArray(parsed.nodes)) {
        for (const node of parsed.nodes) {
          const data = node.data || {}
          // QuoteInputNode 的输出：symbol.property（如 sh.600000.close）
          if (data.nodeType === 'input' && data.params?.code?.value) {
            const codes = Array.isArray(data.params.code.value)
              ? data.params.code.value
              : [data.params.code.value]
            const props = ['close', 'open', 'high', 'low', 'volume']
            for (const code of codes) {
              for (const p of props) names.push(`${code}.${p}`)
            }
          }
        }
      }
      return names
    } catch {
      return []
    }
  }

  return { train, shap, deleteModel, listFeatureColumns }
}

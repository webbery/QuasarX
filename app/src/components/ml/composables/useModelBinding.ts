// app/src/components/ml/composables/useModelBinding.ts
// bind 模型到策略节点（纯前端，不调 service）
//
// 流程：
//   1. 通过 service GET /v0/ml?action=download&model_id=N 下载 model_json + meta_json
//   2. Electron IPC 写入 appData/models/{strategyName}/{label}.json + .meta.json
//   3. 更新策略 JSON 的 data.models: [{label, version}]（version = ISO 时间戳）
//
// 重复 bind 同 label → 静默覆盖（version 改新时间戳，文件覆盖）

import { ref } from 'vue'
import axios from 'axios'
import { ElMessage } from 'element-plus'
import { ipcRenderer } from 'electron'

export interface ModelBinding {
  label: string
  version: string        // ISO 时间戳，例如 "2026-08-06T15:30:12"
  bound_at: string       // 同上，冗余字段方便前端展示
}

export function useModelBinding() {
  const binding = ref(false)
  const lastError = ref<string | null>(null)

  /**
   * 将训练完成的模型 bind 到当前策略的指定 XGBoostNode label。
   *
   * @param modelId     训练返回的 model_id（用于从 service 下载文件）
   * @param strategyName 策略实例名（决定 production 文件名前缀和 Electron 本地目录）
   * @param label        策略节点 label（XGBoostNode.data.label，与 production 文件名后缀一致）
   * @param updateStrategyData 回调：传入绑定信息，由调用方更新 IndexedDB 中的 strategy JSON
   */
  async function bindModel(
    modelId: number,
    strategyName: string,
    label: string,
    updateStrategyData: (bindings: ModelBinding[]) => void,
  ): Promise<boolean> {
    if (!strategyName || !label) {
      ElMessage.error('bind 失败：缺少 strategyName 或 label')
      return false
    }
    binding.value = true
    lastError.value = null
    try {
      // 1. 从 service 下载模型文件
      const server = localStorage.getItem('remote') || 'localhost:19107'
      const token = localStorage.getItem('token') || ''
      const resp = await axios.get(`https://${server}/v0/ml`, {
        params: { action: 'download', model_id: String(modelId) },
        headers: { Authorization: token },
      })
      const modelJson: string = resp.data?.model_json
      const metaJson: string = resp.data?.meta_json || '{}'
      if (!modelJson) {
        throw new Error('service 返回的 model_json 为空')
      }

      // 2. 通过 Electron IPC 写入本地 appData/models/{strategyName}/{label}.json
      const version = new Date().toISOString().slice(0, 19)  // YYYY-MM-DDTHH:MM:SS
      const writeRes = await ipcRenderer.invoke('model-bind-write', {
        strategyName,
        label,
        modelJson,
        metaJson,
      })
      if (!writeRes?.success) {
        throw new Error(writeRes?.error || '本地写入失败')
      }

      // 3. 更新策略 JSON（由调用方操作 IndexedDB，避免 composable 直接依赖 store）
      updateStrategyData([{ label, version, bound_at: version }])

      ElMessage.success(`已应用到策略节点「${label}」${version}`)
      return true
    } catch (err: any) {
      const msg = err.response?.data?.message || err.message || 'bind 失败'
      lastError.value = msg
      ElMessage.error(`应用失败: ${msg}`)
      return false
    } finally {
      binding.value = false
    }
  }

  /**
   * 读取策略当前所有 XGBoostNode 的 binding 列表（从策略 JSON 的 data.models 字段）
   */
  function getBindings(strategyJson: any): ModelBinding[] {
    if (!strategyJson) return []
    const models = strategyJson?.data?.models
    if (!Array.isArray(models)) return []
    return models.filter((m: any) => m && typeof m.label === 'string')
  }

  /**
   * 计算 production 文件名（不含路径），供 XGBoostNode.modelFile auto-fill 使用
   */
  function productionFileName(strategyName: string, label: string): string {
    return `production/${strategyName}-${label}.json`
  }

  /**
   * 修改磁盘上 .meta.json 的白名单字段（version / display_name / description）。
   * 不动模型 .json 文件本身，避免破坏 XGBoostNode.modelFile 路径引用。
   * 路径必须在 {dbPath}/models/experiments/ 或 {dbPath}/models/production/ 下。
   *
   * 用于：
   *  - StrategyTracker 中点击"编辑元数据"修改 production meta
   *  - 未来 ML 训练表单给训练产物打语义版本号
   *
   * @param modelPath 逻辑路径（如 production/CTA_v16-CTA_v16.json）或绝对路径
   * @param fields    白名单字段 {version?, display_name?, description?}
   * @returns true 表示后端写入成功
   */
  async function updateModelMeta(
    modelPath: string,
    fields: { version?: string; display_name?: string; description?: string },
  ): Promise<boolean> {
    if (!modelPath) {
      ElMessage.error('updateModelMeta 失败：缺少 modelPath')
      return false
    }
    const whitelist: Record<string, string> = {}
    if (fields.version !== undefined) whitelist.version = fields.version
    if (fields.display_name !== undefined) whitelist.display_name = fields.display_name
    if (fields.description !== undefined) whitelist.description = fields.description
    if (Object.keys(whitelist).length === 0) {
      ElMessage.error('updateModelMeta 失败：fields 至少包含一个白名单字段')
      return false
    }
    try {
      const resp = await axios.post('/v0/ml', {
        action: 'update_meta',
        model_path: modelPath,
        fields: whitelist,
      })
      if (resp.data?.status !== 'ok') {
        throw new Error(resp.data?.message ?? 'invalid response')
      }
      ElMessage.success('模型元数据已更新')
      return true
    } catch (err: any) {
      const msg = err.response?.data?.message || err.message || 'update_meta 失败'
      ElMessage.error(`更新失败: ${msg}`)
      return false
    }
  }

  return { binding, lastError, bindModel, getBindings, productionFileName, updateModelMeta }
}
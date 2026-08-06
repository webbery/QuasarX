// app/src/components/ml/composables/useStrategyDeploy.ts
// 策略部署（含模型文件 multipart 上传）
//
// 流程：
//   1. 预检：策略 JSON 中每个 XGBoostNode 的 modelFile 必须匹配 production/{inst}-{label}.json
//      且 data.models 中每个 label 必须在本地 appData/models/{inst}/ 存在对应文件
//   2. 构建 multipart FormData:
//      - "script" part: 策略 JSON 字符串
//      - "model_{label}" part: .json 字节
//      - "model_{label}_meta" part: .meta.json 字节
//   3. POST /v0/strategy (multipart) 部署

import { ref } from 'vue'
import axios from 'axios'
import { ElMessage } from 'element-plus'
import { ipcRenderer } from 'electron'

export function useStrategyDeploy() {
  const deploying = ref(false)
  const lastError = ref<string | null>(null)

  /**
   * 部署策略到 service（multipart + 模型文件）。
   *
   * @param strategyName 策略实例名（必须与本地 appData/models/{inst}/ 一致）
   * @param strategyJson 完整策略 JSON（含 graph.nodes / data.models）
   */
  async function deployStrategy(
    strategyName: string,
    strategyJson: any,
  ): Promise<{ success: boolean; message?: string; running?: boolean }> {
    lastError.value = null
    if (!strategyName) {
      ElMessage.error('部署失败：策略名不能为空')
      return { success: false, message: 'empty strategyName' }
    }

    // 1. 预检：收集所有 XGBoostNode 的 modelFile 引用
    const xgbNodes: Array<{ id: string; label: string; modelFile: string }> = []
    const nodes = strategyJson?.graph?.nodes
    if (Array.isArray(nodes)) {
      for (const n of nodes) {
        if (n?.data?.nodeType === 'xgboost') {
          const label = n.data.label
          const modelFile = n.data?.params?.modelFile?.value || n.data?.params?.modelFile || ''
          xgbNodes.push({ id: n.id, label, modelFile })
        }
      }
    }

    // 校验 1：modelFile 必须匹配 production/{strategyName}-{label}.json（禁止跨策略）
    for (const xn of xgbNodes) {
      const expected = `production/${strategyName}-${xn.label}.json`
      if (xn.modelFile !== expected) {
        const msg = `XGBoostNode「${xn.label}」modelFile='${xn.modelFile}' 与策略「${strategyName}」不匹配，禁止跨策略引用（应为 '${expected}'）`
        ElMessage.error(msg)
        lastError.value = msg
        return { success: false, message: msg }
      }
    }

    // 校验 2：data.models 中的每个 label 都必须有本地文件
    const bindings: Array<{ label: string; version: string }> = Array.isArray(strategyJson?.data?.models)
      ? strategyJson.data.models.filter((m: any) => m?.label)
      : []
    const labels = new Set(xgbNodes.map(x => x.label))
    // data.models 里的 label 必须与 XGBoostNode label 一致
    for (const b of bindings) {
      if (!labels.has(b.label)) {
        const msg = `data.models 中的 label '${b.label}' 与任何 XGBoostNode 不匹配`
        ElMessage.error(msg)
        lastError.value = msg
        return { success: false, message: msg }
      }
    }

    // 2. 构建 multipart
    deploying.value = true
    try {
      const form = new FormData()
      form.append('script', JSON.stringify(strategyJson))
      form.append('name', strategyName)

      // 每个 XGBoostNode 都要附 model_{label} 和 model_{label}_meta parts
      for (const xn of xgbNodes) {
        const readRes = await ipcRenderer.invoke('model-read-for-deploy', {
          strategyName,
          label: xn.label,
        })
        if (!readRes?.success) {
          const msg = `模型文件读取失败: ${readRes?.error || xn.label}`
          ElMessage.error(msg)
          lastError.value = msg
          return { success: false, message: msg }
        }
        // .json part
        form.append(`model_${xn.label}`, new Blob([new Uint8Array(readRes.modelBytes)], { type: 'application/json' }), `${xn.label}.json`)
        // .meta.json part
        if (readRes.metaBytes && readRes.metaBytes.length > 0) {
          form.append(`model_${xn.label}_meta`, new Blob([new Uint8Array(readRes.metaBytes)], { type: 'application/json' }), `${xn.label}.meta.json`)
        }
      }

      const server = localStorage.getItem('remote') || 'localhost:19107'
      const token = localStorage.getItem('token') || ''

      // 3. POST multipart
      const resp = await axios.post(`https://${server}/v0/strategy`, form, {
        headers: {
          Authorization: token,
          'Content-Type': 'multipart/form-data',
        },
      })

      if (resp.data?.running === false && resp.data?.message) {
        ElMessage.success(`策略「${strategyName}」部署成功`)
      } else {
        ElMessage.success(`策略「${strategyName}」部署成功${resp.data?.running ? '（已运行）' : ''}`)
      }
      return { success: true, message: resp.data?.message, running: resp.data?.running }
    } catch (err: any) {
      const msg = err.response?.data?.message || err.message || '部署失败'
      lastError.value = msg
      ElMessage.error(`部署失败: ${msg}`)
      return { success: false, message: msg }
    } finally {
      deploying.value = false
    }
  }

  return { deploying, lastError, deployStrategy }
}
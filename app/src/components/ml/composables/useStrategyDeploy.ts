// app/src/components/ml/composables/useStrategyDeploy.ts
// 策略部署（含模型文件 multipart 上传）
//
// 实现已收敛到 app/src/composables/useStrategyControl.ts（统一 URL / 鉴权 / 错误处理）。
// 本文件保留原有的 ElMessage 交互与 { deploying, lastError, deployStrategy } 形状，
// 让 App.vue 等既有调用点零改动。

import { ref } from 'vue'
import { ElMessage } from 'element-plus'
import { useStrategyControl } from '@/composables/useStrategyControl'

export function useStrategyDeploy() {
  const deploying = ref(false)
  const lastError = ref<string | null>(null)
  const control = useStrategyControl()

  /**
   * 部署策略到 service（multipart + 模型文件）。
   *
   * @param strategyName 策略实例名（必须与本地 appData/models/{inst}/ 一致）
   * @param strategyJson 完整策略 JSON（含 graph.nodes / data.models）
   */
  async function deployStrategy(
    strategyName: string,
    strategyJson: any,
    options?: { force?: boolean },
  ): Promise<{ success: boolean; message?: string; running?: boolean; conflict?: boolean }> {
    lastError.value = null
    if (!strategyName) {
      ElMessage.error('部署失败：策略名不能为空')
      return { success: false, message: 'empty strategyName' }
    }

    deploying.value = true
    try {
      const result = await control.deploy(strategyName, strategyJson, options)

      if (!result.success) {
        lastError.value = result.message ?? null
        // 冲突（同名策略在跑）不算错误，交给调用方决定是否强制覆盖
        if (!result.conflict) {
          ElMessage.error(result.message || '部署失败')
        }
        return {
          success: false,
          message: result.message,
          conflict: result.conflict,
          running: result.running,
        }
      }

      ElMessage.success(
        `策略「${strategyName}」部署成功${result.running ? '（已运行）' : ''}`,
      )
      return { success: true, message: result.message, running: result.running }
    } finally {
      deploying.value = false
    }
  }

  return { deploying, lastError, deployStrategy }
}

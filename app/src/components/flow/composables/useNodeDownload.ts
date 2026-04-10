// src/components/flow/composables/useNodeDownload.ts
// 下载逻辑

import { ref, computed } from 'vue'

export function useNodeDownload(nodeData: { params: Record<string, any>, id: string, nodeType: string }) {
  const downloadStatus = ref('')

  const canDownload = computed(() => {
    const downloadPath = Object.values(nodeData.params).find(
      (param: any) => param.type === 'directory'
    )?.value
    return downloadPath && downloadPath.trim() !== ''
  })

  async function downloadFile(paramKey: string) {
    if (!canDownload.value) {
      downloadStatus.value = '请先选择下载路径'
      setTimeout(() => { downloadStatus.value = '' }, 3000)
      return
    }

    try {
      downloadStatus.value = '下载中...'

      const downloadPath = Object.values(nodeData.params).find(
        (param: any) => param.type === 'directory'
      )?.value

      const success = await (window as any).downloadDebugFile?.({
        nodeId: nodeData.id,
        downloadPath: downloadPath,
        nodeType: nodeData.nodeType
      })

      if (success) {
        downloadStatus.value = '下载完成'
        setTimeout(() => { downloadStatus.value = '' }, 2000)
      } else {
        downloadStatus.value = '下载失败'
        setTimeout(() => { downloadStatus.value = '' }, 3000)
      }
    } catch (error: any) {
      console.error('下载失败:', error)
      downloadStatus.value = '下载错误: ' + error.message
      setTimeout(() => { downloadStatus.value = '' }, 3000)
    }
  }

  return {
    downloadStatus,
    canDownload,
    downloadFile
  }
}

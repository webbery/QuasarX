/**
 * Electron 传输层
 *
 * 静态 import electron（渲染进程构建由 vite-plugin-electron-renderer 改写；
 * Agent Lab 构建时被 alias 到 electron-stub）。
 */

import { ipcRenderer } from 'electron'
import type { MemoryTransport } from './ipc'

/** 构造走 ipcRenderer 的传输层 */
export function createElectronTransport(): MemoryTransport {
  return {
    async invoke(channel: string, args: unknown): Promise<any> {
      return ipcRenderer.invoke(channel, args)
    },
  }
}

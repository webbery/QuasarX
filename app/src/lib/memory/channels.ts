/**
 * 记忆 IPC channel 名
 *
 * 渲染进程与主进程共用，改这里两边同步。
 */

export const MEMORY_CHANNELS = {
  init: 'memory-init',
  upsert: 'memory-upsert',
  searchChannels: 'memory-search-channels',
  listByBank: 'memory-list-by-bank',
  getByIds: 'memory-get-by-ids',
  deleteBySource: 'memory-delete-by-source',
  deleteByBank: 'memory-delete-by-bank',
} as const

export type MemoryChannel = (typeof MEMORY_CHANNELS)[keyof typeof MEMORY_CHANNELS]

/**
 * ML 模块共享类型定义
 */

/** 流程步骤节点 */
export interface FlowNode {
  key: string
  index: string
  label: string
  summary: string
  statusText: string
  done: boolean
  failed: boolean
  locked: boolean
  active: boolean
  title: string
}

/** 会话运行记录 */
export interface SessionRun {
  at: string
  stage: 'collect' | 'label' | 'train'
  stageLabel: string
  detail: string
}

/** 控制台日志行 */
export interface ConsoleRow {
  level: 'error' | 'done' | 'step' | 'warn' | 'info'
  tag: string
  text: string
}

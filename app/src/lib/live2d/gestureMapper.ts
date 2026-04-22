/**
 * Live2D 情境 → 动作映射
 * 
 * 根据不同的情境触发相应的模型动作
 */

export interface GestureMapping {
  trigger: string          // 触发条件（视图名称或事件）
  motion: string           // 播放的动作
  message?: string         // 可选的自动推送消息
  priority: number         // 优先级（数字越大优先级越高）
}

/**
 * 默认动作映射
 */
export const DEFAULT_GESTURE_MAPPINGS: GestureMapping[] = [
  {
    trigger: 'account',
    motion: 'wave',
    message: '欢迎来到账户总览！需要我分析市场行情吗？',
    priority: 1,
  },
  {
    trigger: 'strategy',
    motion: 'think',
    message: '正在设计策略？我可以帮你优化参数和逻辑。',
    priority: 1,
  },
  {
    trigger: 'risk',
    motion: 'alert',
    message: '风险控制很重要，让我帮你分析一下当前风险状况。',
    priority: 2,
  },
  {
    trigger: 'position',
    motion: 'analyze',
    message: '让我看看你的持仓结构，提供些调仓建议吧。',
    priority: 1,
  },
  {
    trigger: 'datacenter',
    motion: 'point',
    message: '需要查询什么数据？我可以帮你。',
    priority: 1,
  },
  {
    trigger: 'knowledge_base',
    motion: 'idle',
    message: undefined,  // 不自动推送消息
    priority: 0,
  },
]

/**
 * 动作名称常量
 */
export const MOTION_NAMES = {
  IDLE: 'idle',
  SPEAK: 'speak',
  WAVE: 'wave',
  THINK: 'think',
  ALERT: 'alert',
  ANALYZE: 'analyze',
  POINT: 'point',
} as const

/**
 * 根据触发条件查找映射
 */
export function findGestureMapping(trigger: string): GestureMapping | undefined {
  return DEFAULT_GESTURE_MAPPINGS.find(m => m.trigger === trigger)
}

/**
 * 获取所有支持的视图触发器
 */
export function getViewTriggers(): string[] {
  return DEFAULT_GESTURE_MAPPINGS.map(m => m.trigger)
}

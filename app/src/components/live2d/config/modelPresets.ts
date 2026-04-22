/**
 * Live2D 模型预设配置
 */

export interface ModelPreset {
  id: string
  name: string
  path: string              // model3.json 路径
  scale: number             // 缩放比例
  offsetX: number           // X 偏移
  offsetY: number           // Y 偏移
  motions: {
    idle: string            // 待机动作
    speak: string           // 说话动作
    wave: string            // 挥手
    think?: string          // 思考
    alert?: string          // 提醒
    analyze?: string        // 分析
    point?: string          // 指示
  }
}

/**
 * 默认助手模型配置
 * 
 * 注意：需要先将模型文件放置到以下目录：
 * - 开发环境: app/src/assets/live2d/models/default/
 * - 生产环境: app/public/live2d/models/default/
 * 
 * 模型文件结构：
 * default/
 * ├── model3.json
 * ├── motions/
 * │   ├── idle.motion3.json
 * │   ├── speak.motion3.json
 * │   └── wave.motion3.json
 * └── textures/
 *     └── texture_00.png
 */
export const DEFAULT_MODEL_PRESET: ModelPreset = {
  id: 'default',
  name: '默认助手',
  path: '',  // 运行时由 modelLoader.ts 填充
  scale: 0.8,
  offsetX: 0,
  offsetY: 20,
  motions: {
    idle: 'idle',
    speak: 'speak',
    wave: 'wave',
    think: 'think',
    alert: 'alert',
    analyze: 'analyze',
    point: 'point',
  },
}

/**
 * 模型预设列表
 * 可以添加更多模型
 */
export const MODEL_PRESETS: ModelPreset[] = [
  DEFAULT_MODEL_PRESET,
]

/**
 * 根据 ID 获取模型预设
 */
export function getModelPresetById(id: string): ModelPreset | undefined {
  return MODEL_PRESETS.find(p => p.id === id)
}

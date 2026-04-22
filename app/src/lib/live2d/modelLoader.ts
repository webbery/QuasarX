/**
 * Live2D 模型路径处理
 * 
 * 开发环境: /src/assets/live2d/models/
 * 生产环境: /live2d/models/ (从 public/ 目录复制)
 */

/**
 * 获取 Live2D 模型基础路径
 */
export function getModelBasePath(): string {
  return import.meta.env.DEV
    ? '/src/assets/live2d/models/'
    : '/live2d/models/'
}

/**
 * 获取完整模型路径
 * @param modelName 模型名称，默认 'default'
 */
export function getModelPath(modelName: string = 'default'): string {
  return `${getModelBasePath()}${modelName}/model3.json`
}

/**
 * 获取模型目录路径
 * @param modelName 模型名称
 */
export function getModelDir(modelName: string = 'default'): string {
  return `${getModelBasePath()}${modelName}/`
}

/**
 * 可用的模型列表
 */
export const AVAILABLE_MODELS = [
  {
    id: 'default',
    name: '默认助手',
    path: getModelPath('default'),
  },
] as const

/**
 * 内置 JS 函数注册表
 * 用于获取系统信息（日期、时间、平台等）
 * 白名单机制，不执行任意代码
 */

export type BuiltinFn = (...args: any[]) => any | Promise<any>;

export const builtinFunctions: Record<string, BuiltinFn> = {
  // 日期时间
  'get-date': () =>
    new Date().toLocaleDateString('zh-CN', {
      year: 'numeric',
      month: 'long',
      day: 'numeric',
      weekday: 'long',
    }),

  'get-time': () => new Date().toLocaleTimeString('zh-CN'),

  'get-datetime': () => new Date().toLocaleString('zh-CN'),

  // 系统信息
  'get-platform': () => ({
    os: navigator.platform,
    userAgent: navigator.userAgent,
    language: navigator.language,
  }),

  // 页面信息
  'get-page-title': () => document.title,

  // 窗口信息
  'get-screen-size': () => ({
    width: window.screen.width,
    height: window.screen.height,
  }),
};

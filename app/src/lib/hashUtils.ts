/**
 * 简单的字符串/对象 hash 工具
 * 用于检测 intents.json 内容是否发生变化
 */

export function hashString(str: string): string {
  let hash = 0;
  for (let i = 0; i < str.length; i++) {
    const char = str.charCodeAt(i);
    hash = ((hash << 5) - hash) + char;
    hash = hash & hash; // 转为 32 位整数
  }
  return hash.toString(36);
}

export function hashObject(obj: any): string {
  // 排序 key 保证顺序一致性
  return hashString(JSON.stringify(obj, Object.keys(obj).sort()));
}

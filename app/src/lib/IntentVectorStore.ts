/**
 * 意图向量库（渲染进程 IPC 包装器）
 * 实际的 LanceDB/embedding 操作在主进程执行
 */

import { ipcRenderer } from 'electron';
import type { IntentRule, DiffResult } from './IntentRegistry';

export class IntentVectorStore {
  private initialized = false;

  /**
   * 初始化意图向量表（主进程中执行）
   */
  async init(): Promise<void> {
    if (this.initialized) return;

    const result = await ipcRenderer.invoke('intent-init');
    if (!result.success) {
      throw new Error(result.error);
    }

    this.initialized = true;
  }

  /**
   * 全量重建索引（首次加载或需要完整重建时）
   */
  async indexRules(rules: IntentRule[]): Promise<void> {
    await this.init();

    const rows = this.buildRows(rules);
    if (rows.length === 0) return;

    const result = await ipcRenderer.invoke('intent-index', rows);
    if (!result.success) {
      throw new Error(result.error);
    }
  }

  /**
   * 增量更新（根据 diff 结果只处理变更的规则）
   */
  async patchRules(diff: DiffResult): Promise<void> {
    await this.init();

    const toDelete = diff.removed.map(r => r.id);
    const toAdd = [
      ...diff.modified.flatMap(r => this.buildRows([r])),
      ...diff.added.flatMap(r => this.buildRows([r])),
    ];

    if (toDelete.length === 0 && toAdd.length === 0) return;

    const result = await ipcRenderer.invoke('intent-patch', { toDelete, toAdd });
    if (!result.success) {
      throw new Error(result.error);
    }
  }

  /**
   * 向量相似度检索，返回 Top-K 匹配意图
   */
  async search(query: string, topK: number = 3): Promise<Array<{ ruleId: string; score: number }>> {
    await this.init();

    const result = await ipcRenderer.invoke('intent-search', { queryText: query, topK });
    if (!result.success) {
      console.warn('[IntentVectorStore] search failed:', result.error);
      return [];
    }

    return result.results || [];
  }

  /**
   * 将规则列表转为向量行数据
   */
  private buildRows(rules: IntentRule[]): Array<{ id: string; text: string; ruleId: string }> {
    const rows: Array<{ id: string; text: string; ruleId: string }> = [];

    for (const rule of rules) {
      // description + examples 合并为一段文本
      const text = `${rule.description}。用户可能这样说：${rule.examples.join('；')}`;
      rows.push({
        id: `${rule.id}-main`,
        text,
        ruleId: rule.id,
      });

      // 每个 example 单独存一条，提高匹配粒度
      for (const ex of rule.examples) {
        rows.push({
          id: `${rule.id}-ex-${ex}`,
          text: ex,
          ruleId: rule.id,
        });
      }
    }

    return rows;
  }
}

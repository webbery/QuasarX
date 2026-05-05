/**
 * 意图规则注册表
 * 负责从配置文件加载规则，通过 hash 比对检测变更，计算 diff
 * 统一按多步处理，每个意图是一个可执行的步骤
 */

import intentsConfig from '@/config/intents.json';
import { hashObject } from './hashUtils';
import { ParamBuilder } from './ParamBuilder';

export interface IntentRule {
  id: string;
  api: string;
  description: string;
  examples: string[];
  provides: string[];          // 该步骤输出的数据类型
  requires: string[];          // 该步骤需要的数据类型
  params: Record<string, any>; // 参数模板，支持 $变量名
}

export interface DiffResult {
  added: IntentRule[];
  removed: IntentRule[];
  modified: IntentRule[];
  unchanged: IntentRule[];
}

interface RawIntent {
  id: string;
  api: string;
  description: string;
  examples: string[];
  provides: string[];
  requires: string[];
  params: Record<string, any>;
}

export class IntentRegistry {
  private rules: IntentRule[] = [];
  private currentHash: string = '';
  private previousHash: string = '';

  /**
   * 加载配置文件，计算 hash，与上次比较，返回 diff
   */
  load(): { hasChanged: boolean; diff: DiffResult | null } {
    this.previousHash = this.currentHash;

    // 只 hash intents 内容，不包含外层结构
    this.currentHash = hashObject(intentsConfig.intents);

    const hasChanged = this.currentHash !== this.previousHash || this.previousHash === '';

    // 转成规则对象
    const newRules = (intentsConfig.intents as RawIntent[]).map((intent: RawIntent) => ({
      id: intent.id,
      api: intent.api,
      description: intent.description,
      examples: intent.examples,
      provides: intent.provides || [],
      requires: intent.requires || [],
      params: intent.params || {},
    }));

    let diff: DiffResult | null = null;
    if (hasChanged && this.rules.length > 0) {
      diff = this.computeDiff(this.rules, newRules);
    }

    this.rules = newRules;
    return { hasChanged, diff };
  }

  /**
   * 计算新旧规则的差异
   */
  private computeDiff(oldRules: IntentRule[], newRules: IntentRule[]): DiffResult {
    const oldIds = new Set(oldRules.map(r => r.id));
    const newIds = new Set(newRules.map(r => r.id));

    const added = newRules.filter(r => !oldIds.has(r.id));
    const removed = oldRules.filter(r => !newIds.has(r.id));

    const modified: IntentRule[] = [];
    const unchanged: IntentRule[] = [];

    for (const newRule of newRules) {
      const oldRule = oldRules.find(r => r.id === newRule.id);
      if (oldRule && this.ruleContentChanged(oldRule, newRule)) {
        modified.push(newRule);
      } else if (oldRule) {
        unchanged.push(newRule);
      }
    }

    return { added, removed, modified, unchanged };
  }

  /**
   * 比对单条规则内容是否变化
   */
  private ruleContentChanged(old: IntentRule, current: IntentRule): boolean {
    return (
      old.api !== current.api ||
      old.description !== current.description ||
      JSON.stringify(old.examples.sort()) !== JSON.stringify(current.examples.sort()) ||
      JSON.stringify(old.provides.sort()) !== JSON.stringify(current.provides.sort()) ||
      JSON.stringify(old.requires.sort()) !== JSON.stringify(current.requires.sort()) ||
      JSON.stringify(old.params) !== JSON.stringify(current.params)
    );
  }

  getRules(): IntentRule[] {
    return [...this.rules];
  }
}

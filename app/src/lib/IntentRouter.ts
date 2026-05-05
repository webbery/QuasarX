/**
 * 意图路由器
 * 协调 IntentRegistry（规则加载/diff）和 IntentVectorStore（向量索引/检索）
 * 启动时通过 hash 比对决定全量重建还是增量更新
 * 运行时支持多意图自动编排
 */

import { IntentRegistry, type IntentRule, type DiffResult } from './IntentRegistry';
import { IntentVectorStore } from './IntentVectorStore';

const SIMILARITY_THRESHOLD = 0.5;

export interface IntentMatch {
  rule: IntentRule;
  score: number;
}

export interface OrchestratedResult {
  steps: IntentMatch[];       // 按执行顺序排列
  hasCycle: boolean;          // 是否存在循环依赖
  error?: string;
}

export class IntentRouter {
  private registry = new IntentRegistry();
  private vectorStore = new IntentVectorStore();
  private ruleMap = new Map<string, IntentRule>();
  private isReady = false;
  private isFirstLoad = true;

  /**
   * 初始化：加载规则，比对 hash，更新向量索引
   */
  async init(): Promise<void> {
    if (this.isReady) return;

    // 1. 加载规则，拿到 diff
    const { hasChanged, diff } = this.registry.load();

    // 2. 构建 ruleMap
    this.ruleMap.clear();
    for (const rule of this.registry.getRules()) {
      this.ruleMap.set(rule.id, rule);
    }

    // 3. 初始化向量库
    await this.vectorStore.init();

    // 4. 根据情况决定索引策略
    if (this.isFirstLoad) {
      // 首次启动：全量索引
      console.log('[IntentRouter] 首次加载，全量索引意图规则...');
      await this.vectorStore.indexRules(this.registry.getRules());
      this.isFirstLoad = false;
    } else if (hasChanged && diff) {
      // 非首次且内容变更：增量更新
      console.log(
        `[IntentRouter] 检测到规则变更: +${diff.added.length} -${diff.removed.length} ~${diff.modified.length}，增量更新索引...`
      );
      await this.vectorStore.patchRules(diff);
    } else {
      console.log('[IntentRouter] 意图规则无变更，跳过索引更新');
    }

    this.isReady = true;
  }

  /**
   * 重置状态（用于测试或手动刷新）
   */
  reset(): void {
    this.isReady = false;
    this.isFirstLoad = true;
  }

  /**
   * 意图匹配：返回所有 score > 阈值的意图
   */
  async match(input: string): Promise<IntentMatch[]> {
    await this.init();

    const results = await this.vectorStore.search(input, 5);

    const matches: IntentMatch[] = [];
    for (const r of results) {
      if (r.score < SIMILARITY_THRESHOLD) continue;

      const rule = this.ruleMap.get(r.ruleId);
      if (!rule) continue;

      // 避免重复（同一个 ruleId 只保留最高分）
      if (!matches.some(m => m.rule.id === rule.id)) {
        matches.push({ rule, score: r.score });
      }
    }

    // 按 score 降序
    matches.sort((a, b) => b.score - a.score);
    return matches;
  }

  /**
   * 自动编排：根据 provides/requires 构建依赖图，拓扑排序得到执行顺序
   */
  orchestrate(matches: IntentMatch[]): OrchestratedResult {
    if (matches.length === 0) {
      return { steps: [], hasCycle: false };
    }

    if (matches.length === 1) {
      return { steps: matches, hasCycle: false };
    }

    // 构建依赖图
    const graph = new Map<string, { match: IntentMatch; unresolved: Set<string> }>();
    const provided = new Set<string>();

    // 收集所有意图提供的数据
    for (const m of matches) {
      for (const p of m.rule.provides) {
        provided.add(p);
      }
    }

    // 初始化节点
    for (const m of matches) {
      const unresolved = new Set<string>();
      for (const req of m.rule.requires) {
        if (!provided.has(req)) {
          // 依赖外部提供，标记为未解决
          unresolved.add(req);
        }
      }
      graph.set(m.rule.id, { match: m, unresolved });
    }

    // 拓扑排序
    const order: IntentMatch[] = [];
    const satisfied = new Set<string>();

    // 初始时，所有 requires 为空的节点可执行
    for (const m of matches) {
      if (m.rule.requires.length === 0) {
        // 先添加这些
      }
    }

    while (graph.size > 0) {
      // 找到所有依赖已满足的节点
      const ready: string[] = [];
      for (const [id, node] of graph) {
        const allSatisfied = [...node.unresolved].every(req => satisfied.has(req));
        if (allSatisfied) {
          ready.push(id);
        }
      }

      if (ready.length === 0) {
        // 没有可执行的节点 → 循环依赖或缺失依赖
        const remaining = [...graph.keys()];
        return {
          steps: order,
          hasCycle: true,
          error: `无法满足依赖: ${remaining.join(', ')}`,
        };
      }

      // 执行 ready 节点
      for (const id of ready) {
        const node = graph.get(id)!;
        order.push(node.match);
        // 标记该节点提供的数据为已满足
        for (const p of node.match.rule.provides) {
          satisfied.add(p);
        }
        graph.delete(id);
      }
    }

    return { steps: order, hasCycle: false };
  }
}
